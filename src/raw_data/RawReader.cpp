#include <shm_raw.hpp>
#include "RawReader.hpp"

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

using namespace std;
using namespace PETSYS;

RawReader::RawReader() :
        steps(vector<Step>()),
	dataFile(-1)

{
}

RawReader::~RawReader()
{
	close(dataFile);
}

RawReader *RawReader::openFile(const char *fnPrefix)
{
	char fName[1024];
	sprintf(fName, "%s.idxf", fnPrefix);
	FILE *idxFile = fopen(fName, "r");
	if(idxFile == NULL)  {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
                exit(1);
	}

	sprintf(fName, "%s.rawf", fnPrefix);
	int rawFile = open(fName, O_RDONLY);
	if(rawFile == -1) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
                exit(1);
	}

	uint64_t header[8];
	ssize_t r = read(rawFile, (void *)header, sizeof(uint64_t)*8);
	if(r < 1) {
		fprintf(stderr, "Could not read from '%s'\n", fName, strerror(errno));
		exit(1);
	}
	else if (r < sizeof(uint64_t)*8) {
		fprintf(stderr, "Read only %d bytes from '%s', expected %d\n", r, fName, sizeof(uint64_t)*8);
		exit(1);
	}

	RawReader *reader = new RawReader();
	reader->dataFile = rawFile;
	reader->frequency = header[0] & 0xFFFFFFFFUL;
	reader->qdcMode = (header[0] & 0x100000000UL) != 0;

	Step step;
	while(fscanf(idxFile, "%lu\t%lu\t%lld\t%lld\t%f\t%f", &step.stepBegin, &step.stepEnd, &step.stepFirstFrame, &step.stepLastFrame, &step.step1, &step.step2) == 6) {
		reader->steps.push_back(step);
	}
	return reader;
}

int RawReader::getNSteps()
{
	return steps.size();
}

double RawReader::getFrequency()
{
	return (double) frequency;
}

bool RawReader::isQDC()
{
	return qdcMode;
}

void RawReader::getStepValue(int n, float &step1, float &step2)
{
	Step step = steps[n];
	step1 = step.step1;
	step2 = step.step2;
}

void RawReader::processStep(int n, bool verbose, EventSink<RawHit> *sink)
{
	Step step = steps[n];
	
	sink->pushT0(0);
	
	RawDataFrame *dataFrame = new RawDataFrame;
	lseek(dataFile, step.stepBegin, SEEK_SET);
	EventBuffer<RawHit> *outBuffer = NULL; 
	const long outBlockSize = 4*1024;
	long long currentBufferFirstFrame = 0;
	
	off_t currentPosition = step.stepBegin;
	while (currentPosition < step.stepEnd) {
		int r;
		// Read frame header
		r = read(dataFile, (void*)((dataFrame->data)+0), 2*sizeof(uint64_t));
		assert(r == 2*sizeof(uint64_t));
		currentPosition += r;
		
		int N = dataFrame->getNEvents();
		r = read(dataFile, (void*)((dataFrame->data)+2), N*sizeof(uint64_t));
		assert(r == N*sizeof(uint64_t));
		currentPosition += r;
		
		readahead(dataFile, currentPosition, outBlockSize*sizeof(uint64_t));
		
		if(outBuffer == NULL) {
			currentBufferFirstFrame = dataFrame->getFrameID();
			outBuffer = new EventBuffer<RawHit>(outBlockSize, currentBufferFirstFrame * 1024);
			
		}
		else if(outBuffer->getSize() + N > outBlockSize) {
			sink->pushEvents(outBuffer);
			currentBufferFirstFrame = dataFrame->getFrameID();
			outBuffer = new EventBuffer<RawHit>(outBlockSize, currentBufferFirstFrame * 1024);
		}
		
		long long frameID = dataFrame->getFrameID();
		
		for(int i = 0; i < N; i++) {
			RawHit &e = outBuffer->getWriteSlot();
			
			e.channelID = dataFrame->getChannelID(i);
			e.tacID = dataFrame->getTacID(i);
			e.tcoarse = dataFrame->getTCoarse(i);
			e.tfine = dataFrame->getTFine(i);
			e.ecoarse = dataFrame->getECoarse(i);
			e.efine = dataFrame->getEFine(i);
			
			e.time = (frameID - currentBufferFirstFrame) * 1024 + e.tcoarse;
			e.timeEnd = (frameID - currentBufferFirstFrame) * 1024 + e.ecoarse;
			if((e.timeEnd - e.time) < -256) e.timeEnd += 1024;
			
			e.valid = true;
			
			outBuffer->pushWriteSlot();
		}
		outBuffer->setTMax((frameID + 1) * 1024);
	}
	
	if(outBuffer != NULL) {
		sink->pushEvents(outBuffer);
		outBuffer = NULL;
	}
	
	sink->finish();
	if(verbose) {
		fprintf(stderr, "RawReader report\n");
		sink->report();
	}
	delete sink;
	
}