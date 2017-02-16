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
		uint8_t mh_n1; 
		uint8_t mh_j1;
		long long time1;
		float e1;
		int id1;

		
		uint8_t mh_n2; 
		uint8_t mh_j2;
		long long time2;
		float e2;
		int id2;
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
		
		fprintf(indexFile, "%llu\t|llu\t%e\t%e\n", stepBegin, ftell(dataFile), step1, step2);
		stepBegin = ftell(dataFile);
	};
	
	void addEvents(EventBuffer<Coincidence> *buffer) {
		bool writeMultipleHits = false;
		
		double Tps = 1E12/frequency;
		float Tns = Tps / 1000;
		
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			Coincidence &e = buffer->get(i);
			if(!e.valid) continue;
			if(e.nPhotons != 2) continue;
			
			GammaPhoton &p1 = *e.photons[0];
			GammaPhoton &p2 = *e.photons[1];
			
			int limit1 = writeMultipleHits ? p1.nHits : 1;
			int limit2 = writeMultipleHits ? p2.nHits : 1;
			for(int m = 0; m < limit1; m++) for(int n = 0; n < limit2; n++) {
				if(m != 0 && n != 0) continue;
				Hit &h1 = *p1.hits[m];
				Hit &h2 = *p2.hits[n];
				
				if(isBinary) {
					Event eo = { 
						(uint8_t)p1.nHits, (uint8_t)m,
						(long long)(h1.time * Tps), // WARNING Get this from raw file header
						h1.energy * Tns,
						(int)h1.raw->channelID,
						
						
						(uint8_t)p2.nHits, (uint8_t)n,
						(long long)(h2.time * Tps),
						h2.energy * Tns,
						(int)h2.raw->channelID
						
					};
					fwrite(&eo, sizeof(eo), 1, dataFile);
				}
				else {
					fprintf(dataFile, "%d\t%d\t%lld\t%f\t%d\t%d\t%d\t%lld\t%f\t%d\n",
						p1.nHits, m,
						(long long)(h1.time * Tps),
						h1.energy * Tns,
						h1.raw->channelID,
						
						p2.nHits, n,
						(long long)(h2.time * Tps),
						h2.energy * Tns,
						h2.raw->channelID
					);
				}
			}
		}
		
	};
	
};

class WriteHelper : public OverlappedEventHandler<Coincidence, Coincidence> {
private: 
	DataFileWriter *dataFileWriter;
public:
	WriteHelper(DataFileWriter *dataFileWriter, EventSink<Coincidence> *sink) :
		OverlappedEventHandler<Coincidence, Coincidence>(sink, true),
		dataFileWriter(dataFileWriter)
	{
	};
	
	EventBuffer<Coincidence> * handleEvents(EventBuffer<Coincidence> *buffer) {
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
		printf("Processing step %d of %d: (%f, %f)\n", stepIndex, reader->getNSteps(), step1, step2);
		
		reader->processStep(stepIndex,
				new CoarseSorter(
				new ProcessHit(config,
				new SimpleGrouper(config,
				new CoincidenceGrouper(config,
				new WriteHelper(dataFileWriter,
				new NullSink<Coincidence>()
				))))));
		
		dataFileWriter->closeStep(step1, step2);
	}

	delete dataFileWriter;
	delete reader;

	return 0;
}
