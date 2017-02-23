#include <RawReader.hpp>
#include <OverlappedEventHandler.hpp>
#include <getopt.h>
#include <assert.h>
#include <SystemConfig.hpp>
#include <CoarseSorter.hpp>
#include <ProcessHit.hpp>
#include <SimpleGrouper.hpp>
#include <CoincidenceGrouper.hpp>

#include <TFile.h>
#include <TNtuple.h>

using namespace PETSYS;



class DataFileWriter {
private:
	double frequency;
	bool isBinary;
	FILE *dataFile;
	FILE *indexFile;
	off_t stepBegin;
	
	struct Event {
		long long time;
		float e;
		int id;  
	};
	
public:
	DataFileWriter(char *fName, double frequency, bool isBinary) {
		this->frequency = frequency;
		this->isBinary = isBinary;
		stepBegin = 0;
		
		if(!isBinary) {
			dataFile = fopen(fName, "w");
			assert(dataFile != NULL);
			indexFile = NULL;
		}
		else {
			char fName2[1024];
			sprintf(fName2, "%s.ldat", fName);
			dataFile = fopen(fName2, "w");
			sprintf(fName2, "%s.lidx", fName);
			indexFile = fopen(fName2, "w");
			assert(dataFile != NULL);
			assert(indexFile != NULL);
		}
	};
	
	~DataFileWriter() {
		fclose(dataFile);
		if(isBinary)
			fclose(indexFile);
	}
	
	void closeStep(float step1, float step2) {
		if(!isBinary) return;
		
		fprintf(indexFile, "%llu\t%llu\t%e\t%e\n", stepBegin, ftell(dataFile), step1, step2);
		stepBegin = ftell(dataFile);
	};
	
	void addEvents(EventBuffer<Hit> *buffer) {
		
		
		double Tps = 1E12/frequency;
		float Tns = Tps / 1000;
		
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			Hit &hit = buffer->get(i);
			if(!hit.valid) continue;
			
			
			if (isBinary) {
				Event eo = {
					(long long)(hit.time * Tps),
					hit.energy * Tns,
					(int)hit.raw->channelID
				};
				fwrite(&eo, sizeof(eo), 1, dataFile);
			}
			else {
				fprintf(dataFile, "%lld\t%f\t%d\n",
					(long long)(hit.time * Tps),
					hit.energy * Tns,
					(int)hit.raw->channelID
					);
			}
			

			
		}
		
	}
	
};

class WriteHelper : public OverlappedEventHandler<Hit, Hit> {
private: 
	DataFileWriter *dataFileWriter;
public:
	WriteHelper(DataFileWriter *dataFileWriter, EventSink<Hit> *sink) :
		OverlappedEventHandler<Hit, Hit>(sink, true),
		dataFileWriter(dataFileWriter)
	{
	};
	
	EventBuffer<Hit> * handleEvents(EventBuffer<Hit> *buffer) {
		dataFileWriter->addEvents(buffer);
		return buffer;
	};
	
	void pushT0(double t0) { };
	void report() { };
};

void displayUsage(char *argv0)
{
	printf("Usage: %s --config <config_file> -i <input_file_prefix> -o <output_file_prefix> [optional arguments]\n");
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
                int c = getopt_long(argc, argv, "i:o:c:",longOptions, &optionIndex);

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
			case 0:		displayUsage(argv[0]); exit(0); break;
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

	SystemConfig *config = SystemConfig::fromFile(configFileName);
	
	RawReader *reader = RawReader::openFile(inputFilePrefix);
	DataFileWriter *dataFileWriter = new DataFileWriter(outputFileName, reader->getFrequency(), true);
	
	for(int stepIndex = 0; stepIndex < reader->getNSteps(); stepIndex++) {
		float step1, step2;
		reader->getStepValue(stepIndex, step1, step2);
		printf("Processing step %d of %d: (%f, %f)\n", stepIndex+1, reader->getNSteps(), step1, step2);
		
		reader->processStep(stepIndex,
				new CoarseSorter(
				new ProcessHit(config,
				new WriteHelper(dataFileWriter,
				new NullSink<Hit>()
				))));
		
		dataFileWriter->closeStep(step1, step2);
	}

	delete dataFileWriter;
	delete reader;

	return 0;
}
