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


#include <RawReader.hpp>
#include <SystemConfig.hpp>
#include <CoarseSorter.hpp>
#include <ProcessHit.hpp>

#include <boost/random.hpp>
#include <boost/nondet_random.hpp>
#include <boost/lexical_cast.hpp>

#include <TF1.h>
#include <TH1S.h>
#include <TH1D.h>
#include <TH2S.h>
#include <TProfile.h>
#include <TGraphErrors.h>
#include <TFile.h>
#include <TCanvas.h>

using namespace PETSYS;

struct CalibrationEvent {
	unsigned long gid;
	double ti;
	unsigned short qfine;
};

// TODO Put this somewhere else
const unsigned long MAX_N_ASIC = 32*32*64;
const unsigned long MAX_N_QAC = MAX_N_ASIC * 64 * 4;

struct CalibrationEntry {
	float p0;
	float p1;
	float p2;
	float p3;
	float p4;
	float xMin;
	float xMax;
	bool valid;
};


void sortData(SystemConfig *config, char *inputFilePrefix, char *outputFilePrefix, int verbosity);
void calibrateAllAsics(CalibrationEntry *calibrationTable, char *outputFilePrefix, int nBins, float xMin, float xMax);
void writeCalibrationTable(CalibrationEntry *calibrationTable, const char *outputFilePrefix);
void deleteTemporaryFiles(const char *outputFilePrefix);

void displayUsage() {
}

int main(int argc, char *argv[])
{
	float nominalM = 200;
	char *configFileName = NULL;
	char *inputFilePrefix = NULL;
	char *outputFilePrefix = NULL;
	bool doSorting = true;
	bool keepTemporary = false;
	int verbosity = 0;
 
        static struct option longOptions[] = {
                { "help", no_argument, 0, 0 },
                { "config", required_argument, 0, 0 },
                { "no-sorting", no_argument, 0, 0 },
                { "keep-temporary", no_argument, 0, 0 },
		{ "verbosity", required_argument, 0, 0 }
        };

	while(true) {
		int optionIndex = 0;
                int c = getopt_long(argc, argv, "i:o:c:",longOptions, &optionIndex);

		if(c == -1) break;
		else if(c != 0) {
			// Short arguments
			switch(c) {
			case 'i':	inputFilePrefix = optarg; break;
			case 'o':	outputFilePrefix = optarg; break;
			default:	displayUsage(); exit(1);
			}
		}
		else if(c == 0) {
			switch(optionIndex) {
			case 0: 	displayUsage(); exit(0); break;
			case 1:		configFileName = optarg; break;
			case 2:		doSorting = false; break;
			case 3:		keepTemporary = true; break;
			case 4:		verbosity = boost::lexical_cast<int>(optarg); break;
			default:	displayUsage(); exit(1);
			}
		}
		else {
			assert(false);
		}

	}
	
	SystemConfig *config = SystemConfig::fromFile(configFileName, SystemConfig::LOAD_TDC_CALIBRATION);
	

	char fName[1024];
	sprintf(fName, "%s.bins", inputFilePrefix);
	FILE *binsFile = fopen(fName, "r");
	if(binsFile == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
		exit(1);
	}
	int nBins;
	float xMin, xMax;
	if(fscanf(binsFile, "%d\t%f\t%f\n", &nBins, &xMin, &xMax) != 3) {
		fprintf(stderr, "Error parsing %s\n", fName);
		exit(1);
	}
	
	CalibrationEntry *calibrationTable = (CalibrationEntry *)mmap(NULL, sizeof(CalibrationEntry)*MAX_N_QAC, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	for(int gid = 0; gid < MAX_N_QAC; gid++) calibrationTable[gid].valid = false;

	if(doSorting) {
		sortData(config, inputFilePrefix, outputFilePrefix, verbosity);
	}
 	calibrateAllAsics(calibrationTable, outputFilePrefix, nBins, xMin, xMax);

	writeCalibrationTable(calibrationTable, outputFilePrefix);
	if(!keepTemporary) {
		deleteTemporaryFiles(outputFilePrefix);
	}

	return 0;
}

class EventWriter {
private:
	char *outputFilePrefix;
	unsigned long maxgAsicID;
	char **tmpDataFileNames;
	FILE **tmpDataFiles;
	FILE *tmpListFile;
	
public:
	EventWriter(char *outputFilePrefix)
	{
		this->outputFilePrefix = outputFilePrefix;
		maxgAsicID = 0;	
		tmpDataFileNames = new char *[MAX_N_ASIC];
		tmpDataFiles = new FILE *[MAX_N_ASIC];
		for(int n = 0; n < MAX_N_ASIC; n++)
		{
			tmpDataFileNames[n] = NULL;
			tmpDataFiles[n] = NULL;
		}
		
		// Create the temporary list file
		char fName[1024];
		sprintf(fName, "%s_list.tmp", outputFilePrefix);
		tmpListFile = fopen(fName, "w");
		if(tmpListFile == NULL) {
			fprintf(stderr, "Could not open '%s' for writing: %s\n", fName, strerror(errno));
			exit(1);
		}
	};
	
	void handleEvents(EventBuffer<Hit> *buffer)
	{
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			Hit &hit = buffer->get(i);
			if(!hit.valid) continue;
			
			unsigned long gChannelID = hit.raw->channelID;
			unsigned long tacID = hit.raw->tacID;
			unsigned long gid = (gChannelID << 2) | tacID;
			unsigned gAsicID = gChannelID / 64;

			maxgAsicID = (maxgAsicID > gAsicID) ? maxgAsicID : gAsicID;
			
			FILE * f = tmpDataFiles[gAsicID];
			if(f == NULL) {
				// We haven't opened this file yet.
				unsigned chipID = (gChannelID >> 6) % 64;
				unsigned slaveID = (gChannelID >> 12) % 32;
				unsigned portID = (gChannelID >> 17) % 32;
				char *fn = new char[1024];
				sprintf(fn, "%s_%02u_%02u_%02u_data.tmp", outputFilePrefix, portID, slaveID, chipID);
				f = fopen(fn, "w");
				if(f == NULL) {
					fprintf(stderr, "Could not open '%s' for reading: %s\n", fn, strerror(errno));
					exit(1);
				}
				tmpDataFiles[gAsicID] = f;
				tmpDataFileNames[gAsicID] = fn;
			}
			
			CalibrationEvent e;
			e.gid = gid;
			e.ti = hit.timeEnd - hit.time;
			e.qfine = hit.raw->efine; // E Fine has the ADC output
			fwrite((void*)&e, sizeof(e), 1, f);
		}
	};
	
	void close()
	{
		for(unsigned long gAsicID = 0; gAsicID <= maxgAsicID; gAsicID++) {
			if(tmpDataFiles[gAsicID] != NULL) {
				fprintf(tmpListFile, "%lu %s\n", gAsicID, tmpDataFileNames[gAsicID]);
				fclose(tmpDataFiles[gAsicID]);
			}
			tmpDataFiles[gAsicID] = NULL;
			delete [] tmpDataFileNames[gAsicID];
		}
		fclose(tmpListFile);
	};
};

class WriteHelper : public OverlappedEventHandler<Hit, Hit> {
private: 
	EventWriter *eventWriter;
public:
	WriteHelper(EventWriter *eventWriter, EventSink<Hit> *sink) :
		OverlappedEventHandler<Hit, Hit>(sink, true),
		eventWriter(eventWriter)
	{
	};
	
	EventBuffer<Hit> * handleEvents(EventBuffer<Hit> *buffer) {
		eventWriter->handleEvents(buffer);
		return buffer;
	};
};

void sortData(SystemConfig *config, char *inputFilePrefix, char *outputFilePrefix, int verbosity)
{
	
	RawReader *reader = RawReader::openFile(inputFilePrefix);
	assert(reader->isQDC());
	EventWriter *eventWriter = new EventWriter(outputFilePrefix);
	for(int stepIndex = 0; stepIndex < reader->getNSteps(); stepIndex++) {
		reader->processStep(stepIndex, (verbosity > 0),
				new ProcessHit(config, true,
				new WriteHelper(eventWriter,
				new NullSink<Hit>()
				)));
	}
	eventWriter->close();
	delete reader;
}


void calibrateAsic(
	unsigned long gAsicID,
	int dataFile,
	CalibrationEntry *calibrationTable,
	char *summaryFilePrefix,
	int nBins, float xMin, float xMax
)
{
	// Allocate a dummy canvas, otherwise one will be created for fits
	// and print a message
	TCanvas *tmp0 = new TCanvas();
	
	char fName[1024];
	sprintf(fName, "%s.root", summaryFilePrefix);
	TFile *rootFile = new TFile(fName, "RECREATE");

	
	unsigned long gidStart = gAsicID * 64 * 4;
	unsigned long gidEnd = (gAsicID+1) * 64 * 4;
	unsigned long nQAC = gidEnd - gidStart;

	// Build the histograms
	TH2S **hFine2_list = new TH2S *[nQAC];
	for(int n = 0; n < nQAC; n++) {
		hFine2_list[n] = NULL;
	}
	
	for(unsigned gid = gidStart; gid < gidEnd; gid++) {
		unsigned tacID = (gid >> 0) % 4;
		unsigned channelID = (gid >> 2) % 64;
		unsigned chipID = (gid >> 8) % 64;
		unsigned slaveID = (gid >> 14) % 32;
		unsigned portID = (gid >> 19) % 32;
		char hName[128];
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_hFine2", portID, slaveID, chipID, channelID, tacID);
		hFine2_list[gid-gidStart] = new TH2S(hName, hName, nBins, xMin, xMax, 1024, 0, 1024);
	}
	
	struct timespec t0;
	clock_gettime(CLOCK_REALTIME, &t0);

	unsigned READ_BUFFER_SIZE = 32*1024*1024 / sizeof(CalibrationEvent);
	CalibrationEvent *eventBuffer = new CalibrationEvent[READ_BUFFER_SIZE];
	int r;
	bool asicPresent = false;
	lseek(dataFile, 0, SEEK_SET);
	while((r = read(dataFile, eventBuffer, sizeof(CalibrationEvent)*READ_BUFFER_SIZE)) > 0) {
		int nEvents = r / sizeof(CalibrationEvent);
		for(int i = 0; i < nEvents; i++) {
			CalibrationEvent &event = eventBuffer[i];
			assert(hFine2_list[event.gid-gidStart] != NULL);
			//if(event.fine < 0.5 * nominalM || event.fine > 4 * nominalM) continue;
			hFine2_list[event.gid-gidStart]->Fill(event.ti, event.qfine);
			asicPresent = true;
		}
	}
	
	struct timespec t1;
	clock_gettime(CLOCK_REALTIME, &t1);
	
	for(unsigned gid = gidStart; gid < gidEnd; gid++) {
		unsigned tacID = (gid >> 0) % 4;
		unsigned channelID = (gid >> 2) % 64;
		unsigned chipID = (gid >> 8) % 64;
		unsigned slaveID = (gid >> 14) % 32;
		unsigned portID = (gid >> 19) % 32;
		char hName[128];

		TH2S *hFine2 = hFine2_list[gid-gidStart];
		assert(hFine2 != NULL);
		
		if(hFine2->GetEntries() < 1000) {
			fprintf(stderr, "WARNING: Not enough data to calibrate. Skipping QAC (%02u %02d %02d %02d %u)\n",
					portID, slaveID, chipID, channelID, tacID);
			continue;
		}

		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_pFine", portID, slaveID, chipID, channelID, tacID);
		TProfile *pFine = hFine2->ProfileX(hName, 1, -1, "s");
		
		float yMin = FLT_MAX;
		float yMax = FLT_MIN;
		for(int i = 1; i < nBins+1; i++) {
			if(pFine->GetBinEntries(i) < 10) continue;
			float v = pFine->GetBinContent(i);
			if(v < yMin) yMin = v;
			if(v > yMax) yMax = v;
		}
		float xMin = pFine->GetBinCenter(pFine->FindFirstBinAbove(yMin));
		float xMax = pFine->GetBinCenter(pFine->FindFirstBinAbove(0.90 * yMax));
		
		pFine->Fit("pol4", "Q", "", xMin, xMax);
		TF1 *pol2 = pFine->GetFunction("pol4");
		if(pol2 == NULL) {
			fprintf(stderr, "WARNING: Could not make a fit. Skipping TAC (%02u %02d %02d %02d %u)\n",
				portID, slaveID, chipID, channelID, tacID);
			continue;
		}
		
		CalibrationEntry &entry = calibrationTable[gid];
		entry.p0 = pol2->GetParameter(0);
		entry.p1 = pol2->GetParameter(1);
		entry.p2 = pol2->GetParameter(2);
		entry.p3 = pol2->GetParameter(3);
		entry.p4 = pol2->GetParameter(4);
		entry.xMin = xMin;
		entry.xMax = xMax;
		entry.valid = true;
		
		
	}
	

	TProfile **pControlT_list = new TProfile *[nQAC];
	TH1S **hControlE_list = new TH1S *[nQAC];
	
	int ControlHistogramNBins = 512;
	float ControlHistogramRange = 0.5;
	for(unsigned long gid = gidStart; gid < gidEnd; gid++) {
		pControlT_list[gid-gidStart] = NULL;
		hControlE_list[gid-gidStart] = NULL;
		
		CalibrationEntry &entry = calibrationTable[gid];
		if(!entry.valid) continue;
		
		unsigned tacID = (gid >> 0) % 4;
		unsigned channelID = (gid >> 2) % 64;
		unsigned chipID = (gid >> 8) % 64;
		unsigned slaveID = (gid >> 14) % 32;
		unsigned portID = (gid >> 19) % 32;
		char hName[128];
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_control_Q", portID, slaveID, chipID, channelID, tacID);
		pControlT_list[gid-gidStart] = new TProfile(hName, hName, 128, 0, 128*4, "s");
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_control_E", portID, slaveID, chipID, channelID, tacID);
		
		int ControlHistogramNBins = 128;
		float ControlHistogramRange = 2.0;
		hControlE_list[gid-gidStart] = new TH1S(hName, hName, ControlHistogramNBins, -ControlHistogramRange, ControlHistogramRange);
	}
	
	lseek(dataFile, 0, SEEK_SET);
	while((r = read(dataFile, eventBuffer, sizeof(CalibrationEvent)*READ_BUFFER_SIZE)) > 0) {
		int nEvents = r / sizeof(CalibrationEvent);
		for (int i = 0; i < nEvents; i++) {
			CalibrationEvent &event = eventBuffer[i];
			assert(event.gid >= gidStart);
			assert(event.gid < gidEnd);

			CalibrationEntry &entry = calibrationTable[event.gid];
			if(!entry.valid) continue;
			
			if(event.ti < entry.xMin || event.ti > entry.xMax) continue;
			
			float qExpected =entry.p0
					+ entry.p1 * event.ti
					+ entry.p2 * event.ti * event.ti
					+ entry.p3 * event.ti * event.ti * event.ti
					+ entry.p4 * event.ti * event.ti * event.ti * event.ti;
			
			float qError = event.qfine - qExpected;
			
			
			TProfile *pControlT = pControlT_list[event.gid-gidStart];
			TH1S *hControlE = hControlE_list[event.gid-gidStart];
			pControlT->Fill(event.ti, qError);
			hControlE->Fill(qError);
		}
	}


	double maxCounts = 0;
	for(unsigned long gid = gidStart; gid < gidEnd; gid++) {
		CalibrationEntry &entry = calibrationTable[gid];
		if(!entry.valid) continue;

		TH1S *hControlE = hControlE_list[gid-gidStart];
		double counts = hControlE->GetEntries();
		maxCounts = (maxCounts > counts) ? maxCounts : counts;
	}

	TH1F *hCounts = new TH1F("hCounts", "", 64*4, 0, 64);
	for(unsigned long channelID = 0; channelID < 64; channelID++) {
		for(unsigned long tacID = 0; tacID < 4; tacID++) {
			unsigned long gid = gidStart | (channelID << 2) | tacID;
			CalibrationEntry &entry = calibrationTable[gid];
			if(!entry.valid) continue;

			TH1S *hControlE = hControlE_list[gid-gidStart];
			double counts = hControlE->GetEntries();
			hCounts->SetBinContent(1 + 4*channelID + tacID, counts);
		}
	}

	TCanvas *c = new TCanvas();
	c->Divide(2, 2);
	c->cd(1);
	hCounts->GetXaxis()->SetTitle("Channel");
	hCounts->GetYaxis()->SetRangeUser(0, maxCounts * 1.10);
	hCounts->Draw("HIST");

	sprintf(fName, "%s.pdf", summaryFilePrefix);
	c->SaveAs(fName);

	sprintf(fName, "%s.png", summaryFilePrefix);
	c->SaveAs(fName);
	delete c;

	rootFile->Write();
	delete rootFile;

	delete tmp0;	
}

void calibrateAllAsics(CalibrationEntry *calibrationTable, char *outputFilePrefix, int nBins, float xMin, float xMax)
{
	char fName[1024];
	sprintf(fName, "%s_list.tmp", outputFilePrefix);
	FILE *tmpListFile = fopen(fName, "r");
	if(tmpListFile == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
		exit(1);
	}
	
	int nCPU = sysconf(_SC_NPROCESSORS_ONLN);
	struct sysinfo si;
	sysinfo(&si);
	int maxWorkersByMem = si.totalram * si.mem_unit / (4LL * 1024*1024*1024);
	int maxWorkers = (nCPU < maxWorkersByMem) ? nCPU : maxWorkersByMem;
	
	unsigned long gAsicID;
	char tmpDataFileName[1024];
	int nWorkers = 0;
	while(fscanf(tmpListFile, "%lu %[^\n]\n", &gAsicID, tmpDataFileName) == 2) {
		int tmpDataFile = open(tmpDataFileName, O_RDONLY);
		if(tmpDataFile == -1) {
			fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
			exit(1);
		}
		
		if(fork() == 0) {
			// We are in child
			unsigned long chipID = gAsicID % 64;
			unsigned long slaveID = (gAsicID >> 6) % 32;
			unsigned long portID = (gAsicID >> 11) % 32;
			
			char summaryFilePrefix[1024];
			sprintf(summaryFilePrefix, "%s_%02d_%02d_%02d", outputFilePrefix, portID, slaveID, chipID);
			printf("Calibrating ASIC (%2d %2d %2d)\n", portID, slaveID, chipID);
			calibrateAsic(gAsicID, tmpDataFile, calibrationTable, summaryFilePrefix, nBins, xMin, xMax);
			exit(0);
		} else {
			nWorkers += 1;
		}
		
		while(nWorkers >= maxWorkers) {
			pid_t r = wait(NULL);
			if(r > 0) {
				nWorkers --;
			}
			else if (r < 0) {
				fprintf(stderr, "Unexpected error on %s:%d: %s\n", __FILE__ , __LINE__, strerror(errno));
				exit(1);
			}
		}
		
	}
	
	while(nWorkers > 0) {
		pid_t r = wait(NULL);
		if(r > 0) {
			nWorkers --;
		}
		else if (r < 0) {
			fprintf(stderr, "Unexpected error on %s:%d: %s\n", __FILE__ , __LINE__, strerror(errno));
			exit(1);
		}
	}	
}

void writeCalibrationTable(CalibrationEntry *calibrationTable, const char *outputFilePrefix)
{
	char fName[1024];
	sprintf(fName, "%s.tsv", outputFilePrefix);
	FILE *f = fopen(fName, "w");
	if(f == NULL) {
                fprintf(stderr, "Could not open '%s' for writing: %s\n", fName, strerror(errno));
                exit(1);
	}

	fprintf(f, "# portID\tslaveID\tchipID\tchannelID\ttacID\tp0\tp1\tp2\tp3\tp4\n");

	for(unsigned long gid = 0; gid < MAX_N_QAC; gid++) {
		CalibrationEntry &entry = calibrationTable[gid];
		if(!entry.valid) continue;
		unsigned tacID = (gid >> 0) % 4;
		unsigned channelID = (gid >> 2) % 64;
		unsigned chipID = (gid >> 8) % 64;
		unsigned slaveID = (gid >> 14) % 32;
		unsigned portID = (gid >> 19) % 32;
	
		fprintf(f, "%d\t%d\t%d\t%d\t%d\t%8.7e\t%8.7e\t%8.7e\t%8.7e\t%8.7e\n",
			portID, slaveID, chipID, channelID, tacID,
			entry.p0, entry.p1, entry.p2, entry.p3, entry.p4
		);
	}
	fclose(f);
}

void deleteTemporaryFiles(const char *outputFilePrefix) 
{
        char fName[1024];
        sprintf(fName, "%s_list.tmp", outputFilePrefix);
        FILE *tmpListFile = fopen(fName, "r");
        if(tmpListFile == NULL) {
                fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
                exit(1);
        }

        unsigned long gAsicID;
        char tmpDataFileName[1024];
        while(fscanf(tmpListFile, "%lu %[^\n]\n", &gAsicID, tmpDataFileName) == 2) {
		unlink(tmpDataFileName);
	}
	unlink(fName);
	fclose(tmpListFile);
}
