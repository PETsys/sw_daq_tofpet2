#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <shm_raw.hpp>
using namespace PETSYS;

void displayUsage()
{
}

int main(int argc, char *argv[])
{
	char *inputFilePrefix = NULL;
	bool suppressEmpty = false;
	bool statsOnly = false;
	
	static struct option longOptions[] = {
                { "suppress-empty", no_argument, 0, 0 },
		{ "stats-only", no_argument, 0, 0 }
        };

	while(true) {
		int optionIndex = 0;
                int c = getopt_long(argc, argv, "i:",longOptions, &optionIndex);

		if(c == -1) break;
		else if(c != 0) {
			// Short arguments
			switch(c) {
			case 'i':	inputFilePrefix = optarg; break;
			default:	displayUsage(); exit(1);
			}
		}
		else if(c == 0) {
			switch(optionIndex) {
			case 0: 	suppressEmpty = true ; break;
			case 1:		statsOnly = true; break;
			default:	displayUsage(); exit(1);
			}
		}
		else {
			assert(false);
		}

	}	
	
	char fName[1024];
	// Open the data index file
	sprintf(fName, "%s.idxf", inputFilePrefix);
	FILE *indexFile = fopen(fName, "r");
	if(indexFile == NULL) {
			fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
			exit(1);
	}
	
	// Open the data file
	sprintf(fName, "%s.rawf", inputFilePrefix);
	FILE *dataFile = fopen(fName, "r");
	if(dataFile == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
		exit(1);
	}

	long startOffset, endOffset;
	float step1, step2;
	RawDataFrame *tmpRawDataFrame = new RawDataFrame;
	while(fscanf(indexFile, "%ld %ld %*lld %*lld %f %f\n", &startOffset, &endOffset, &step1, &step2) == 4) {
		fseek(dataFile, startOffset, SEEK_SET);
		
		bool firstFrame = true;
		long long unsigned lastFrameID = 0;
		bool lastFrameLost = false;
		
		unsigned long long sumDataFrames = 0;
		unsigned long long sumDataFramesLost = 0;
		unsigned long long sumEvents = 0;
		
		while(ftell(dataFile) < endOffset) {
			fread((void *)(tmpRawDataFrame->data), sizeof(uint64_t), 2, dataFile);
			auto frameSize = tmpRawDataFrame->getFrameSize();
			fread((void *)((tmpRawDataFrame->data)+2), sizeof(uint64_t), frameSize-2, dataFile);
			
			auto frameID = tmpRawDataFrame->getFrameID();
			auto frameLost = tmpRawDataFrame->getFrameLost();
			auto nEvents = tmpRawDataFrame->getNEvents();

			/*
			 * Sequences of frames with zero events are suppressed after the first frame in the sequence
			 * These can be either good frames or discarded frames
			 * They are not mixed
			 */
			int framesCompacted = firstFrame ? 0 : frameID - lastFrameID - 1;
			sumDataFrames += framesCompacted;
			sumDataFramesLost += lastFrameLost ? framesCompacted : 0;
			lastFrameID = frameID;
			lastFrameLost = frameLost;
			
			sumDataFrames += 1;
			sumDataFramesLost += frameLost ? 1 : 0;
			sumEvents += nEvents;

			if(statsOnly) continue;
			if(suppressEmpty && nEvents == 0) continue;
			
			
			printf("%04d %016lx Size: %-4u FrameID: %-20llu\n", 0, tmpRawDataFrame->data[0], tmpRawDataFrame->getFrameSize(), frameID);
			printf("%04d %016lx nEvents: %20d %4s\n", 1,  tmpRawDataFrame->data[1], tmpRawDataFrame->getNEvents(), frameLost ? "LOST" : "");
			
			for (int i = 0; i < nEvents; i++) {

				auto channelID = tmpRawDataFrame->getChannelID(i);
				auto tacID = tmpRawDataFrame->getTacID(i);
				auto tCoarse = tmpRawDataFrame->getTCoarse(i);
				auto eCoarse = tmpRawDataFrame->getECoarse(i);
				auto tFine = tmpRawDataFrame->getTFine(i);
				auto eFine = tmpRawDataFrame->getEFine(i);
				
				printf("%04d %016lx", i+2,  tmpRawDataFrame->data[i+2]);
				printf(" ChannelID: (%02u %02u %02u %02u)", (channelID >> 17) % 32, (channelID >> 12) % 32, (channelID >> 6) % 64, (channelID % 64));
				printf(" TacID: %u TCoarse: %4u TFine: %4u ECoarse: %4u EFine: %4u", tacID, tCoarse, tFine, eCoarse, eFine);
				printf("\n");
			}
			
			
		}
		printf("STAT %llu %llu %llu\n", sumDataFrames, sumDataFramesLost, sumEvents);
	}
	delete tmpRawDataFrame;
	
	return 0;
}
