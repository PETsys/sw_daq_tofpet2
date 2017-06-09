#include <RawReader.hpp>
#include <OverlappedEventHandler.hpp>
#include <getopt.h>
#include <assert.h>

#include <TFile.h>
#include <TNtuple.h>

using namespace PETSYS;



class DataFileWriter {
private:
	TFile *hFile;
	TNtuple *hData;
public:
	DataFileWriter(char *fName) {
		hFile = new TFile(fName, "RECREATE");
		hData = new TNtuple("data", "Event List", "step1:step2:channelID:tacID:tcoarse:tfine:ecoarse:efine:frameID");
	};
	
	~DataFileWriter() {
		hFile->Write();
		hFile->Close();
	}
	
	void addEvents(float step1, float step2, EventBuffer<RawHit> *buffer) {
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			RawHit &e = buffer->get(i);
			hData->Fill(step1, step2, e.channelID, e.tacID, e.tcoarse, e.tfine, e.ecoarse, e.efine, e.frameID);
		}
		
	};
	
};

class WriteHelper : public OverlappedEventHandler<RawHit, RawHit> {
private: 
	DataFileWriter *rootFile;
	float step1;
	float step2;
public:
	WriteHelper(DataFileWriter *rootFile, float step1, float step2, EventSink<RawHit> *sink) :
		OverlappedEventHandler<RawHit, RawHit>(sink, true),
		rootFile(rootFile), step1(step1), step2(step2)
	{
	};
	
	EventBuffer<RawHit> * handleEvents(EventBuffer<RawHit> *buffer) {
		rootFile->addEvents(step1, step2, buffer);
		return buffer;
	};
	
	void pushT0(double t0) { };
	void report() { };
};

void displayHelp(char * program)
{
	fprintf(stderr, "Usage: %s --config <config_file> -i <input_file_prefix> -o <output_file_prefix> [optional arguments]\n", program);
	fprintf(stderr, "Arguments:\n");
	fprintf(stderr,  "  --config \t\t Configuration file containing path to tdc calibration table \n");
	fprintf(stderr,  "  -i \t\t\t Input file prefix - raw data\n");
	fprintf(stderr,  "  -o \t\t\t Output file name - containins raw event data in ROOT format.\n");
	fprintf(stderr, "Optional flags:\n");
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
    
        static struct option longOptions[] = {
                { "help", no_argument, 0, 0 },
                { "config", required_argument, 0, 0 }
        };

        while(true) {
                int optionIndex = 0;
                int c = getopt_long(argc, argv, "i:o:",longOptions, &optionIndex);

                if(c == -1) break;
                else if(c != 0) {
                        // Short arguments
                        switch(c) {
                        case 'i':       inputFilePrefix = optarg; break;
                        case 'o':       outputFileName = optarg; break;
			default:        displayUsage(argv[0]); exit(1);
			}
		}
		else if(c == 0) {
			switch(optionIndex) {
			case 0:		displayHelp(argv[0]); exit(0); break;
                        case 1:		configFileName = optarg; break;
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
	

	
	RawReader *reader = RawReader::openFile(inputFilePrefix);
	
	DataFileWriter *rootFile = new DataFileWriter(outputFileName);
	
	for(int stepIndex = 0; stepIndex < reader->getNSteps(); stepIndex++) {
		float step1, step2;
		reader->getStepValue(stepIndex, step1, step2);
		printf("Processing step %d of %d: (%f, %f)\n", stepIndex, reader->getNSteps(), step1, step2);
		
		reader->processStep(stepIndex, true,
				new WriteHelper(rootFile, step1, step2,
				new NullSink<RawHit>()
				));
		
	}

	delete rootFile;
	delete reader;

	return 0;
}
