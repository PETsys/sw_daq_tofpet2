#include <RawReader.hpp>
#include <OrderedEventHandler.hpp>
#include <getopt.h>
#include <assert.h>

#include <boost/lexical_cast.hpp>

#include <TFile.h>
#include <TNtuple.h>

using namespace PETSYS;



enum FILE_TYPE { FILE_TEXT, FILE_ROOT };

class DataFileWriter {
private:
	FILE_TYPE fileType;
	FILE *dataFile;
	FILE *indexFile;
	off_t stepBegin;

	int eventFractionToWrite;
	long long eventCounter;
	
	TTree *hData;
	TTree *hIndex;
	TFile *hFile;
	// ROOT Tree fields
	float		brStep1;
	float		brStep2;
	long long 	brStepBegin;
	long long 	brStepEnd;

	long long	brFrameID;
	unsigned int	brChannelID;
	unsigned short	brTacID;
	unsigned short	brTCoarse;
	unsigned short	brECoarse;
	unsigned short	brTFine;
	unsigned short	brEFine;
	


public:
	DataFileWriter(char *fName, FILE_TYPE fileType, int eventFractionToWrite) {
		this->fileType = fileType;
		this->eventFractionToWrite = eventFractionToWrite;
		this->eventCounter = 0;

		stepBegin = 0;
		
		if (fileType == FILE_ROOT){
			hFile = new TFile(fName, "RECREATE");
			int bs = 512*1024;

			hData = new TTree("data", "Event List", 2);
			hData->Branch("step1", &brStep1, bs);
			hData->Branch("step2", &brStep2, bs);
			hData->Branch("frameID", &brFrameID, bs);
			hData->Branch("channelID", &brChannelID, bs);
			hData->Branch("tacID", &brTacID, bs);
			hData->Branch("tcoarse", &brTCoarse, bs);
			hData->Branch("ecoarse", &brECoarse, bs);
			hData->Branch("tfine", &brTFine, bs);
			hData->Branch("efine", &brEFine, bs);
			
			hIndex = new TTree("index", "Step Index", 2);
			hIndex->Branch("step1", &brStep1, bs);
			hIndex->Branch("step2", &brStep2, bs);
			hIndex->Branch("stepBegin", &brStepBegin, bs);
			hIndex->Branch("stepEnd", &brStepEnd, bs);
		}
		else {
			dataFile = fopen(fName, "w");
			assert(dataFile != NULL);
			indexFile = NULL;
		}
	};
	
	~DataFileWriter() {
		if (fileType == FILE_ROOT){
			hFile->Write();
			hFile->Close();
		}
		else {
			fclose(dataFile);
		}
	}
	
	void closeStep(float step1, float step2) {
		if (fileType == FILE_ROOT){
			brStepBegin = stepBegin;
			brStepEnd = hData->GetEntries();
			brStep1 = step1;
			brStep2 = step2;
			hIndex->Fill();
			stepBegin = hData->GetEntries();
			hFile->Write();
		}
		else {
			// Do nothing
		}
	};
	
	void addEvents(float step1, float step2,EventBuffer<RawHit> *buffer) {
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			long long tmpCounter = eventCounter;
			eventCounter += 1;
			if((tmpCounter % 1024) >= eventFractionToWrite) continue;

			RawHit &hit = buffer->get(i);
			if (fileType == FILE_ROOT){
				brStep1 = step1;
				brStep2 = step2;
				
				brFrameID = hit.frameID;
				brChannelID = hit.channelID;
				brTacID = hit.tacID;
				brTCoarse = hit.tcoarse;
				brECoarse = hit.ecoarse;
				brTFine = hit.tfine;
				brEFine = hit.efine;
				hData->Fill();
			}
			else {
				fprintf(dataFile, "%lu\t%u\t%hu\t%hu\t%hu\t%hu\t%hu\n",
					hit.frameID,
					hit.channelID, hit.tacID,
					hit.tcoarse, hit.ecoarse,
					hit.tfine, hit.efine
				);
			}
		}
		
	}
	
};

class WriteHelper : public OrderedEventHandler<RawHit, RawHit> {
private: 
	DataFileWriter *dataFileWriter;
	float step1;
	float step2;
public:
	WriteHelper(DataFileWriter *dataFileWriter, float step1, float step2, EventSink<RawHit> *sink) :
		OrderedEventHandler<RawHit, RawHit>(sink),
		dataFileWriter(dataFileWriter), step1(step1), step2(step2)
	{
	};
	
	EventBuffer<RawHit> * handleEvents(EventBuffer<RawHit> *buffer) {
		dataFileWriter->addEvents(step1, step2,buffer);
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
	fprintf(stderr,  "  --writeFraction N \t\t Fraction of events to write. Default: 100%%.\n");
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
	long long eventFractionToWrite = 1024;
    
        static struct option longOptions[] = {
                { "help", no_argument, 0, 0 },
                { "config", required_argument, 0, 0 },
		{ "writeFraction", required_argument }
		
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
			case 2:		eventFractionToWrite = round(1024 *boost::lexical_cast<float>(optarg) / 100.0); break;
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
	
	DataFileWriter *dataFileWriter = new DataFileWriter(outputFileName, FILE_ROOT, eventFractionToWrite);
	
	for(int stepIndex = 0; stepIndex < reader->getNSteps(); stepIndex++) {
		float step1, step2;
		reader->getStepValue(stepIndex, step1, step2);
		printf("Processing step %d of %d: (%f, %f)\n", stepIndex+1, reader->getNSteps(), step1, step2);
		fflush(stdout);
		reader->processStep(stepIndex, true,
				new WriteHelper(dataFileWriter, step1, step2,
				new NullSink<RawHit>()
				));
		
		dataFileWriter->closeStep(step1, step2);
	}

	delete dataFileWriter;
	delete reader;

	return 0;
}
