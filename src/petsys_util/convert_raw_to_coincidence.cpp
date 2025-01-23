#include <RawReader.hpp>
#include <OrderedEventHandler.hpp>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <SystemConfig.hpp>
#include <CoarseSorter.hpp>
#include <HwTriggerSimulator.hpp>
#include <ProcessHit.hpp>
#include <SimpleGrouper.hpp>
#include <CoincidenceGrouper.hpp>
#include <DataFileWriter.hpp>
#include <boost/lexical_cast.hpp>

#include <TFile.h>
#include <TNtuple.h>

using namespace PETSYS;


void displayHelp(char * program)
{
	fprintf(stderr, "Usage: %s --config <config_file> -i <input_file_prefix> -o <output_file_prefix> [optional arguments]\n", program);
	fprintf(stderr, "Arguments:\n");
	fprintf(stderr,  "  --config \t\t Configuration file containing path to tdc calibration table \n");
	fprintf(stderr,  "  -i \t\t\t Input file prefix - raw data\n");
	fprintf(stderr,  "  -o \t\t\t Output file name - by default in text data format\n");
	fprintf(stderr, "Optional flags:\n");
	fprintf(stderr,  "  --writeBinary \t Set the output data format to binary\n");
	fprintf(stderr,  "  --writeBinaryCompact \t Set the output data format to compact binary\n");
	fprintf(stderr,  "  --writeTextCompact \t Set the output data format to compact text \n");
	fprintf(stderr,  "  --writeRoot \t\t Set the output data format to ROOT (TTree)\n");
	fprintf(stderr,  "  --writeMultipleHits N  Writes multiple hits, up to the Nth hit\n");
	fprintf(stderr,  "  --writeFraction N \t Fraction of events to write, in percentage\n");
	fprintf(stderr,  "  --splitTime t \t Split output into different files every t seconds\n");
	fprintf(stderr,  "  --simulateHwTrigger \t\t Set the program to filter raw events as in hw trigger, before processing them\n");
	fprintf(stderr,  "  --timeref [sync|wall|step|user] \t\t Select timeref for written data\n");
	fprintf(stderr,  "  --epoch \t\tEpoch for --timeref wall. 0 is UNIX epoch.\n");
	fprintf(stderr,  "  --help \t\t Show this help message and exit \n");
};

void displayUsage(char *argv0)
{
	printf("Usage: %s --config <config_file> -i <input_file_prefix> -o <output_file_prefix> [optional arguments]\n", argv0);
}


int main(int argc, char *argv[])
{
	char *configFileName = NULL;
    char *inputFilePrefix = NULL;
	char *outputFileName = NULL;
	FILE_TYPE fileType = FILE_TEXT;
	int hitLimitToWrite = 1;
	long long eventFractionToWrite = 1024;
	bool simulateHwTrigger = false;
	double fileSplitTime = 0;
	RawReader::timeref_t tb = RawReader::SYNC;
	double fileEpoch = 0;

	static struct option longOptions[] = {
		{ "help", no_argument, 0, 0 },
		{ "config", required_argument, 0, 0 },
		{ "writeBinary", no_argument, 0, 0 },
		{ "writeRoot", no_argument, 0, 0 },
		{ "writeTextCompact", no_argument, 0, 0 },
		{ "writeBinaryCompact", no_argument, 0, 0 },
		{ "writeMultipleHits", required_argument, 0, 0},
		{ "writeFraction", required_argument },
		{ "simulateHwTrigger", no_argument, 0, 0},
		{ "splitTime", required_argument, 0, 0},
		{ "timeref", required_argument, 0, 0},
		{ "epoch", required_argument, 0, 0}
    };

	while(true) {
		int optionIndex = 0;
		int c = getopt_long(argc, argv, "i:o:c:",longOptions, &optionIndex);

		if(c == -1) break;
		else if(c != 0) {
			// Short arguments
			switch(c) {
				case 'i':       inputFilePrefix = optarg; break;
				case 'o':       outputFileName = optarg; break;
				default:    displayUsage(argv[0]); exit(1);
			}
		}
		else if(c == 0) {
			switch(optionIndex) {
				case 0:		displayHelp(argv[0]); exit(0); break;
				case 1:		configFileName = optarg; break;
				case 2:		fileType = FILE_BINARY; break;
				case 3:		fileType = FILE_ROOT; break;
				case 4:		fileType = FILE_TEXT_COMPACT; break;
				case 5:		fileType = FILE_BINARY_COMPACT; break;
				case 6:		hitLimitToWrite = boost::lexical_cast<int>(optarg); break;
				case 7:		eventFractionToWrite = round(1024 *boost::lexical_cast<float>(optarg) / 100.0); break;
				case 8:		simulateHwTrigger = true; break;
				case 9:		fileSplitTime = boost::lexical_cast<double>(optarg); break;
				case 10:	if(strcmp(optarg, "sync") == 0) tb = RawReader::SYNC;
							else if(strcmp(optarg, "wall") == 0) tb = RawReader::WALL;
							else if(strcmp(optarg, "step") == 0) tb = RawReader::STEP;
							else if(strcmp(optarg, "user") == 0) tb = RawReader::USER;
							else { fprintf(stderr, "ERROR: unkown timeref '%s'\n", optarg); exit(1); }
							break;
			        case 11:	fileEpoch = boost::lexical_cast<double>(optarg); break;			
				default:	displayUsage(argv[0]); exit(1);

			}
		}
		else {
			assert(false);
		}
	}

	if(configFileName == NULL) {
		fprintf(stderr, "--config must be specified\n");
		exit(1);
	}
	
	if(inputFilePrefix == NULL) {
		fprintf(stderr, "-i must be specified\n");
		exit(1);
	}

	if(outputFileName == NULL) {
		fprintf(stderr, "-o must be specified\n");
		exit(1);
	}

	RawReader *reader = RawReader::openFile(inputFilePrefix, tb);
	
	unsigned long long mask = SystemConfig::LOAD_ALL;
	// If data was taken in full ToT mode, do not attempt to load these files
	if(reader->isTOT()){ 
		mask ^= (SystemConfig::LOAD_QDC_CALIBRATION | SystemConfig::LOAD_ENERGY_CALIBRATION);
	}

	if(!simulateHwTrigger) {
		mask ^= (SystemConfig::LOAD_FIRMWARE_EMPIRICAL_CALIBRATIONS);
	}
	
	SystemConfig *config = SystemConfig::fromFile(configFileName, mask);
	
	DataFileWriter *dataFileWriter = new DataFileWriter(outputFileName, reader->getFrequency(), COINCIDENCE, fileType, fileEpoch, hitLimitToWrite, eventFractionToWrite, fileSplitTime);
	
	int stepIndex = 0;
	while(reader->getNextStep()) {
		float step1, step2;
		reader->getStepValue(step1, step2);
		printf("Processing step %d: (%f, %f)\n", stepIndex+1, step1, step2);
		fflush(stdout);
		dataFileWriter->setStepValues(step1, step2);
		if(!simulateHwTrigger){
			reader->processStep(true,
					new CoarseSorter(
					new ProcessHit(config, reader,
					new SimpleGrouper(config,
					new CoincidenceGrouper(config,
					new WriteCoincidencesHelper(dataFileWriter,
					new NullSink<Coincidence>()
					))))));
		}
		else{
			reader->processStep(true,
					new HwTriggerSimulator(config,
					new ProcessHit(config, reader,
					new SimpleGrouper(config,
					new CoincidenceGrouper(config,
					new WriteCoincidencesHelper(dataFileWriter,
					new NullSink<Coincidence>()
					))))));
		}

		dataFileWriter->closeStep();
		stepIndex += 1;
	}

	delete dataFileWriter;
	delete reader;

	return 0;
}
