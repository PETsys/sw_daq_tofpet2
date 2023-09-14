#include <RawReader.hpp>
#include <OrderedEventHandler.hpp>
#include <getopt.h>
#include <assert.h>
#include <strings.h>
#include <SystemConfig.hpp>
#include <CoarseSorter.hpp>
#include <ProcessHit.hpp>
#include <SimpleGrouper.hpp>

#include <boost/lexical_cast.hpp>

#include <TFile.h>
#include <TNtuple.h>

using namespace PETSYS;


enum FILE_TYPE { FILE_TEXT, FILE_BINARY, FILE_ROOT, FILE_NULL, FILE_TEXT_COMPACT, FILE_BINARY_COMPACT};

class DataFileWriter {
private:
	std::string fName;
	double frequency;
	FILE_TYPE fileType;
	int eventFractionToWrite;
	long long eventCounter;
	double fileSplitTime;
	long long currentFilePartIndex;

	int hitLimitToWrite;


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

	unsigned short	brN;
	unsigned short	brJ;
	long long	brTime;
	long long	brTimeDelta;
	unsigned int	brChannelID;
	float		brToT;
	float		brEnergy;
	unsigned short	brTacID;
	int		brXi;
	int		brYi;
	float		brX;
	float 		brY;
	float 		brZ;
	
	struct GroupEvent {
		uint8_t mh_n; 
		uint8_t mh_j;
		long long time;
		float e;
		int id;
	} __attribute__((__packed__));
	
	struct Event {
		long long time;
		float e;
		int id;
	} __attribute__((__packed__));

	typedef uint8_t GroupHeader;

public:
	DataFileWriter(char *fName, double frequency, FILE_TYPE fileType, int hitLimitToWrite, int eventFractionToWrite, float splitTime) {
		this->fName = std::string(fName);
		this->frequency = frequency;
		this->fileType = (strcmp(fName, "/dev/null") != 0) ? fileType : FILE_NULL;
		this->eventFractionToWrite = eventFractionToWrite;
		this->eventCounter = 0;

		this->fileSplitTime = splitTime * frequency; // Convert from seconds to clock cycles
		this->currentFilePartIndex = 0;

		this->hitLimitToWrite = hitLimitToWrite;

		openFile();
	};

	void openFile() {
		stepBegin = 0;
		if (fileType == FILE_ROOT){
			hFile = new TFile(fName.c_str(), "RECREATE");
			int bs = 512*1024;

			hData = new TTree("data", "Event List", 2);
			hData->Branch("step1", &brStep1, bs);
			hData->Branch("step2", &brStep2, bs);
			
			hData->Branch("mh_n", &brN, bs);
			hData->Branch("mh_j", &brJ, bs);
			hData->Branch("tot", &brToT, bs);
			hData->Branch("time", &brTime, bs);
			hData->Branch("timeDelta", &brTimeDelta, bs);
			hData->Branch("channelID", &brChannelID, bs);
			hData->Branch("energy", &brEnergy, bs);
			hData->Branch("tacID", &brTacID, bs);
			hData->Branch("xi", &brXi, bs);
			hData->Branch("yi", &brYi, bs);
			hData->Branch("x", &brX, bs);
			hData->Branch("y", &brY, bs);
			hData->Branch("z", &brZ, bs);
			hIndex = new TTree("index", "Step Index", 2);
			hIndex->Branch("step1", &brStep1, bs);
			hIndex->Branch("step2", &brStep2, bs);
			hIndex->Branch("stepBegin", &brStepBegin, bs);
			hIndex->Branch("stepEnd", &brStepEnd, bs);
		}
		else if(fileType == FILE_BINARY || fileType == FILE_BINARY_COMPACT) {
			char fName2[1024];
			sprintf(fName2, "%s.ldat", fName.c_str());
			dataFile = fopen(fName2, "w");
			sprintf(fName2, "%s.lidx", fName.c_str());
			indexFile = fopen(fName2, "w");
			assert(dataFile != NULL);
			assert(indexFile != NULL);
		}
		else if (fileType == FILE_TEXT || fileType == FILE_TEXT_COMPACT) {
			dataFile = fopen(fName.c_str(), "w");
			assert(dataFile != NULL);
			indexFile = NULL;
		}
	};
	
	~DataFileWriter() {
		closeFile();
		if(fileSplitTime > 0) {
			renameFile();
		}
	};

	void closeFile() {
		if (fileType == FILE_ROOT){
			hFile->Write();
			hFile->Close();
		}
		else if(fileType == FILE_BINARY || fileType == FILE_BINARY_COMPACT) {
			fclose(dataFile);
			fclose(indexFile);
		}
		else if (fileType == FILE_TEXT || fileType == FILE_TEXT_COMPACT) {
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
		}
		else if(fileType == FILE_BINARY || fileType == FILE_BINARY_COMPACT || fileType == FILE_BINARY || fileType == FILE_BINARY_COMPACT) {
			fprintf(indexFile, "%ld\t%ld\t%e\t%e\n", stepBegin, ftell(dataFile), step1, step2);
			stepBegin = ftell(dataFile);
		}
		else {
			// Do nothing
		}
	};

	void renameFile() {
		char *fName1 = new char[1024];
		char *fName2 = new char[1024];
		if(fileType == FILE_BINARY || fileType == FILE_BINARY_COMPACT) {
			// Binary output consists of two files and fName is their common prefix

			sprintf(fName1, "%s.ldat", fName.c_str());
			sprintf(fName2, "%s_%08lld.ldat", fName.c_str(), currentFilePartIndex);
			int r = rename(fName1, fName2);
			assert(r == 0);

			sprintf(fName1, "%s.lidx", fName.c_str());
			sprintf(fName2, "%s_%08lld.lidx", fName.c_str(), currentFilePartIndex);
			r = rename(fName1, fName2);
			assert(r == 0);

		}
		else {
			// ROOT or text output consists of a single file and fName is the complete fileName
			strcpy(fName1, fName.c_str());
			char *p = rindex(fName1, '.');

			if(p == NULL) {
				// If fName lacks a "." append the file part number at the end of the file name
				sprintf(fName2, "%s_%08lld", fName1, currentFilePartIndex);
				int r = rename(fName1, fName2);
				assert(r == 0);
			}
			else {
				// Insert the file part number before the extension
				char tmp = *p;
				*p = '\0';
				sprintf(fName2, "%s_%08lld.%s", fName1, currentFilePartIndex, p+1);
				*p = tmp;
				int r = rename(fName1, fName2);
				assert(r == 0);
			}

		}
		delete [] fName2;
		delete [] fName1;
	};
	
	void addEvents(float step1, float step2,EventBuffer<GammaPhoton> *buffer) {
		long long filePartIndex = (int)floor(buffer->getTMin() / fileSplitTime);

		if((fileSplitTime > 0) && (filePartIndex > currentFilePartIndex)) {
			closeStep(step1, step2);
			closeFile();
			renameFile();

			openFile();
			currentFilePartIndex = filePartIndex;
		}

		bool writeMultipleHits = false;

		double Tps = 1E12/frequency;
		float Tns = Tps / 1000;
	
		long long tMin = buffer->getTMin() * (long long)Tps;
		
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			long long tmpCounter = eventCounter;
			eventCounter += 1;
			if((tmpCounter % 1024) >= eventFractionToWrite) continue;

			GammaPhoton &p = buffer->get(i);
			
			if(!p.valid) continue;

			Hit &h0 = *p.hits[0];
			int limit = (hitLimitToWrite < p.nHits) ? hitLimitToWrite : p.nHits;

			if(fileType == FILE_TEXT_COMPACT) {
				fprintf(dataFile, "%d\n", limit);
			}
			else if(fileType == FILE_BINARY_COMPACT) {
				GroupHeader header = {(uint8_t)limit};
				fwrite(&header, sizeof(header), 1, dataFile);
			}

			for(int m = 0; m < limit; m++) {
				Hit &h = *p.hits[m];
				float Eunit = h.raw->qdcMode ? 1.0 : Tns;


				if (fileType == FILE_ROOT){
					brStep1 = step1;
					brStep2 = step2;

					brN  = p.nHits;
					brJ = m;
					brTime = ((long long)(h.time * Tps)) + tMin;
					brTimeDelta = (long long)((h.time - h0.time) * Tps);
					brChannelID = h.raw->channelID;
					brToT = (h.timeEnd - h.time) * Tps;
					brEnergy = h.energy * Eunit;
					brTacID = h.raw->tacID;
					brX = h.x;
					brY = h.y;
					brZ = h.z;
					brXi = h.xi;
					brYi = h.yi;
					
					hData->Fill();
				}
				else if(fileType == FILE_BINARY) {
					GroupEvent eo = { 
						(uint8_t)p.nHits, (uint8_t)m,
						((long long)(h.time * Tps)) + tMin,
						h.energy * Eunit,
						(int)h.raw->channelID
					};
					fwrite(&eo, sizeof(eo), 1, dataFile);
				}
				else if(fileType == FILE_BINARY_COMPACT) {
					Event eo = {
						((long long)(h.time * Tps)) + tMin,
						h.energy * Eunit,
						(int)h.raw->channelID
					};
					fwrite(&eo, sizeof(eo), 1, dataFile);
				}
				else if (fileType == FILE_TEXT) {
					fprintf(dataFile, "%d\t%d\t%lld\t%f\t%d\n",
						p.nHits, m,
						((long long)(h.time * Tps)) + tMin,
						h.energy * Eunit,
						h.raw->channelID
					);
				}
				else if (fileType == FILE_TEXT_COMPACT) {
					fprintf(dataFile, "%lld\t%f\t%d\n",
						((long long)(h.time * Tps)) + tMin,
						h.energy * Eunit,
						h.raw->channelID
					);
				}
			}
		}	
	};
};

class WriteHelper : public OrderedEventHandler<GammaPhoton, GammaPhoton> {
private: 
	DataFileWriter *dataFileWriter;
	float step1;
	float step2;
public:
	WriteHelper(DataFileWriter *dataFileWriter, float step1, float step2, EventSink<GammaPhoton> *sink) :
		OrderedEventHandler<GammaPhoton, GammaPhoton>(sink),
		dataFileWriter(dataFileWriter), step1(step1), step2(step2)
	{
	};
	
	EventBuffer<GammaPhoton> * handleEvents(EventBuffer<GammaPhoton> *buffer) {
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
	fprintf(stderr,  "  --writeBinaryCompact \t Set the output data format to compact binary\n");
	fprintf(stderr,  "  --writeTextCompact \t Set the output data format to compact text \n");
	fprintf(stderr,  "  --writeMultipleHits N \t\t Writes multiple hits, up to the Nth hit\n");
	fprintf(stderr,  "  --writeFraction N \t\t Fraction of events to write. Default: 100%%.\n");
	fprintf(stderr,  "  --splitTime t \t\t Split output into different files every t seconds.\n");
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
	double fileSplitTime = 0.0;

	static struct option longOptions[] = {
		{ "help", no_argument, 0, 0 },
		{ "config", required_argument, 0, 0 },
		{ "writeBinary", no_argument, 0, 0 },
		{ "writeRoot", no_argument, 0, 0 },
		{ "writeTextCompact", no_argument, 0, 0 },
		{ "writeBinaryCompact", no_argument, 0, 0 },
		{ "writeMultipleHits", required_argument, 0, 0},
		{ "writeFraction", required_argument, 0, 0},
		{ "splitTime", required_argument, 0, 0}
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
			case 4:		fileType = FILE_TEXT_COMPACT; break;
			case 5:		fileType = FILE_BINARY_COMPACT; break;
			case 6:		hitLimitToWrite = boost::lexical_cast<int>(optarg); break;
			case 7:		eventFractionToWrite = round(1024 *boost::lexical_cast<float>(optarg) / 100.0); break;
			case 8:		fileSplitTime = boost::lexical_cast<double>(optarg); break;
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
	
	// If data was taken in ToT mode, do not attempt to load these files
	unsigned long long mask = SystemConfig::LOAD_ALL;
	if(reader->isTOT()) {
		mask ^= (SystemConfig::LOAD_QDC_CALIBRATION | SystemConfig::LOAD_ENERGY_CALIBRATION);
	}
	SystemConfig *config = SystemConfig::fromFile(configFileName, mask);
	
	DataFileWriter *dataFileWriter = new DataFileWriter(outputFileName, reader->getFrequency(), fileType, hitLimitToWrite, eventFractionToWrite, fileSplitTime);
	
	int stepIndex = 0;
	while(reader->getNextStep()) {
		float step1, step2;
		reader->getStepValue(step1, step2);
		printf("Processing step %d: (%f, %f)\n", stepIndex+1, step1, step2);
		fflush(stdout);
		reader->processStep(true,
				new CoarseSorter(
				new ProcessHit(config, reader,
				new SimpleGrouper(config,
				new WriteHelper(dataFileWriter, step1, step2,
				new NullSink<GammaPhoton>()
				)))));
		
		dataFileWriter->closeStep(step1, step2);
		stepIndex += 1;
	}

	delete dataFileWriter;
	delete reader;

	return 0;
}
