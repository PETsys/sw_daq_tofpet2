#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <set>
#include <map>
#include <shm_raw.hpp>
#include <boost/python.hpp>
#include <boost/lexical_cast.hpp>
#include "Monitor.hpp"
#include "SingleValue.hpp"
#include "Histogram1D.hpp"
#include <Event.hpp>
#include <SystemConfig.hpp>
#include <OrderedEventHandler.hpp>
#include <CoarseSorter.hpp>
#include <ProcessHit.hpp>
#include <SimpleGrouper.hpp>
#include <CoincidenceGrouper.hpp>
#include <DataFileWriter.hpp>
#include <ThreadPool.hpp>
#include <boost/regex.hpp>
#include <string>
#include <iostream>
#include <TFile.h>
#include <TTree.h>

using namespace std;
using namespace PETSYS;
using namespace PETSYS::OnlineMonitor;

static const unsigned MAX_NUMBER_CHANNELS = 4194304;

enum FrameType { FRAME_TYPE_UNKNOWN, FRAME_TYPE_SOME_DATA, FRAME_TYPE_ZERO_DATA, FRAME_TYPE_SOME_LOST, FRAME_TYPE_ALL_LOST };

enum timeref_t {SYNC, WALL, STEP, USER};

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

class OnlineEventStream : public EventStream {
public:
	OnlineEventStream(double f, int tID) : frequency(f), triggerID(tID) { } ;
	double getFrequency() { return frequency; };
	int getTriggerID() { return triggerID; };
	bool isQDC(unsigned int gChannelID){ return qdcMode[gChannelID]; };
	void setMode(unsigned int gChannelID, bool mode){ qdcMode[gChannelID] = mode; };
private:
	double frequency;
	int triggerID;
	bool qdcMode[MAX_NUMBER_CHANNELS];
};

/* 
 * Duplicate code from RawReader
 * Needs reforming, probably once we add support for QDC/ToT mixed mode acquisitions
 */

struct UndecodedHit {
		u_int64_t frameID;
		u_int64_t eventWord;
	};

class Decoder : public UnorderedEventHandler<UndecodedHit, RawHit> {

public:
	Decoder(OnlineEventStream *stream, EventSink<RawHit> *sink) : UnorderedEventHandler<UndecodedHit, RawHit>(sink), stream(stream)
	{
	};
	~Decoder()  
	{
	};
	void report()
	{
		UnorderedEventHandler<UndecodedHit,RawHit>::report();
	};
	void resetCounters()
	{
		UnorderedEventHandler<UndecodedHit,RawHit>::resetCounters();
	};
protected:
	virtual EventBuffer<RawHit> * handleEvents (EventBuffer<UndecodedHit> *inBuffer)
	{
		unsigned N =  inBuffer->getSize();
		EventBuffer<RawHit> *outBuffer = new EventBuffer<RawHit>(N, inBuffer);

		UndecodedHit *pi = inBuffer->getPtr();
		UndecodedHit *pe = pi + N;
		RawHit *po = outBuffer->getPtr();
		for(; pi < pe; pi++, po++) {
			RawEventWord e = RawEventWord(pi->eventWord);
			po->channelID = e.getChannelID();
			po->qdcMode = stream->isQDC(po->channelID);
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
	OnlineEventStream *stream;
};


Decoder *createProcessingPipeline(EVENT_TYPE eventType, OnlineEventStream *eventStream, SystemConfig *config, DataFileWriter *dataFileWriter){
	Decoder *pipeline;
	if(eventType == RAW){
		pipeline = new Decoder(eventStream, 
			new WriteRawHelper(dataFileWriter,
			new NullSink<RawHit>()
			));
	}
	else if(eventType == SINGLE){
		pipeline = new Decoder(eventStream, 
			new CoarseSorter(
			new ProcessHit(config, eventStream,
			new WriteSinglesHelper(dataFileWriter, 
			new NullSink<Hit>()
			))));
	}
	else if(eventType == GROUP){
		pipeline = new Decoder(eventStream, 
			new CoarseSorter(
			new ProcessHit(config, eventStream,
			new SimpleGrouper(config,		
			new WriteGroupsHelper(dataFileWriter, 
			new NullSink<GammaPhoton>()
			)))));
	}
	else if(eventType == COINCIDENCE){
		pipeline = new Decoder(eventStream, 
			new CoarseSorter(
			new ProcessHit(config, eventStream,
			new SimpleGrouper(config,
			new CoincidenceGrouper(config,
			new WriteCoincidencesHelper(dataFileWriter, 
			new NullSink<Coincidence>()
			))))));
	}
	return pipeline;
}

struct BlockHeader  {
	float step1;
	float step2;	
	uint32_t wrPointer;
	uint32_t rdPointer;
	int32_t blockType;
};


int main(int argc, char *argv[])
{
	assert(argc == 16);
	long systemFrequency = boost::lexical_cast<long>(argv[1]);
	char *fileNamePrefix = argv[2];
	char *eType = argv[3];
	char *fType = argv[4];
	char *mode = argv[5];
	char *configFileName = argv[6];
	char *shmObjectPath = argv[7];
	int triggerID = boost::lexical_cast<int>(argv[8]);
	double daqSynchronizationEpoch = boost::lexical_cast<double>(argv[9]);
	unsigned long long fileCreationDAQTime = boost::lexical_cast<unsigned long long>(argv[10]);
	int eventFractionToWrite = round(1024*boost::lexical_cast<float>(argv[11])/ 100.0);
	int hitLimitToWrite = boost::lexical_cast<int>(argv[12]);
	double fileSplitTime = boost::lexical_cast<double>(argv[13]);
	char *tref = argv[14];
	bool verbose = (argv[15][0] == 'T');
	bool useAsyncWriting = false;
	
	EVENT_TYPE eventType; 
	if(strcmp(eType, "raw") == 0){
		eventType = RAW;
	}
	else if(strcmp(eType, "singles") == 0){
		eventType = SINGLE;
	}	
	else if(strcmp(eType, "groups") == 0){
		eventType = GROUP;
	}
	else eventType = COINCIDENCE;
	       
	
	FILE_TYPE fileType; 
	if(strcmp(fType, "binary") == 0){
		fileType = FILE_BINARY;
		useAsyncWriting = true;
	}
	if(strcmp(fType, "binaryCompact") == 0){
		fileType = FILE_BINARY_COMPACT;
		useAsyncWriting = true;
	}	
	else if(strcmp(fType, "text") == 0){
		fileType = FILE_TEXT;
	}
	else if(strcmp(fType, "textCompact") == 0){
		fileType = FILE_TEXT_COMPACT;
	}
	else if(strcmp(fType, "root") == 0){
		fileType = FILE_ROOT;
	}

		
	timeref_t tb; 
	if(strcmp(tref, "sync") == 0){
		tb = SYNC;
	}
	else if(strcmp(tref, "wall") == 0){
		tb = WALL;
	}
	else if(strcmp(tref, "step") == 0){
		tb = STEP;
	}
	else if(strcmp(tref, "user") == 0){
		tb = USER;
	}

	if(eventType == RAW && fileType != FILE_TEXT && fileType != FILE_ROOT){
		fprintf(stderr, "ERROR: Raw output type can only be written to text or ROOT output format\n");
		exit(1);
	}

	if(eventType == SINGLE && (fileType == FILE_BINARY_COMPACT || fileType == FILE_TEXT_COMPACT)){
		fprintf(stderr, "ERROR: Singles output type can only be written to text, binary or ROOT output formats.\n");
		exit(1);
	}
	bool totMode = (strcmp(mode, "tot") == 0);
       	
	// If data was taken in full ToT mode, do not attempt to load these files
	unsigned long long mask = SystemConfig::LOAD_ALL;	
	if(totMode){ 
		mask ^= (SystemConfig::LOAD_QDC_CALIBRATION | SystemConfig::LOAD_ENERGY_CALIBRATION);
	}
	SystemConfig *config = SystemConfig::fromFile(configFileName, mask);	
	
	PETSYS::SHM_RAW *shm = new PETSYS::SHM_RAW(shmObjectPath);
	bool firstBlock = true;
	
	BlockHeader blockHeader;
	
    long long stepEvents = 0;
	long long stepMaxFrame = 0;
	long long stepAllFrames = 0;
	long long stepLostFramesN = 0;
	long long stepLostFrames0 = 0;

	long long minFrameID = 0x7FFFFFFFFFFFFFFFLL, maxFrameID = 0, lastMaxFrameID = 0;
	
	long long lastFrameID = -1;
	long long stepFirstFrameID = -1;

	FrameType lastFrameType = FRAME_TYPE_UNKNOWN;

	ThreadPool<UndecodedHit> *pool = new ThreadPool<UndecodedHit>();
	OnlineEventStream *eventStream = new OnlineEventStream(systemFrequency, triggerID);
	
	// If acquisition mode is mixed, read ".modf" file to assign channel energy mode 
	if(strcmp(mode, "mixed") == 0){
		char fName[1024];
		sprintf(fName, "%s.modf", fileNamePrefix);
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
			char m[128];		
			if(sscanf(line, "%d\t%u\t%u\t%u\t%s", &portID, &slaveID, &chipID, &channelID, m)!= 5) continue;
			unsigned long gChannelID = 0;
			gChannelID |= channelID;
			gChannelID |= (chipID << 6);
			gChannelID |= (slaveID << 12);
			gChannelID |= (portID << 17);
			eventStream->setMode(gChannelID, strcmp(m, "qdc") == 0);
		}
	}
	else{
		for(unsigned long gChannelID = 0; gChannelID < MAX_NUMBER_CHANNELS; gChannelID++)
			eventStream->setMode(gChannelID, strcmp(mode, "qdc") == 0);
	}

	char outputFileName[1024];
	
	DataFileWriter *dataFileWriter = new DataFileWriter(fileNamePrefix, useAsyncWriting, eventStream->getFrequency(), eventType, fileType, 0, hitLimitToWrite, eventFractionToWrite, fileSplitTime);	

	Decoder *pipeline = createProcessingPipeline(eventType, eventStream, config, dataFileWriter);

	pipeline->pushT0(0.0);

	bool isReadyToAcquire = true; 
	fwrite(&isReadyToAcquire, sizeof(bool), 1, stdout);
	//fprintf(stderr, "pos1\n"); 
	fflush(stdout);
	sleep(0.01);
	//fprintf(stderr, "pos2\n");	
	EventBuffer<UndecodedHit> *outBuffer = NULL; 
	size_t seqN = 0;
	long long currentBufferFirstFrame = 0;	

	while(fread(&blockHeader, sizeof(blockHeader), 1, stdin) == 1){
		dataFileWriter->setStepValues(blockHeader.step1, blockHeader.step2);

		unsigned bs = shm->getSizeInFrames();
		unsigned rdPointer = blockHeader.rdPointer % (2*bs);
		unsigned wrPointer = blockHeader.wrPointer % (2*bs);
		//fprintf(stderr, "d\t%d\n", rdPointer, wrPointer);

		if(blockHeader.blockType == 0) {
			// First block in a step
			unsigned index = rdPointer % bs;

			stepAllFrames = 0;
			stepEvents = 0;
			stepMaxFrame = 0;
			stepLostFramesN = 0;
			stepLostFrames0 = 0;

			stepFirstFrameID = shm->getFrameID(index);
			lastFrameID = stepFirstFrameID - 1;
			lastFrameType = FRAME_TYPE_UNKNOWN;
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
		
			pipeline->pushT0(t0);

			//r = fprintf(tempFile, "%f\t%f\t%ld\t%ld\t", blockHeader.step1, blockHeader.step2, stepStartOffset, stepFirstFrameID);
			//if(r < 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
			//r = fflush(tempFile);
			//if(r != 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
		}
		


		while(rdPointer != wrPointer) {
			unsigned index = rdPointer % bs;
			
			long long frameID = shm->getFrameID(index);
			if(stepFirstFrameID == -1) stepFirstFrameID = frameID;
			if(frameID <= lastFrameID && verbose==true) {
				fprintf(stderr, "WARNING!! Frame ID reversal: %12lld -> %12lld | %04u %04u %04u\n", 
					lastFrameID, frameID, 
					blockHeader.wrPointer, blockHeader.rdPointer, rdPointer
					);
				
			}
			else if((lastFrameID >= 0) && (frameID != (lastFrameID + 1))) {
				// We have skipped one or more frame ID, so 
				// we account them as lost...
				long long skippedFrames = (frameID - lastFrameID) - 1;
				stepAllFrames += skippedFrames;
				stepLostFrames0 += skippedFrames;				
				// .. and we set the lastFrameType
				lastFrameType = FRAME_TYPE_ALL_LOST;
			}

			lastFrameID = frameID;
			minFrameID = minFrameID < frameID ? minFrameID : frameID;
			maxFrameID = maxFrameID > frameID ? maxFrameID : frameID;

			// Get the pointer to the raw data frame
			PETSYS::RawDataFrame *dataFrame = shm->getRawDataFrame(index);
			// Increase the circular buffer pointer
			rdPointer = (rdPointer+1) % (2*bs);
			
			int frameSize = shm->getFrameSize(index);
			int nEvents = shm->getNEvents(index);
			bool frameLost = shm->getFrameLost(index);
			
			stepEvents += nEvents;
			stepMaxFrame = stepMaxFrame > nEvents ? stepMaxFrame : nEvents;
			if(frameLost){
				if(nEvents == 0)
					stepLostFrames0 += 1;
				else
					stepLostFramesN += 1;
			}			
			stepAllFrames += 1;
			FrameType frameType = FRAME_TYPE_UNKNOWN;
			if(frameLost) {
				frameType = (nEvents == 0) ? FRAME_TYPE_ZERO_DATA : FRAME_TYPE_SOME_DATA;
			}
			else {
				frameType = (nEvents == 0) ? FRAME_TYPE_ALL_LOST : FRAME_TYPE_SOME_LOST;
			}

			// Do not write sequences of normal empty frames, unless we're closing a step
			if(blockHeader.blockType == 1 && lastFrameType == FRAME_TYPE_ZERO_DATA && frameType == lastFrameType) {
				continue;
			}

			// Do not write sequences of all lost frames, unless we're closing a step
			if(blockHeader.blockType == 1 && lastFrameType == FRAME_TYPE_ALL_LOST && frameType == lastFrameType) {
				continue;
			}
			lastFrameType = frameType;

			// Blocksize
			// Best block size from profiling: 2048
			// but handle larger frames correctly
			size_t allocSize = max(nEvents, 4096);
		
			if(outBuffer == NULL) {
				//fprintf(stderr,"Allocating first: %d %d %u %u %u %u\n",outBuffer->getFree(), nEvents,bs,rdPointer, wrPointer, index);
				currentBufferFirstFrame = dataFrame->getFrameID();
				outBuffer = new EventBuffer<UndecodedHit>(allocSize, seqN, currentBufferFirstFrame * 1024);
				seqN += 1;
			}
			else if((outBuffer->getFree() < nEvents) || ((frameID - currentBufferFirstFrame) > (1LL << 32))) {
				// Buffer is full or buffer is covering too much time
				//fprintf(stderr,"Allocating new: %d %d %u %u %u %u\n",outBuffer->getFree(), nEvents,bs,rdPointer, wrPointer, index);
				pool->queueTask(outBuffer, pipeline);
				currentBufferFirstFrame = dataFrame->getFrameID();
				outBuffer = new EventBuffer<UndecodedHit>(allocSize, seqN, currentBufferFirstFrame * 1024);
				seqN += 1;
			}
			//else{fprintf(stderr,"%d %d %u %u %u %u\n",outBuffer->getFree(), nEvents,bs,rdPointer, wrPointer, index);}
			
			UndecodedHit *p = outBuffer->getPtr() + outBuffer->getUsed();
			for(int i = 0; i < nEvents; i++) {
				p[i].frameID = frameID - currentBufferFirstFrame;
				p[i].eventWord = dataFrame->data[2+i];
			}
			outBuffer->setUsed(outBuffer->getUsed() + nEvents);
			outBuffer->setTMax((frameID + 1) * 1024);        
		}		
		if(outBuffer != NULL) {
			pool->queueTask(outBuffer, pipeline);
			outBuffer = NULL;
		}
		pool->completeQueue();
		
		if(blockHeader.blockType == 2){
			if(verbose == true){
				fprintf(stderr, "onlineProcessing:: Step had %lld frames with %lld events; %f events/frame avg, %lld event/frame max\n", 
					stepAllFrames, stepEvents, 
					float(stepEvents)/stepAllFrames,
					stepMaxFrame); fflush(stderr);
				fprintf(stderr, "onlineProcessing:: some events were lost for %lld (%5.1f%%) frames; all events were lost for %lld (%5.1f%%) frames\n", 
					stepLostFramesN, 100.0 * stepLostFramesN / stepAllFrames,
					stepLostFrames0, 100.0 * stepLostFrames0 / stepAllFrames
					); 
			
			pipeline->report();
			}
			fflush(stderr);

			pipeline->resetCounters();
			dataFileWriter->closeStep();
			
			
		}
		
		fwrite(&rdPointer, sizeof(uint32_t), 1, stdout);
		//long long dummy = 0;
		fwrite(&stepAllFrames, sizeof(long long), 1, stdout);
		fwrite(&stepLostFrames0, sizeof(long long), 1, stdout);
		fwrite(&stepEvents, sizeof(long long), 1, stdout);

		fflush(stdout);

	}

	delete pool;		
	
	return 0;
}

