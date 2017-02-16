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

#include <shm_raw.hpp>
#include <boost/lexical_cast.hpp>

using namespace std;


struct BlockHeader  {
	float step1;
	float step2;	
	uint32_t wrPointer;
	uint32_t rdPointer;
	int32_t endOfStep;
};

int main(int argc, char *argv[])
{
	assert(argc == 5);
	char *shmObjectPath = argv[1];
	char *outputFilePrefix = argv[2];
	long systemFrequency = boost::lexical_cast<long>(argv[3]);
	bool qdcMode = (argv[4][0] == 'Q');
	

	PETSYS::SHM_RAW *shm = new PETSYS::SHM_RAW(shmObjectPath);

	char fName[1024];
	sprintf(fName, "%s.rawf", outputFilePrefix);
	FILE * dataFile = fopen(fName, "wb");
	assert(dataFile != NULL);
	if(dataFile == NULL) {
		fprintf(stderr, "Could not open '%s' for writing: %s\n", fName, strerror(errno));
		return 1;
	}

	sprintf(fName, "%s.idxf", outputFilePrefix);
	FILE * indexFile = fopen(fName, "wb");
	if(indexFile == NULL) {
		fprintf(stderr, "Could not open '%s' for writing: %s\n", fName, strerror(errno));
		return 1;
	}
	fprintf(stderr, "INFO: Writing data to '%s.rawf' and index to '%s.idxf'\n", outputFilePrefix, outputFilePrefix);
	
	// Write a 64 byte header
	// For now, all is zero
	uint64_t header[8];
	for(int i = 0; i < 8; i++)
		header[i] = 0;
	header[0] |= uint32_t(systemFrequency);
	header[0] |= (qdcMode ? 0x1UL : 0x0UL) << 32;
	fwrite((void *)&header, sizeof(uint64_t), 8, dataFile);
	

	bool firstBlock = true;
	float step1;
	float step2;
	BlockHeader blockHeader;
	
	long long stepEvents = 0;
	long long stepMaxFrame = 0;
	long long stepAllFrames = 0;
	long long stepLostFramesN = 0;
	long long stepLostFrames0 = 0;
	
	long long minFrameID = 0x7FFFFFFFFFFFFFFFLL, maxFrameID = 0, lastMaxFrameID = 0;
	
	long long lastFrameID = -1;
	long long stepFirstFrameID = -1;
	
	long stepStartOffset = ftell(dataFile);
	while(fread(&blockHeader, sizeof(blockHeader), 1, stdin) == 1) {

		step1 = blockHeader.step1;
		step2 = blockHeader.step2;
		
		unsigned bs = shm->getSizeInFrames();
		unsigned rdPointer = blockHeader.rdPointer % (2*bs);
		unsigned wrPointer = blockHeader.wrPointer % (2*bs);
		while(rdPointer != wrPointer) {
			unsigned index = rdPointer % bs;
			
			long long frameID = shm->getFrameID(index);
			if(stepFirstFrameID == -1) stepFirstFrameID = frameID;
			if(frameID <= lastFrameID) {
				fprintf(stderr, "WARNING!! Frame ID reversal: %12lld -> %12lld | %04u %04u %04u\n", 
					lastFrameID, frameID, 
					blockHeader.wrPointer, blockHeader.rdPointer, rdPointer
					);
				
			}
			else if ((lastFrameID >= 0) && (frameID != (lastFrameID + 1))) {
				// We have skipped one or more frame ID, so 
				// we account them as lost
				long long skippedFrames = (frameID - lastFrameID) - 1;
				stepAllFrames += skippedFrames;
				stepLostFrames0 += skippedFrames;
			}

			lastFrameID = frameID;
			minFrameID = minFrameID < frameID ? minFrameID : frameID;
			maxFrameID = maxFrameID > frameID ? maxFrameID : frameID;
			
			// Simply dump the raw data frame
			int frameSize = shm->getFrameSize(index);
			PETSYS::RawDataFrame *dataFrame = shm->getRawDataFrame(index);
			fwrite((void *)(dataFrame->data), sizeof(uint64_t), frameSize, dataFile);

			int nEvents = shm->getNEvents(index);
			bool frameLost = shm->getFrameLost(index);

			stepEvents += nEvents;
			stepMaxFrame = stepMaxFrame > nEvents ? stepMaxFrame : nEvents;
			if(frameLost) {
				if(nEvents == 0)
					stepLostFrames0 += 1;
				else
					stepLostFramesN += 1;
			}			
			stepAllFrames += 1;
			
			rdPointer = (rdPointer+1) % (2*bs);
		}		
		
		if(blockHeader.endOfStep != 0) {

			
			fprintf(stderr, "writeRaw:: Step had %lld frames with %lld events; %f events/frame avg, %d event/frame max\n", 
					stepAllFrames, stepEvents, 
					float(stepEvents)/stepAllFrames,
					stepMaxFrame); fflush(stderr);
			fprintf(stderr, "writeRaw:: some events were lost for %d (%5.1f%%) frames; all events were lost for %d (%5.1f%%) frames\n", 
					stepLostFramesN, 100.0 * stepLostFramesN / stepAllFrames,
					stepLostFrames0, 100.0 * stepLostFrames0 / stepAllFrames
					); 
			fflush(stderr);
			
			fflush(dataFile);
			fprintf(indexFile, "%ld\t%ld\t%lld\t%lld\t%f\t%f\n", stepStartOffset, ftell(dataFile), stepFirstFrameID, lastFrameID, blockHeader.step1, blockHeader.step2);
			fflush(indexFile);
			stepStartOffset = ftell(dataFile);

			stepAllFrames = 0;
			stepEvents = 0;
			stepMaxFrame = 0;
			stepLostFramesN = 0;
			stepLostFrames0 = 0;
			lastFrameID = -1;
			stepFirstFrameID = -1;
		}

		fwrite(&rdPointer, sizeof(uint32_t), 1, stdout);
		fflush(stdout);
	}

	fclose(dataFile);
	return 0;
}

