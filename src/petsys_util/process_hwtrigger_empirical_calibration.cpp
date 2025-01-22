#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <list>
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
#include <algorithm>

#include <RawReader.hpp>
#include <SystemConfig.hpp>
#include <CoarseSorter.hpp>
#include <ProcessHit.hpp>
#include <SimpleGrouper.hpp>
#include <OrderedEventHandler.hpp>

#include <boost/random.hpp>
#include <boost/nondet_random.hpp>
#include <boost/regex.hpp>

#include <TF1.h>
#include <TH1S.h>
#include <TH1D.h>
#include <TH2S.h>
#include <TProfile.h>
#include <TGraphErrors.h>
#include <TFile.h>
#include <TCanvas.h>
#include <TSpectrum.h>
#include <TStyle.h>
#include <TGraphErrors.h>
#include <TError.h>
#include <TROOT.h>

using namespace std;
using namespace PETSYS;

template <typename T>
vector<size_t> sort_indexes(const vector<T> &v) {
  
  vector<size_t> idx(v.size());
  iota(idx.begin(), idx.end(), 0);

  stable_sort(idx.begin(), idx.end(),
       [&v](size_t i1, size_t i2) {return v[i1] < v[i2];});

  return idx;
}

struct EnergyEmpiricalCalibrationEvent{
	int gTacID;
	float energy;
	unsigned short eFine;
	double time;
};

struct GainAdjustCalibrationEvent{
	float energy;
	int gChannelID;
};

// TODO Put this somewhere else
const unsigned long MAX_N_ASICS = 32*32*32;
const unsigned long MAX_N_CHANNELS = MAX_N_ASICS * 64;
const unsigned long MAX_N_TACS = MAX_N_CHANNELS * 4;


struct CalibrationEntry {
	float p0;
	float p1;
	float p2;
	float k0;
	float chi2_E;
	float chi2_T;
	bool valid;
};

void sortData(SystemConfig *config, char *inputFilePrefix, char *outputFilePrefix);
void calibrateAllModules(SystemConfig *config, CalibrationEntry *calibrationTable, char *outputFilePrefix);
void writeCalibrationTable(CalibrationEntry *calibrationTable, const char *outputFilePrefix);
void deleteTemporaryFiles(const char *outputFilePrefix);

void displayUsage() {
}

static void normalizeLine(char *line) {
	std::string s = std::string(line);
	s = boost::regex_replace(s, boost::regex("\r"), "");
	s = boost::regex_replace(s, boost::regex("\\s*#.*"), "");
	s = boost::regex_replace(s, boost::regex("^\\s+"), "");
	s = boost::regex_replace(s, boost::regex("\\s+$"), "");
	s = boost::regex_replace(s, boost::regex("\\s+"), "\t");
	strcpy(line, s.c_str());
}
int main(int argc, char *argv[])
{
	//gErrorIgnoreLevel = 2001;
	char *configFileName = NULL;

	char *inputFilePrefix = NULL;
	char *outputFilePrefix = NULL;
	bool doSorting = true;
	bool keepTemporary = false;
	
 
        static struct option longOptions[] = {
                { "help", no_argument, 0, 0 },
                { "config", required_argument, 0, 0 },
                { "no-sorting", no_argument, 0, 0 },
                { "keep-temporary", no_argument, 0, 0 }
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
			default:	displayUsage(); exit(1);
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
	   
	if(outputFilePrefix== NULL) {
		fprintf(stderr, "-o must be specified\n");
		exit(1);
	}

	unsigned long long mask = SystemConfig::LOAD_ALL;
	mask ^= (SystemConfig::LOAD_ENERGY_CALIBRATION | SystemConfig::LOAD_TIMEALIGN_CALIBRATION | SystemConfig::LOAD_FIRMWARE_EMPIRICAL_CALIBRATIONS);
	SystemConfig *config = SystemConfig::fromFile(configFileName, mask);
	
	char fName[1024];
	
	CalibrationEntry *calibrationTable = (CalibrationEntry *)mmap(NULL, sizeof(CalibrationEntry)*MAX_N_TACS, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	for(int gid = 0; gid < MAX_N_TACS; gid++) calibrationTable[gid].valid = false;

	if(doSorting) {
		sortData(config, inputFilePrefix, outputFilePrefix);
	}
	
	calibrateAllModules(config, calibrationTable, outputFilePrefix);

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
	double frequency;
	char **tmpDataFileNames;
	FILE **tmpDataFiles;
	

	FILE *tmpListFile;

	bool doGainAdjust;

	SystemConfig *config;

public:
	EventWriter(char *outputFilePrefix, double frequency, SystemConfig *config)
	{
		this->outputFilePrefix = outputFilePrefix;
		this->doGainAdjust = doGainAdjust;
		this->frequency = frequency;
		this->config = config;

		maxgAsicID = 0;	
		tmpDataFileNames = new char *[MAX_N_ASICS];
		tmpDataFiles = new FILE *[MAX_N_ASICS];



		for(int n = 0; n < MAX_N_ASICS; n++)
		{
			tmpDataFileNames[n] = NULL;
			tmpDataFiles[n] = NULL;
		}
		
		// Create the temporary list file
		char fName[1024];
		
		sprintf(fName, "%s_energyCal_event_list.tmp", outputFilePrefix);
		tmpListFile = fopen(fName, "w");
		if(tmpListFile == NULL) {
			fprintf(stderr, "Could not open '%s' for writing: %s\n", fName, strerror(errno));
			exit(1);
		}
	};

	void handleEvents(EventBuffer<GammaPhoton> *buffer)
	{
		double Tps = 1E12/frequency;
		long long tMin = buffer->getTMin() * (long long)Tps;

		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
		
			GammaPhoton &p = buffer->get(i);
			if(!p.valid) continue;
			
			Hit &h0 = *p.hits[0];
	        	
			unsigned gChannelID = h0.raw->channelID;
			int gAsicID = int(h0.raw->channelID / 64);

			maxgAsicID = (maxgAsicID > gAsicID) ? maxgAsicID : gAsicID;
			
			FILE * f = tmpDataFiles[gAsicID];

			if(f == NULL) {
				// We haven't opened this file yet.
				unsigned asicID = (gChannelID >> 6) % 64;
				unsigned slaveID = (gChannelID >> 12) % 32;
				unsigned portID = (gChannelID >> 17) % 32;
				char *fn = new char[1024];
		
				sprintf(fn, "%s_%02u_%02u_%02u_energyCal_event_data.tmp", outputFilePrefix, portID, slaveID, asicID);				
				f = fopen(fn, "w");
				if(f == NULL) {
					fprintf(stderr, "Could not open '%s' for reading: %s\n", fn, strerror(errno));
					exit(1);
				}
				tmpDataFiles[gAsicID] = f;
				tmpDataFileNames[gAsicID] = fn;
			}

			for(int m = 0; m < p.nHits; m++) {
				Hit &h0 = *p.hits[0];
				Hit &h = *p.hits[m];
				EnergyEmpiricalCalibrationEvent e;
				e.gTacID = int(h.raw->channelID)*4 + int(h.raw->tacID);
				e.energy = h.energy;
				e.eFine = h.raw->efine;
				if(m>0) {
					e.time = h.time - h0.time;
				}
				else{
					e.time = -1E10;
				}
				fwrite((void*)&e, sizeof(e), 1, f);
			}
	
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



class WriteHelper : public OrderedEventHandler<GammaPhoton, GammaPhoton> {
private: 
	EventWriter *eventWriter;
public:
	WriteHelper(EventWriter *eventWriter, EventSink<GammaPhoton> *sink) :
		OrderedEventHandler<GammaPhoton, GammaPhoton>(sink),
		eventWriter(eventWriter)
	{
	};
	
	EventBuffer<GammaPhoton> * handleEvents(EventBuffer<GammaPhoton> *buffer) {
		eventWriter->handleEvents(buffer);
		return buffer;
	};
};

void sortData(SystemConfig *config, char *inputFilePrefix, char *outputFilePrefix)
{
	RawReader::timeref_t tb = RawReader::SYNC;
	RawReader *reader = RawReader::openFile(inputFilePrefix, tb);
	assert(!reader->isTOT());
	EventWriter *eventWriter;
	
	eventWriter = new EventWriter(outputFilePrefix, reader->getFrequency(), config);
	

	while(reader->getNextStep()) {
		
		reader->processStep(true,
			   new CoarseSorter(    
			   new ProcessHit(config, reader,
			   new SimpleGrouper(config,	       
			   new WriteHelper(eventWriter,
			   new NullSink<GammaPhoton>()
			   )))));

	eventWriter->close();
	}
	delete reader;
}
static Double_t TimeWalkFitFunction(Double_t *xx, Double_t *pp)
{
	Double_t Q = xx[0];	
	Double_t time = 0.06 + pp[0]/(Q);
	return time;
}


void calibrateAsic(
		   unsigned long gAsicID,
		   int dataFile,
		   CalibrationEntry *calibrationTable,
		   char *summaryFilePrefix,
		   SystemConfig *config
)
{
	
	TCanvas *tmp0 = new TCanvas();
	
	char fName[1024];
	sprintf(fName, "%s.root", summaryFilePrefix);
	TFile *rootFile = new TFile(fName, "RECREATE");

	unsigned long nTacs = 256;
	unsigned long nChannels = 64;
	
	unsigned long gTacStart = gAsicID * nTacs;
	unsigned long gTacEnd = (gAsicID+1) * nTacs;
	
	unsigned long gChannelStart = gAsicID * nChannels;
	unsigned long gChannelEnd = (gAsicID+1) * nChannels;
		
	TH2F **hEnergy_vs_efine = new TH2F *[nTacs];
	TH2F **hTime_vs_energy = new TH2F *[nTacs];
	
	TH1D **hTotalEnergy = new TH1D *[nChannels];

	for(int n = 0; n < nTacs ; n++) {
		hEnergy_vs_efine[n] = NULL;	
	}
	
	for(unsigned gid = gTacStart; gid < gTacEnd; gid++) {
		unsigned tacID = (gid >> 0) % 4;
		unsigned channelID = (gid >> 2) % 64;
		unsigned asicID = (gid >> 8) % 64;
		unsigned slaveID = (gid >> 14) % 32;
		unsigned portID = (gid >> 19) % 32;
				
		char hName[128];
	
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%02d_hEnergy_vs_efine", portID, slaveID, asicID, channelID, tacID);
		hEnergy_vs_efine[gid-gTacStart] = new TH2F(hName, hName, 500, 0, 500, 250, 0, 50);
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%02d_hTime_vs_energy", portID, slaveID, asicID, channelID, tacID);
		hTime_vs_energy[gid-gTacStart] = new TH2F(hName, hName, 100, 0, 50, 2000, 0, 2.);
	}

	for(int n = 0; n < nChannels ; n++) {
		hTotalEnergy[n] = NULL;	
	}
	
	for(unsigned gid = gChannelStart; gid < gChannelEnd; gid++) {
		
		unsigned channelID = (gid >> 0) % 64;
		unsigned asicID = (gid >> 6) % 64;
		unsigned slaveID = (gid >> 12) % 32;
		unsigned portID = (gid >> 17) % 32;
				
		char hName[128];

		sprintf(hName, "c_%02d_%02d_%02d_%02d_hGroupEnergy", portID, slaveID, asicID, channelID);
		hTotalEnergy[gid-gChannelStart] = new TH1D(hName, hName, 600, 0, 120);
	}
	
	unsigned READ_BUFFER_SIZE = 32*1024*1024 / sizeof(EnergyEmpiricalCalibrationEvent);
	EnergyEmpiricalCalibrationEvent *eventBuffer = new EnergyEmpiricalCalibrationEvent[READ_BUFFER_SIZE];
	int r;

	lseek(dataFile, 0, SEEK_SET);
	while((r = read(dataFile, eventBuffer, sizeof(EnergyEmpiricalCalibrationEvent)*READ_BUFFER_SIZE)) > 0) {
		int nEvents = r / sizeof(EnergyEmpiricalCalibrationEvent);
		for(int i = 0; i < nEvents; i++) {
			EnergyEmpiricalCalibrationEvent &event = eventBuffer[i];
						
			if(event.gTacID-gTacStart >= nTacs){
				continue;
			}
			if(event.gTacID-gTacStart < 0){
				continue;
			}
		
			assert(hEnergy_vs_efine[event.gTacID-gTacStart] != NULL);
			hEnergy_vs_efine[event.gTacID-gTacStart]->Fill(event.eFine, event.energy);
		}
	}
	
	for(unsigned gid = gTacStart; gid < gTacEnd; gid++) {
	
		unsigned tacID = (gid >> 0) % 4;
		unsigned gChannelID = (gid >> 2);
		unsigned channelID = (gid >> 2) % 64;
		unsigned asicID = (gid >> 8) % 64;
		unsigned slaveID = (gid >> 14) % 32;
		unsigned portID = (gid >> 19) % 32;			

		char hName[128];

		TProfile *profileTAC  = hEnergy_vs_efine[gid-gTacStart]->ProfileX("_pfx", 1, -1, "s");
	
		assert(profileTAC!= NULL);

		double fixedError = 0;  

		CalibrationEntry &entry = calibrationTable[gid];
		entry.p0 = 0;
		entry.p1 = 0;
		entry.p2 = 0;
		entry.chi2_E = 0;	
		
		if(profileTAC->GetEntries() < 5000) {
			fprintf(stderr, "WARNING: Not enough data to calibrate empirical charge. Skipping Calibration of TAC (%02u %02d %02d %02d %02d)\n",
					portID, slaveID, asicID, channelID, tacID);
			continue;
		}

		Int_t binLow = profileTAC->FindFirstBinAbove(0);

		for (int bin = binLow + 20 ; bin > binLow ; bin--){
			if( profileTAC->GetBinContent(bin-1) > profileTAC->GetBinContent(bin)){
				binLow = bin;
				continue;
			}	  
		}
		
		Int_t binHigh = profileTAC->FindLastBinAbove(1);

		profileTAC->Fit("pol2","Q","",float(binLow+2),float(binHigh));
		TF1 *fitFunc = profileTAC->GetFunction("pol2");

		entry.p0 = fitFunc->GetParameter(0);
		entry.p1 = fitFunc->GetParameter(1);
		entry.p2 = fitFunc->GetParameter(2);
		
		entry.chi2_E = fitFunc->GetChisquare()/fitFunc->GetNDF();
		entry.valid = true;
	}

	lseek(dataFile, 0, SEEK_SET);
	while((r = read(dataFile, eventBuffer, sizeof(EnergyEmpiricalCalibrationEvent)*READ_BUFFER_SIZE)) > 0) {
		int nEvents = r / sizeof(EnergyEmpiricalCalibrationEvent);
		for(int i = 0; i < nEvents; i++) {
			EnergyEmpiricalCalibrationEvent &event = eventBuffer[i];
							
			if(event.gTacID-gTacStart >= nTacs){
				continue;
			}
			if(event.gTacID-gTacStart < 0){
				continue;
			}

			assert(hTime_vs_energy[event.gTacID-gTacStart] != NULL);

			if(event.time != -1E10){
				hTime_vs_energy[event.gTacID-gTacStart]->Fill(event.energy, event.time);
			}		
		}
	}

	for(unsigned gid = gTacStart; gid < gTacEnd; gid++) {
	
		CalibrationEntry &entry = calibrationTable[gid];
		entry.valid = false;
		unsigned tacID = (gid >> 0) % 4;
		unsigned gChannelID = (gid >> 2);
		unsigned channelID = (gid >> 2) % 64;
		unsigned asicID = (gid >> 8) % 64;
		unsigned slaveID = (gid >> 14) % 32;
		unsigned portID = (gid >> 19) % 32;			

		char hName[128];

		TProfile *profileEnergy  = hTime_vs_energy[gid-gTacStart]->ProfileX("_pfx", 1, -1, "");
	
		if(hTime_vs_energy[gid-gTacStart]->GetEntries() < 1500){
			fprintf(stderr, "WARNING: Not enough data to calibrate for time walk. Skipping Calibration of TAC (%02u %02u %02u %02u %02u)\n",
					portID, slaveID, asicID, channelID, tacID);
			continue;
		}
		
		TF1 *fitFunc1 = new TF1("TimeWalkFitFunction", TimeWalkFitFunction, 0, 50, 1);
		
		fitFunc1->SetParameter(0,1);
	 
		hTime_vs_energy[gid-gTacStart]->Fit(fitFunc1,"Q","",0.2, 20);
		profileEnergy->Fit(fitFunc1,"Q","",0.2, 20);
		
		entry.k0 = fitFunc1->GetParameter(0);
		entry.chi2_T = fitFunc1->GetChisquare()/fitFunc1->GetNDF();
		

		entry.valid = true;
	}

	

	rootFile->Write();
	delete rootFile;
	delete tmp0;	
}

void calibrateAllModules(SystemConfig *config, CalibrationEntry *calibrationTable, char *outputFilePrefix)
{
	char fName[1024];
	char fName2[1024];
	sprintf(fName, "%s_energyCal_event_list.tmp", outputFilePrefix);
	FILE *tmpListFile = fopen(fName, "r");
	if(tmpListFile == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
		exit(1);
	}
	

    struct tmp_entry_t {
		unsigned long gAsicID;
		string fileName;
	};
    
    list<tmp_entry_t> tmp_list;
	
	unsigned long gAsicID;

    off_t max_tmp_file_size;
	off_t max_tmp_file_size1 = 0;

    struct stat tmp_file_stat;


	rewind(tmpListFile);


	while(fscanf(tmpListFile, "%lu %[^\n]\n", &gAsicID, fName) == 2) {
		int r = stat(fName,  &tmp_file_stat);
		if (r == 0) {
			if(tmp_file_stat.st_size > max_tmp_file_size) max_tmp_file_size = tmp_file_stat.st_size;
		}

		tmp_entry_t tmp_entry = { gAsicID, string(fName) };
		tmp_list.push_back(tmp_entry);

	}
	fclose(tmpListFile);

	//max_tmp_file_size = max_tmp_file_size1 + max_tmp_file_size2; //+ max_tmp_file_size3;
	
	int nCPU = sysconf(_SC_NPROCESSORS_ONLN);
	
	/*
	 * Restrict number of worker processed based on temporary file size and available RAM.
	 * The calibration process needs to read the temporary file size multiple times
	 * which is very slow if the system does not have enough RAM to cache the files
	 */
	struct sysinfo si;
	sysinfo(&si);
	int maxWorkersByMem = si.totalram * si.mem_unit / (1LL * 1024*1024*1024 + max_tmp_file_size);
	int maxWorkers = (nCPU < maxWorkersByMem) ? nCPU : maxWorkersByMem;

	// Ensure we have at least one worker or the software does not run properly
	maxWorkers = maxWorkers > 1 ? maxWorkers : 1;
	
	int nWorkers = 0;
       //auto it2 = tmp_list2.begin();
     
	for( auto it = tmp_list.begin(); it != tmp_list.end(); it++) {
	
		int tmpDataFile = open(it->fileName.c_str(), O_RDONLY);
	
		//int tmpDataFile2 = open(it->fileName.c_str(), O_RDONLY);
                
		if(tmpDataFile == -1) {
			fprintf(stderr, "Could not open '%s' for reading: %s\n", it->fileName.c_str(), strerror(errno));
			exit(1);
		}
	
			
		if(fork() == 0) {
			// We are in child
			
			unsigned long gAsicID = it->gAsicID;
			unsigned long asicID = gAsicID % 64;
			unsigned long slaveID = (gAsicID >> 6) % 32;
			unsigned long portID = (gAsicID >> 11) % 32;
			
			char summaryFilePrefix[1024];
			sprintf(summaryFilePrefix, "%s_%02d_%02d_%02d", outputFilePrefix, portID, slaveID, asicID);
			printf("Processing ASIC (%2d %2d %2d)\n", portID, slaveID, asicID);
            fflush(stdout);
			calibrateAsic(gAsicID, tmpDataFile, calibrationTable, summaryFilePrefix, config);
		
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

	sprintf(fName, "%s.root", outputFilePrefix);
	TFile *rootFile = new TFile(fName, "RECREATE");

	TH1F *h_p0 = new TH1F("h_p0", "h_p0", 2000, -500, 500);
	TH1F *h_p1 = new TH1F("h_p1", "h_p1", 1000, -20, 20);
	TH1F *h_p2 = new TH1F("h_p2", "h_p2", 5000, -1, 1);
	TH1F *h_k0 = new TH1F("h_k0", "h_k0", 1000, 0, 10);

	TH1F *h_chi2_T = new TH1F("h_chi2_T", "h_chi2_T", 50000, 1, 1E7);
	TH1F *h_chi2_E = new TH1F("h_chi2_E", "h_chi2_E", 500, 1, 1000);


	fprintf(f, "# portID\tslaveID\tasicID\tchannelID\ttacID\tp0\tp1\tp2\tk0\n");

	int count = 0;

	for(unsigned long gid = 0; gid < MAX_N_TACS; gid++) {
		CalibrationEntry &entry = calibrationTable[gid];
		if(!entry.valid) continue;
		unsigned tacID = (gid >> 0) % 4;
		unsigned channelID = (gid >> 2) % 64;
		unsigned asicID = (gid >> 8) % 64;
		unsigned slaveID = (gid >> 14) % 32;
		unsigned portID = (gid >> 19) % 32;
		
		fprintf(f, "%d\t%d\t%d\t%d\t%d\t%8.7e\t%8.7e\t%8.7e\t%8.7e\n",
			portID, slaveID, asicID, channelID, tacID,
			entry.p0, entry.p1, entry.p2, entry.k0);

		h_p0->Fill(entry.p0);
		h_p1->Fill(entry.p1);
		h_p2->Fill(entry.p2);
		h_k0->Fill(entry.k0);

		if(entry.chi2_E > 0 && entry.chi2_E < 20 && entry.chi2_T > 0 &&  entry.chi2_T < 1E7){
			h_chi2_T->Fill(entry.chi2_T);
			h_chi2_E->Fill(entry.chi2_E);
			//if( entry.chi2_E < 1000 &&  entry.chi2_T < 5E6)
			//{
			count++;
				//}
		}
	}

	printf("\nReport: Calibration obtained for %d TACS, %d channels\n\n", count, count/4);

	fclose(f);
	rootFile->Write();
	delete rootFile;
}

void deleteTemporaryFiles(const char *outputFilePrefix) 
{
   
	char fName[1024];
	sprintf(fName, "%s_energyCal_event_list.tmp", outputFilePrefix);
	FILE *tmpListFile = fopen(fName, "r");
	if(tmpListFile == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
		exit(1);
	}
	unsigned long gAsicID;
    char tmpDataFileName1[1024];
    while(fscanf(tmpListFile, "%lu %[^\n]\n", &gAsicID, tmpDataFileName1) == 2) {
		unlink(tmpDataFileName1);
	}
	unlink(fName);
	fclose(tmpListFile);	
}
