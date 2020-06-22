#include <RawReader.hpp>
#include <OverlappedEventHandler.hpp>
#include <getopt.h>
#include <assert.h>
#include <SystemConfig.hpp>
#include <CoarseSorter.hpp>
#include <ProcessHit.hpp>
#include <SimpleGrouper.hpp>
#include <CoincidenceGrouper.hpp>

#include <boost/lexical_cast.hpp>

#include <TFile.h>
#include <TNtuple.h>

using namespace PETSYS;


enum FILE_TYPE { FILE_TEXT, FILE_BINARY, FILE_ROOT };

class DataFileWriter {
private:
	double frequency;
	FILE_TYPE fileType;
	int hitLimitToWrite;
	int eventFractionToWrite;
	long long eventCounter;

	FILE *dataFile;
	FILE *indexFile;
	off_t stepBegin;
	
	TTree *hData;
	TTree *hIndex;
	TFile *hFile;
	// ROOT Tree fields
	float		brStep1;
	float		brStep2;
	long long 	brStepBegin;
	long long 	brStepEnd;

	unsigned short	br1N, 		br2N;
	unsigned short	br1J,		br2J;
	long long	br1Time,	br2Time;
	unsigned int	br1ChannelID,	br2ChannelID;
	float		br1ToT,		br2ToT;
	float		br1TCoarse, 	br2TCoarse;
	float		br1TFine, 	br2TFine;
	float		br1QCoarse, 	br2QCoarse;
	float		br1QFine, 	br2QFine;
	float		br1Energy, 	br2Energy;
	unsigned short	br1TacID,	br2TacID;
	int		br1Xi,		br2Xi;
	int		br1Yi,		br2Yi;
	float		br1X,		br2X;
	float 		br1Y,		br2Y;
	float 		br1Z,		br2Z;
	
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
	} __attribute__((__packed__));
	
public:
	DataFileWriter(char *fName, double frequency, FILE_TYPE fileType, int hitLimitToWrite, int eventFractionToWrite) {
		this->frequency = frequency;
		this->fileType = fileType;
		this->hitLimitToWrite = hitLimitToWrite;
		this->eventFractionToWrite = eventFractionToWrite;
		this->eventCounter = 0;

		stepBegin = 0;
		
		if (fileType == FILE_ROOT){
			hFile = new TFile(fName, "RECREATE");
			int bs = 512*1024;

			hData = new TTree("data", "Event List", 2);
			hData->Branch("step1", &brStep1, bs);
			hData->Branch("step2", &brStep2, bs);
			
			hData->Branch("mh_n1", &br1N, bs);
			hData->Branch("mh_j1", &br1J, bs);
			hData->Branch("tot1", &br1ToT, bs);
			hData->Branch("time1", &br1Time, bs);
			hData->Branch("channelID1", &br1ChannelID, bs);
			hData->Branch("tcoarse1", &br1TCoarse, bs);
			hData->Branch("tfine1", &br1TFine, bs);
			hData->Branch("qcoarse1", &br1QCoarse, bs);
			hData->Branch("qfine1", &br1QFine, bs);
			hData->Branch("energy1", &br1Energy, bs);
			hData->Branch("tacID1", &br1TacID, bs);
			hData->Branch("xi1", &br1Xi, bs);
			hData->Branch("yi1", &br1Yi, bs);
			hData->Branch("x1", &br1X, bs);
			hData->Branch("y1", &br1Y, bs);
			hData->Branch("z1", &br1Z, bs);
			hData->Branch("mh_n2", &br2N, bs);
			hData->Branch("mh_j2", &br2J, bs);
			hData->Branch("time2", &br2Time, bs);
			hData->Branch("channelID2", &br2ChannelID, bs);
			hData->Branch("tot2", &br2ToT, bs);
			hData->Branch("tcoarse2", &br2TCoarse, bs);
			hData->Branch("tfine2", &br2TFine, bs);
			hData->Branch("qcoarse2", &br2QCoarse, bs);
			hData->Branch("qfine2", &br2QFine, bs);
			hData->Branch("energy2", &br2Energy, bs);
			hData->Branch("tacID2", &br2TacID, bs);
			hData->Branch("xi2", &br2Xi, bs);
			hData->Branch("yi2", &br2Yi, bs);
			hData->Branch("x2", &br2X, bs);
			hData->Branch("y2", &br2Y, bs);
			hData->Branch("z2", &br2Z, bs);
			
			hIndex = new TTree("index", "Step Index", 2);
			hIndex->Branch("step1", &brStep1, bs);
			hIndex->Branch("step2", &brStep2, bs);
			hIndex->Branch("stepBegin", &brStepBegin, bs);
			hIndex->Branch("stepEnd", &brStepEnd, bs);
		}
		else if(fileType == FILE_BINARY) {
			char fName2[1024];
			sprintf(fName2, "%s.ldat", fName);
			dataFile = fopen(fName2, "w");
			sprintf(fName2, "%s.lidx", fName);
			indexFile = fopen(fName2, "w");
			assert(dataFile != NULL);
			assert(indexFile != NULL);
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
		else if(fileType == FILE_BINARY) {
			fclose(dataFile);
			fclose(indexFile);
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
		else if(fileType == FILE_BINARY) {
			fprintf(indexFile, "%llu\t%llu\t%e\t%e\n", stepBegin, ftell(dataFile), step1, step2);
			stepBegin = ftell(dataFile);
		}
		else {
			// Do nothing
		}
	};
	
	void addEvents(float step1, float step2,EventBuffer<Coincidence> *buffer) {
		bool writeMultipleHits = false;

		double Tps = 1E12/frequency;
		float Tns = Tps / 1000;
	

		long long tMin = buffer->getTMin() * (long long)Tps;
		
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			long long tmpCounter = eventCounter;
			eventCounter += 1;
			if((tmpCounter % 1024) >= eventFractionToWrite) continue;

			Coincidence &e = buffer->get(i);
			if(!e.valid) continue;
			if(e.nPhotons != 2) continue;
			
			GammaPhoton &p1 = *e.photons[0];
			GammaPhoton &p2 = *e.photons[1];
			
			int limit1 = (hitLimitToWrite < p1.nHits) ? hitLimitToWrite : p1.nHits;
			int limit2 = (hitLimitToWrite < p2.nHits) ? hitLimitToWrite : p2.nHits;
			for(int m = 0; m < limit1; m++) for(int n = 0; n < limit2; n++) {
				if(m != 0 && n != 0) continue;
				Hit &h1 = *p1.hits[m];
				Hit &h2 = *p2.hits[n];
				
				float Eunit1 = h1.raw->qdcMode ? 1.0 : Tns;
				float Eunit2 = h2.raw->qdcMode ? 1.0 : Tns;

				if (fileType == FILE_ROOT){
					brStep1 = step1;
					brStep2 = step2;

					br1N  = p1.nHits;
					br1J = m;
					br1Time = ((long long)(h1.time * Tps)) + tMin;
					br1ChannelID = h1.raw->channelID;
					br1ToT = (h1.timeEnd - h1.time) * Tps;
					br1TCoarse = h1.raw->tcoarse;
					br1TFine = h1.raw->tfine;
					br1QCoarse = h1.raw->ecoarse;
					br1QFine = h1.raw->efine;
					br1Energy = h1.energy * Eunit1;
					br1TacID = h1.raw->tacID;
					br1X = h1.x;
					br1Y = h1.y;
					br1Z = h1.z;
					br1Xi = h1.xi;
					br1Yi = h1.yi;
					
					br2N  = p2.nHits;
					br2J = n;
					br2Time = ((long long)(h2.time * Tps)) + tMin;
					br2ChannelID = h2.raw->channelID;
					br2ToT = (h2.timeEnd - h2.time) * Tps;
					br2TCoarse = h2.raw->tcoarse;
					br2TFine = h2.raw->tfine;
					br2QCoarse = h2.raw->ecoarse;
					br2QFine = h2.raw->efine;
					br2Energy = h2.energy * Eunit2;
					br2TacID = h2.raw->tacID;
					br2X = h2.x;
					br2Y = h2.y;
					br2Z = h2.z;
					br2Xi = h2.xi;
					br2Yi = h2.yi;
					
					hData->Fill();
				}
				else if(fileType == FILE_BINARY) {
					Event eo = { 
						(uint8_t)p1.nHits, (uint8_t)m,
						((long long)(h1.time * Tps)) + tMin,
						h1.energy * Eunit1,
						(int)h1.raw->channelID,
						
						
						(uint8_t)p2.nHits, (uint8_t)n,
						((long long)(h2.time * Tps)) + tMin,
						h2.energy * Eunit2,
						(int)h2.raw->channelID
						
					};
					fwrite(&eo, sizeof(eo), 1, dataFile);
				}
				else {
					fprintf(dataFile, "%d\t%d\t%lld\t%f\t%d\t%d\t%d\t%lld\t%f\t%d\n",
						p1.nHits, m,
						((long long)(h1.time * Tps)) + tMin,
						h1.energy * Eunit1,
						h1.raw->channelID,
						
						p2.nHits, n,
						((long long)(h2.time * Tps)) + tMin,
						h2.energy * Eunit2,
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
	float step1;
	float step2;
public:
	WriteHelper(DataFileWriter *dataFileWriter, float step1, float step2, EventSink<Coincidence> *sink) :
		OverlappedEventHandler<Coincidence, Coincidence>(sink, true),
		dataFileWriter(dataFileWriter), step1(step1), step2(step2)
	{
	};
	
	EventBuffer<Coincidence> * handleEvents(EventBuffer<Coincidence> *buffer) {
		dataFileWriter->addEvents(step1, step2, buffer);
		return buffer;
	};
};

void displayHelp(char * program)
{
	fprintf(stderr, "Usage: %s --config <config_file> -i <input_file_prefix> -o <output_file_prefix> [optional arguments]\n", program);
	fprintf(stderr, "Arguments:\n");
	fprintf(stderr,  "  --config \t\t Configuration file containing path to tdc calibration table \n");
	fprintf(stderr,  "  -i \t\t\t Input file prefix - raw data\n");
	fprintf(stderr,  "  -o \t\t\t Output file name - by default in text data format\n");
	fprintf(stderr, "Optional flags:\n");
	fprintf(stderr,  "  --writeBinary \t Set the output data format to binary\n");
	fprintf(stderr,  "  --writeRoot \t\t Set the output data format to ROOT TTree\n");
	fprintf(stderr,  "  --writeMultipleHits N \t\t Writes multiple hits, up to the Nth hit\n");
	fprintf(stderr,  "  --writeFraction N \t\t Fraction of events to write. Default: 100%.\n");
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


        static struct option longOptions[] = {
                { "help", no_argument, 0, 0 },
                { "config", required_argument, 0, 0 },
		{ "writeBinary", no_argument, 0, 0 },
		{ "writeRoot", no_argument, 0, 0 },
		{ "writeMultipleHits", required_argument, 0, 0},
		{ "writeFraction", required_argument }

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
			case 0:		displayHelp(argv[0]); exit(0); break;
                        case 1:		configFileName = optarg; break;
			case 2:		fileType = FILE_BINARY; break;
			case 3:		fileType = FILE_ROOT; break;
			case 4:		hitLimitToWrite = boost::lexical_cast<int>(optarg); break;
			case 5:		eventFractionToWrite = round(1024 *boost::lexical_cast<float>(optarg) / 100.0); break;
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
	
	// If data was taken in full ToT mode, do not attempt to load these files
	unsigned long long mask = SystemConfig::LOAD_ALL;	
	if(reader->isTOT()){ 
		mask ^= (SystemConfig::LOAD_QDC_CALIBRATION | SystemConfig::LOAD_ENERGY_CALIBRATION);
	}

	SystemConfig *config = SystemConfig::fromFile(configFileName, mask);
	
	DataFileWriter *dataFileWriter = new DataFileWriter(outputFileName, reader->getFrequency(), fileType, hitLimitToWrite, eventFractionToWrite);
	
	for(int stepIndex = 0; stepIndex < reader->getNSteps(); stepIndex++) {
		float step1, step2;
		reader->getStepValue(stepIndex, step1, step2);
		printf("Processing step %d of %d: (%f, %f)\n", stepIndex+1, reader->getNSteps(), step1, step2);
		fflush(stdout);
		reader->processStep(stepIndex, true,
				new CoarseSorter(
				new ProcessHit(config, reader,
				new SimpleGrouper(config,
				new CoincidenceGrouper(config,
				new WriteHelper(dataFileWriter, step1, step2,
				new NullSink<Coincidence>()
				))))));
		
		dataFileWriter->closeStep(step1, step2);
	}

	delete dataFileWriter;
	delete reader;

	return 0;
}
