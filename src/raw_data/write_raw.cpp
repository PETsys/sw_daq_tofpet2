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
#include <shm_raw.hpp>
#include <boost/lexical_cast.hpp>

using namespace std;

struct CalibrationData{
	uint64_t eventWord;
	int freq;
};


struct BlockHeader  {
	float step1;
	float step2;	
	uint32_t wrPointer;
	uint32_t rdPointer;
	int32_t endOfStep;
};


enum FrameType { FRAME_TYPE_UNKNOWN, FRAME_TYPE_SOME_DATA, FRAME_TYPE_ZERO_DATA, FRAME_TYPE_SOME_LOST, FRAME_TYPE_ALL_LOST };

int main(int argc, char *argv[])
{
	assert(argc == 8);
	char *shmObjectPath = argv[1];
	char *outputFilePrefix = argv[2];
	long systemFrequency = boost::lexical_cast<long>(argv[3]);
	bool qdcMode = (strcmp(argv[4], "qdc") == 0);
	double acquisitionStartTime = boost::lexical_cast<double>(argv[5]);
	bool acqStdMode = (argv[6][0] == 'N');
	int triggerID = boost::lexical_cast<int>(argv[7]);

	PETSYS::SHM_RAW *shm = new PETSYS::SHM_RAW(shmObjectPath);
	  
	char fNameRaw[1024];
	char fNameIdx[1024];


	if(strcmp(outputFilePrefix, "/dev/null") == 0) {
		sprintf(fNameRaw, "%s", outputFilePrefix);
		sprintf(fNameIdx, "%s", outputFilePrefix);
	}
	else {
		sprintf(fNameRaw, "%s.rawf", outputFilePrefix);
		sprintf(fNameIdx, "%s.idxf", outputFilePrefix);
	}
	
	FILE * dataFile = fopen(fNameRaw, "wb");
	assert(dataFile != NULL);
	if(dataFile == NULL) {
		fprintf(stderr, "Could not open '%s' for writing: %s\n", fNameRaw, strerror(errno));
		return 1;
	}

	FILE * indexFile = fopen(fNameIdx, "wb");
	if(indexFile == NULL) {
		fprintf(stderr, "Could not open '%s' for writing: %s\n", fNameIdx, strerror(errno));
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
	memcpy(header+1, &acquisitionStartTime, sizeof(double));
	if (triggerID != -1) { header[2] = 0x8000 + triggerID; }
	if (strcmp(argv[4], "mixed") == 0) { header[3] = 0x1UL; }
	int r = fwrite((void *)&header, sizeof(uint64_t), 8, dataFile);
	if(r != 8) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }

	
	multiset<uint64_t> calEventSet;  
	CalibrationData calData;

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
	FrameType lastFrameType = FRAME_TYPE_UNKNOWN;
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
				// we account them as lost...
				long long skippedFrames = (frameID - lastFrameID) - 1;
				stepAllFrames += skippedFrames;
				stepLostFrames0 += skippedFrames;

				// ...and we write the first frameID of the lost batch to the datafile...
				uint64_t lostFrameBuffer[2];
				lostFrameBuffer[0] = (2ULL << 36) | (lastFrameID + 1);
				lostFrameBuffer[1] = 1ULL << 16;
				if(acqStdMode) {
					int r = fwrite((void*)lostFrameBuffer, sizeof(uint64_t), 2, dataFile);
					if(r != 2) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
				}
				
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
			
			// If acquiring calibration data, insert events into multiset for data compression
			if(!acqStdMode){
				for(int i = 0 ; i < nEvents ; i++){
					uint64_t event = shm->getFrameWord(index, i+2);
					calEventSet.insert(event);
				}
			}
		
			// Accounting
			stepEvents += nEvents;
			stepMaxFrame = stepMaxFrame > nEvents ? stepMaxFrame : nEvents;
			if(frameLost) {
				if(nEvents == 0)
					stepLostFrames0 += 1;
				else
					stepLostFramesN += 1;
			}			
			stepAllFrames += 1;

			// Determine this frame type
			FrameType frameType = FRAME_TYPE_UNKNOWN;
			if(frameLost) {
				frameType = (nEvents == 0) ? FRAME_TYPE_ZERO_DATA : FRAME_TYPE_SOME_DATA;
			}
			else {
				frameType = (nEvents == 0) ? FRAME_TYPE_ALL_LOST : FRAME_TYPE_SOME_LOST;
			}

			// Do not write sequences of normal empty frames, unless we're closing a step
			if(blockHeader.endOfStep == 0 && lastFrameType == FRAME_TYPE_ZERO_DATA && frameType == lastFrameType) {
				continue;
			}

			// Do not write sequences of all lost frames, unless we're closing a step
			if(blockHeader.endOfStep == 0 && lastFrameType == FRAME_TYPE_ALL_LOST && frameType == lastFrameType) {
				continue;
			}
			lastFrameType = frameType;
			
			// Write out the data frame contents
			if(acqStdMode){
				r = fwrite((void *)(dataFrame->data), sizeof(uint64_t), frameSize, dataFile);
				if(r != frameSize) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
			}
	        
		}		
		
		if(blockHeader.endOfStep != 0) {
			// If acquiring calibration data, at the end of each calibration step, write compressed data to disk 
			if(!acqStdMode){
				multiset<uint64_t>::iterator eventIt = calEventSet.begin();
		        
				while(eventIt != calEventSet.end()){
					calData.freq = calEventSet.count(*eventIt);
					calData.eventWord = *eventIt;
					r = fwrite(&calData, sizeof(CalibrationData), 1, dataFile);
					if(r != 1) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
					r = fflush(dataFile);
					if(r != 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
					eventIt = calEventSet.upper_bound(*eventIt);
				}
				calEventSet.clear();
			}	
			
			fprintf(stderr, "writeRaw:: Step had %lld frames with %lld events; %f events/frame avg, %lld event/frame max\n", 
					stepAllFrames, stepEvents, 
					float(stepEvents)/stepAllFrames,
					stepMaxFrame); fflush(stderr);
			fprintf(stderr, "writeRaw:: some events were lost for %lld (%5.1f%%) frames; all events were lost for %lld (%5.1f%%) frames\n", 
					stepLostFramesN, 100.0 * stepLostFramesN / stepAllFrames,
					stepLostFrames0, 100.0 * stepLostFrames0 / stepAllFrames
					); 
			fflush(stderr);
			
			int r = fflush(dataFile);
			if(r != 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
			r = fprintf(indexFile, "%ld\t%ld\t%lld\t%lld\t%f\t%f\n", stepStartOffset, ftell(dataFile), stepFirstFrameID, lastFrameID, blockHeader.step1, blockHeader.step2);
			if(r < 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
			r = fflush(indexFile);
			if(r != 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
			stepStartOffset = ftell(dataFile);

			stepAllFrames = 0;
			stepEvents = 0;
			stepMaxFrame = 0;
			stepLostFramesN = 0;
			stepLostFrames0 = 0;
			lastFrameID = -1;
			stepFirstFrameID = -1;
			lastFrameType = FRAME_TYPE_UNKNOWN;
		}
		
		fwrite(&rdPointer, sizeof(uint32_t), 1, stdout);
		fflush(stdout);
	}

	fclose(dataFile);
	return 0;
}

