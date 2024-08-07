#include <shm_raw.hpp>
#include "RawReader.hpp"
#include <ThreadPool.hpp>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <boost/regex.hpp>
#include <libgen.h>
#include <limits.h>
#include <boost/algorithm/string/replace.hpp>


using namespace std;
using namespace PETSYS;


static const unsigned dataFileBufferSize = 131072; // 128K

static void normalizeLine(char *line) {
	std::string s = std::string(line);
	// Remove carriage return, from Windows written files
	s = boost::regex_replace(s, boost::regex("\r"), "");
	// Remove comments
	s = boost::regex_replace(s, boost::regex("\\s*#.*"), "");
	// Remove leading white space
	s = boost::regex_replace(s, boost::regex("^\\s+"), "");
	// Remove trailing whitespace
	s = boost::regex_replace(s, boost::regex("\\s+$"), "");
	// Normalize white space to tab
	s = boost::regex_replace(s, boost::regex("\\s+"), "\t");
	strcpy(line, s.c_str());	
}


RawReader::RawReader() :
	dataFile(-1), indexFile(NULL)
{
	assert(dataFileBufferSize >= MaxRawDataFrameSize * sizeof(uint64_t));
	dataFileBuffer = new char[dataFileBufferSize];
	dataFileBufferPtr = dataFileBuffer;
	dataFileBufferEnd = dataFileBuffer;
}

RawReader::~RawReader()
{
	delete [] dataFileBuffer;
	close(dataFile);

	if(indexFile != NULL) fclose(indexFile);
}

RawReader *RawReader::openFile(const char *fnPrefix, timeref_t tb)
{
	RawReader *reader = new RawReader();

	char fName[1024];

	// Try to open temp index file
	sprintf(fName, "%s.tmpf", fnPrefix);
	reader->indexFile = fopen(fName, "r");
	reader->indexIsTemp = true;

	// Fallback to open regular index file
	if(reader->indexFile == NULL) {
		sprintf(fName, "%s.idxf", fnPrefix);
		reader->indexFile = fopen(fName, "r");
		if(reader->indexFile == NULL)  {
			fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
			exit(1);
		}

		reader->indexIsTemp = false;
	}


	sprintf(fName, "%s.rawf", fnPrefix);
	reader->dataFile = open(fName, O_RDONLY);
	if(reader->dataFile == -1) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
                exit(1);
	}

	uint64_t header[8];
	ssize_t r = read(reader->dataFile, (void *)header, sizeof(uint64_t)*8);
	if(r < 1) {
		fprintf(stderr, "Could not read from '%s': %s\n", fName, strerror(errno));
		exit(1);
	}
	else if (r < sizeof(uint64_t)*8) {
		fprintf(stderr, "Read only %ld bytes from '%s', expected %lu\n", r, fName, sizeof(uint64_t)*8);
		exit(1);
	}

	reader->frequency = header[0] & 0xFFFFFFFFUL;
	if ((header[2] & 0x8000) != 0) { 
		reader->triggerID = header[2] & 0x7FFF; 
	}
	else {
		reader->triggerID = -1;
	}
       
	if(header[3]!=0){
		sprintf(fName, "%s.modf", fnPrefix);
		FILE *modeFile = fopen(fName, "r");
		if(modeFile == NULL) {
			fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
			exit(1);
		}
		char line[PATH_MAX];
		while(fscanf(modeFile, "%[^\n]\n", line) == 1) {
			normalizeLine(line);
			if(strlen(line) == 0) continue;
			unsigned portID, slaveID, chipID,channelID;
			char mode[128];		
			if(sscanf(line, "%d\t%u\t%u\t%u\t%s", &portID, &slaveID, &chipID, &channelID, mode)!= 5) continue;
			unsigned long gChannelID = 0;
			gChannelID |= channelID;
			gChannelID |= (chipID << 6);
			gChannelID |= (slaveID << 12);
			gChannelID |= (portID << 17);
			reader->qdcMode[gChannelID] = strcmp(mode, "qdc") == 0;
		}
	}
	else {
		for(unsigned long gChannelID = 0; gChannelID < MAX_NUMBER_CHANNELS; gChannelID++)
			reader->qdcMode[gChannelID] = (header[0] & 0x100000000UL) != 0;
	}
	
	uint32_t systemFrequency = header[0] & 0xFFFFFFFFUL;
	memcpy((void*)&(reader->daqSynchronizationEpoch), &header[1], sizeof(double));
	reader->daqSynchronizationEpoch *= systemFrequency;
	reader->fileCreationDAQTime = header[4];
	reader->tb = tb;

	return reader;
}

double RawReader::getFrequency()
{
	return (double) frequency;
}

bool RawReader::isQDC(unsigned int gChannelID)
{
	return qdcMode[gChannelID];
}

bool RawReader::isTOT()
{
	bool isToT = true;
	for(unsigned long gChannelID = 0; gChannelID < MAX_NUMBER_CHANNELS; gChannelID++){
		if(isQDC(gChannelID) == true){
			isToT = false;
			break;
		}	
	}
	return isToT;
}

int RawReader::getTriggerID()
{
	return triggerID;
}

int RawReader::readFromDataFile(char *buf, int count)
{
	int rval = 0;
	while(rval < count) {
		// Read from file if needed
		if(dataFileBufferPtr == dataFileBufferEnd) {
			int r = read(dataFile, dataFileBuffer, dataFileBufferSize);
			if(r < 0) {
				return -1;
			}

			if(r == 0) {
				if(getStepEnd() == ULLONG_MAX) {
					// We're in follow mode, so let's just retry again
					continue;
				}
				else {
					// We're not in follow mode (any more)
					// Make one mode attempt since we may have switched from follow to normal mode
					// after the previous read attempt

					r = read(dataFile, dataFileBuffer, dataFileBufferSize);
					if(r < 0) {
						return -1;
					}

				}
			}


			dataFileBufferPtr = dataFileBuffer;
			dataFileBufferEnd = dataFileBuffer + r;

			off_t current = lseek(dataFile, 0, SEEK_CUR);
			readahead(dataFile, current, dataFileBufferSize);
		}

		int countRemaining = count - rval;
		int bufferRemaining = dataFileBufferEnd - dataFileBufferPtr;
		int count2 = (countRemaining < bufferRemaining) ? countRemaining : bufferRemaining;

		memcpy(buf, dataFileBufferPtr, count2);
		dataFileBufferPtr += count2;
		buf += count2;
		rval += count2;

		// We arrived at this point without actually adding data in this iteration
		// Give and let the upper layer handle whatever data we have
		if(count2 == 0) break;

	};
	return rval;
}

bool  RawReader::getNextStep() {

	if(!indexIsTemp) {
		int r = fscanf(indexFile, "%llu\t%llu\t%llu\t%*llu\t%f\t%f\n", &stepBegin, &stepEnd, &stepFirstFrameID, &stepValue1, &stepValue2);
		if(r == 5)
			return true;
	}

	else {

		while(fscanf(indexFile, "%f\t", &stepValue1) < 1);
		while(fscanf(indexFile, "%f\t", &stepValue2) < 1);
		while(fscanf(indexFile, "%llu\t", &stepBegin) < 1);
		while(fscanf(indexFile, "%llu\t", &stepFirstFrameID) < 1);
		stepEnd = ULLONG_MAX;

		if(stepBegin < ULLONG_MAX)
			return true;
	}


	return false;
}

void  RawReader::getStepValue(float &step1, float &step2)
{
	step1 = stepValue1;
	step2 = stepValue2;

}

unsigned long long RawReader::getStepBegin() {
	return stepBegin;
}

unsigned long long RawReader::getStepEnd() {
	if(stepEnd < ULLONG_MAX)
		return stepEnd;

	if(!indexIsTemp)
		return stepEnd;

	unsigned long long readValue;
	int r = fscanf(indexFile, "%llu\n", &readValue);
	if(r == 1)
		stepEnd = readValue;

	return stepEnd;
}

void RawReader::processStep(bool verbose, EventSink<RawHit> *sink)
{
	auto pool = new ThreadPool<UndecodedHit>();
	auto mysink = new Decoder(this, sink);

	double t0 = 0;
	switch(tb) {
		case SYNC:	t0 = 0;
				break;
		case WALL:	t0 = daqSynchronizationEpoch;
				break;
		case STEP:	t0 = -double(stepFirstFrameID) * 1024;
				break;
		case USER:	t0 = -double(fileCreationDAQTime);
				break;
		default:
				t0 = 0;
	}

	mysink->pushT0(t0);
	
	RawDataFrame *dataFrame = new RawDataFrame;
	EventBuffer<UndecodedHit> *outBuffer = NULL; 
	size_t seqN = 0;
	long long currentBufferFirstFrame = 0;
	
	long long lastFrameID = -1;
	bool lastFrameWasLost0 = false;
	long long nFrames = 0;
	long long nFramesLost0 = 0;
	long long nFramesLostN = 0;
	long long nEventsNoLost = 0;
	long long nEventsSomeLost = 0;
	
	// Set file handle to start of step
	lseek(dataFile, getStepBegin(), SEEK_SET);
	off_t currentPosition = getStepBegin();
	// Reset file buffer pointers
	dataFileBufferPtr = dataFileBuffer;
	dataFileBufferEnd = dataFileBuffer;
	while (currentPosition < getStepEnd()) {
		int r;
		// Read frame header
		r = readFromDataFile((char*)((dataFrame->data)+0), 2*sizeof(uint64_t));
		assert(r == 2*sizeof(uint64_t));
		currentPosition += r;
		
		int N = dataFrame->getNEvents();
		assert((N+2) <= MaxRawDataFrameSize);

		long long frameID = dataFrame->getFrameID();
		if(lastFrameID == -1) lastFrameID = frameID - 1;
		bool frameLost = dataFrame->getFrameLost();

		// Account skipped frames
		if (frameID != lastFrameID + 1) {
			int skippedFrames = (frameID - lastFrameID) - 1;

			// We have skipped frames...
			nFrames += skippedFrames;

			if(lastFrameWasLost0) {
				// ... and they indicate lost frames
				nFramesLost0 += skippedFrames;
			}
		}

		// Increament frame counter
		nFrames += 1;

		// Account frames with lost data
		if(frameLost && (N == 0)) nFramesLost0 += 1;
		if(frameLost && (N != 0)) nFramesLostN += 1;
		
		if(frameLost) 
			nEventsSomeLost += N;
		else
			nEventsNoLost += N;
		
		// Keep track of frame with all event lost
		lastFrameWasLost0 = (frameLost && (N == 0));
		lastFrameID = frameID;

		if(N == 0) continue;

		r = readFromDataFile((char*)((dataFrame->data)+2), N*sizeof(uint64_t));
		assert(r == N*sizeof(uint64_t));
		currentPosition += r;

		// Blocksize
		// Best block size from profiling: 2048
		// but handle larger frames correctly
		size_t allocSize = max(N, 2048);
		if(outBuffer == NULL) {
			currentBufferFirstFrame = dataFrame->getFrameID();
			outBuffer = new EventBuffer<UndecodedHit>(allocSize, seqN, currentBufferFirstFrame * 1024);
			seqN += 1;
		}
		else if((outBuffer->getFree() < N) || ((frameID - currentBufferFirstFrame) > (1LL << 32))) {
			// Buffer is full or buffer is covering too much time
			pool->queueTask(outBuffer, mysink);
			currentBufferFirstFrame = dataFrame->getFrameID();
			outBuffer = new EventBuffer<UndecodedHit>(allocSize, seqN, currentBufferFirstFrame * 1024);
			seqN += 1;
		}

		UndecodedHit *p = outBuffer->getPtr() + outBuffer->getUsed();
		for(int i = 0; i < N; i++) {
			p[i].frameID = frameID - currentBufferFirstFrame;
			p[i].eventWord = dataFrame->data[2+i];
		}
		outBuffer->setUsed(outBuffer->getUsed() + N);
		outBuffer->setTMax((frameID + 1) * 1024);
	}
	
	if(outBuffer != NULL) {
		pool->queueTask(outBuffer, mysink);
		outBuffer = NULL;
	}
	
	pool->completeQueue();
	delete pool;
	
	mysink->finish();
	if(verbose) {
		fprintf(stderr, "RawReader report\n");
		fprintf(stderr, "step values: %f %f\n", stepValue1, stepValue2);
		fprintf(stderr, " data frames\n");
		fprintf(stderr, " %10lld total\n", nFrames);
		fprintf(stderr, " %10lld (%4.1f%%) were missing all data\n", nFramesLost0, 100.0 * nFramesLost0 / (nFrames));
		fprintf(stderr, " %10lld (%4.1f%%) were missing some data\n", nFramesLostN, 100.0 * nFramesLostN / (nFrames));
		fprintf(stderr, " events\n");
		fprintf(stderr, " %10lld total\n", nEventsNoLost + nEventsSomeLost);
		long long goodFrames = nFrames - nFramesLost0 - nFramesLostN;
		fprintf(stderr, " %10.1f events per frame avergage\n", 1.0 * nEventsNoLost / goodFrames);
		sink->report();
	}

	delete dataFrame;
	delete sink;
	
}

RawReader::Decoder::Decoder(RawReader *reader, EventSink<RawHit> *sink) : 
	UnorderedEventHandler<RawReader::UndecodedHit, RawHit>(sink), reader(reader)
{
}


EventBuffer<RawHit> * RawReader::Decoder::handleEvents(EventBuffer<RawReader::UndecodedHit > *inBuffer)
{
	unsigned N =  inBuffer->getSize();
	EventBuffer<RawHit> *outBuffer = new EventBuffer<RawHit>(N, inBuffer);

	UndecodedHit *pi = inBuffer->getPtr();
	UndecodedHit *pe = pi + N;
	RawHit *po = outBuffer->getPtr();
	for(; pi < pe; pi++, po++) {
		RawEventWord e = RawEventWord(pi->eventWord);
		po->channelID = e.getChannelID();
		po->qdcMode = reader->isQDC(po->channelID);
		po->tacID = e.getTacID();
		po->frameID = pi->frameID;
		po->tcoarse = e.getTCoarse();
		po->tfine = e.getTFine();
		po->ecoarse = e.getECoarse();
		po->efine = e.getEFine();

		po->time = pi->frameID * 1024 + po->tcoarse;
		po->timeEnd = pi->frameID * 1024 + po->ecoarse;
		if((po->timeEnd - po->time) < -256) po->timeEnd += 1024;
		po->valid = true;
	}
	outBuffer->setUsed(N);
	return outBuffer;

}

void RawReader::Decoder::report()
{
	UnorderedEventHandler<RawReader::UndecodedHit,RawHit>::report();
}
