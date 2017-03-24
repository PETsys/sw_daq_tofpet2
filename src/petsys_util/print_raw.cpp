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
	
	static struct option longOptions[] = {
                { "suppress-empty", no_argument, 0, 0 },
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
		while(ftell(dataFile) < endOffset) {
			fread((void *)(tmpRawDataFrame->data), sizeof(uint64_t), 2, dataFile);
			long frameSize = tmpRawDataFrame->getFrameSize();
			fread((void *)((tmpRawDataFrame->data)+2), sizeof(uint64_t), frameSize-2, dataFile);
			
			
			int nEvents = tmpRawDataFrame->getNEvents();
			if(suppressEmpty && nEvents == 0) continue;
			
			printf("%04d %016llx Size: %-4llu FrameID: %-20llu\n", 0, tmpRawDataFrame->data[0], tmpRawDataFrame->getFrameSize(), tmpRawDataFrame->getFrameID());
			printf("%04d %016llx nEvents: %20llu %4s\n", 1,  tmpRawDataFrame->data[1], tmpRawDataFrame->getNEvents(), tmpRawDataFrame->getFrameLost() ? "LOST" : "");
			
			for (int i = 0; i < nEvents; i++) {

				unsigned long channelID = tmpRawDataFrame->getChannelID(i);
				unsigned long tacID = tmpRawDataFrame->getTacID(i);
				unsigned long tCoarse = tmpRawDataFrame->getTCoarse(i);
				unsigned long eCoarse = tmpRawDataFrame->getECoarse(i);
				unsigned long tFine = tmpRawDataFrame->getTFine(i);
				unsigned long eFine = tmpRawDataFrame->getEFine(i);
				
				printf("%04d %016llx", i+2,  tmpRawDataFrame->data[i+2]);
				printf(" ChannelID: (%02d %02d %02d %02d)", (channelID >> 16) % 32, (channelID >> 11) % 32, (channelID >> 6) % 64, (channelID % 64));
				printf(" TacID: %d TCoarse: %4d TFine: %4d ECoarse: %4d EFine: %4d", tacID, tCoarse, tFine, eCoarse, eFine);
				printf("\n");
			}
			
		}
	}
	delete tmpRawDataFrame;
	
	return 0;
}