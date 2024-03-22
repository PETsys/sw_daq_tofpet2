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


void sortData(SystemConfig *config, char *inputFilePrefix1, char *inputFilePrefix2, char *outputFilePrefix);
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

	char *inputFilePrefix1 = NULL;
	char *inputFilePrefix2 = NULL;
	char *outputFilePrefix = NULL;
	bool doSorting = true;
	bool keepTemporary = false;
	
 
        static struct option longOptions[] = {
                { "help", no_argument, 0, 0 },
                { "config", required_argument, 0, 0 },
				{ "photopeak-list", optional_argument, 0, 0 },
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
			case 'i':	inputFilePrefix1 = optarg; break;
			case 'o':	outputFilePrefix = optarg; break;
			default:	displayUsage(); exit(1);
			}
		}
		else if(c == 0) {
			switch(optionIndex) {
			case 0: 	displayUsage(); exit(0); break;
			case 1:		configFileName = optarg; break;
			case 2:		inputFilePrefix2 = optarg; break;
			case 3:		doSorting = false; break;
			case 4:		keepTemporary = true; break;
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
	
	if(inputFilePrefix1 == NULL) {
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
		sortData(config, inputFilePrefix1, outputFilePrefix, inputFilePrefix2);
	}
	
	calibrateAllModules(config, calibrationTable, outputFilePrefix);

	writeCalibrationTable(calibrationTable, outputFilePrefix);
	if(!keepTemporary){
	 	deleteTemporaryFiles(outputFilePrefix);
	 }
	return 0;
}

class EventWriter {
private:
	char *outputFilePrefix;
	unsigned long maxgAsicID;
	double frequency;
	char **tmpDataFileNames1;
	FILE **tmpDataFiles1;
	
	char **tmpDataFileNames2;
	FILE **tmpDataFiles2;

	FILE *tmpListFile1;


	SystemConfig *config;

	vector<int> i3mFem256TimeChannels{35,36,33,34,31,32,29,30,52,55,54,57,56,59,58,61,92,75,90,74,89,77,87,85,80,84,86,88,83,82,81,91,21,19,17,18,15,16,8,0,2,4,6,14,10,12,13,11,69,66,67,64,
	                                  65,112,114,113,101,102,103,104,105,106,107,108,172,171,170,169,168,167,166,165,177,178,176,129,128,131,130,133,203,205,204,202,206,198, 196,194,192,200,
					  208,207,210,209,211,213,155,145,146,147,152,150,148,144,149,151,141,153,138,154,139,156,253,250,251,248,249,246,247,244,222,221,224,223,226,225,228,227};

	vector<int> i3mFem256EnergyChannels{44,45,42,40,43,38,39,37,27,28,26,24,25,22,20,23,190,191,189,188,187,175,174,173,163,161,140,160,159,142,157,143,47,49,46,48,41,50,51,53,60,63,62,1,3,5,7,
					   9,186,185,184,183,182,181,180,179,132,164,135,162,134,137,158,136,72,94,73,70,98,71,100,68,115,116,117,118,119,120,121,122,201,199,197,195,193,254,255,252,
					    245,243,242,233,240,238,241,239,79,93,78,95,96,76,97,99,109,110,111,123,124,125,127,126,215,212,214,217,216,218,220,219,229,231,230,235,232,234,237,236};

public:
	EventWriter(char *outputFilePrefix, char *inputPhotopeakFileName, double frequency, SystemConfig *config)
	{
		this->outputFilePrefix = outputFilePrefix;
		this->frequency = frequency;
		this->config = config;

		maxgAsicID = 0;	
		tmpDataFileNames1 = new char *[MAX_N_ASICS];
		tmpDataFiles1 = new FILE *[MAX_N_ASICS];

		tmpDataFileNames2 = new char *[MAX_N_ASICS];
		tmpDataFiles2 = new FILE *[MAX_N_ASICS];

		for(int n = 0; n < MAX_N_ASICS; n++)
		{
			tmpDataFileNames1[n] = NULL;
			tmpDataFiles1[n] = NULL;
			tmpDataFileNames2[n] = NULL;
			tmpDataFiles2[n] = NULL;
		}
		
		// Create the temporary list file
		char fName[1024];
		
		sprintf(fName, "%s_energyCal_event_list.tmp", outputFilePrefix);
		tmpListFile1 = fopen(fName, "w");
		if(tmpListFile1 == NULL) {
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
			
			FILE * f1 = tmpDataFiles1[gAsicID];

			if(f1 == NULL) {
				// We haven't opened this file yet.
				unsigned asicID = (gChannelID >> 6) % 64;
				unsigned slaveID = (gChannelID >> 12) % 32;
				unsigned portID = (gChannelID >> 17) % 32;
				char *fn1 = new char[1024];
		
				sprintf(fn1, "%s_%02u_%02u_%02u_energyCal_event_data.tmp", outputFilePrefix, portID, slaveID, asicID);				
				f1 = fopen(fn1, "w");
				if(f1 == NULL) {
					fprintf(stderr, "Could not open '%s' for reading: %s\n", fn1, strerror(errno));
					exit(1);
				}
				tmpDataFiles1[gAsicID] = f1;
				tmpDataFileNames1[gAsicID] = fn1;
			}

			for(int m = 0; m < p.nHits; m++) {

				Hit &h0 = *p.hits[0];
				Hit &h = *p.hits[m];
				if (std::find(i3mFem256EnergyChannels.begin(), i3mFem256EnergyChannels.end(), h.raw->channelID%256) != i3mFem256EnergyChannels.end()) {
					continue;
				}
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
				fwrite((void*)&e, sizeof(e), 1, f1);
			}
	
		}
	};
	
	void close()
	{
		for(unsigned long gAsicID = 0; gAsicID <= maxgAsicID; gAsicID++) {
			if(tmpDataFiles1[gAsicID] != NULL) {
				fprintf(tmpListFile1, "%lu %s\n", gAsicID, tmpDataFileNames1[gAsicID]);
				fclose(tmpDataFiles1[gAsicID]);
			}
			tmpDataFiles1[gAsicID] = NULL;
			delete [] tmpDataFileNames1[gAsicID];
		}
		fclose(tmpListFile1);
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

void sortData(SystemConfig *config, char *inputFilePrefix, char *outputFilePrefix, char *inputFilePrefix2)
{
	
	RawReader *reader = RawReader::openFile(inputFilePrefix);
	assert(!reader->isTOT());
	EventWriter *eventWriter;
	if(inputFilePrefix2 != NULL){
		eventWriter = new EventWriter(outputFilePrefix, inputFilePrefix2, reader->getFrequency(), config);
	}
	else{
		eventWriter = new EventWriter(outputFilePrefix, NULL, reader->getFrequency(), config);
	}
	

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
		   int dataFile1, int dataFile2,
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


	vector<int> i3mFem256TimeChannels{35,36,33,34,31,32,29,30,52,55,54,57,56,59,58,61,92,75,90,74,89,77,87,85,80,84,86,88,83,82,81,91,21,19,17,18,15,16,8,0,2,4,6,14,10,12,13,11,69,66,67,64,
	                                  65,112,114,113,101,102,103,104,105,106,107,108,172,171,170,169,168,167,166,165,177,178,176,129,128,131,130,133,203,205,204,202,206,198, 196,194,192,200,
					  208,207,210,209,211,213,155,145,146,147,152,150,148,144,149,151,141,153,138,154,139,156,253,250,251,248,249,246,247,244,222,221,224,223,226,225,228,227};

	vector<int> i3mFem256EnergyChannels{44,45,42,40,43,38,39,37,27,28,26,24,25,22,20,23,190,191,189,188,187,175,174,173,163,161,140,160,159,142,157,143,47,49,46,48,41,50,51,53,60,63,62,1,3,5,7,
					    9,186,185,184,183,182,181,180,179,132,164,135,162,134,137,158,136,72,94,73,70,98,71,100,68,115,116,117,118,119,120,121,122,201,199,197,195,193,254,255,252,
					   245,243,242,233,240,238,241,239,79,93,78,95,96,76,97,99,109,110,111,123,124,125,127,126,215,212,214,217,216,218,220,219,229,231,230,235,232,234,237,236};


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

	lseek(dataFile1, 0, SEEK_SET);
	while((r = read(dataFile1, eventBuffer, sizeof(EnergyEmpiricalCalibrationEvent)*READ_BUFFER_SIZE)) > 0) {
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
		CalibrationEntry &entry = calibrationTable[gid];

		if (std::find(i3mFem256EnergyChannels.begin(), i3mFem256EnergyChannels.end(), gChannelID%256) != i3mFem256EnergyChannels.end()){
			entry.p0 = 0;
			entry.p1 = 0;
			entry.p2 = 0;
			entry.chi2_E = 0;
			entry.valid = true;
			continue;
		}	

		TProfile *profileTAC  = hEnergy_vs_efine[gid-gTacStart]->ProfileX();
	
		assert(profileTAC!= NULL);
			
		if(profileTAC->GetEntries() < 3000){
			fprintf(stderr, "WARNING: Not enough data to calibrate empirical QDC. Skipping Calibration of TAC (%02u %02u %02u %02u %02u)\n",
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
		
		Int_t binHigh = hEnergy_vs_efine[gid-gTacStart]->FindLastBinAbove(0.5,1);
		
		int binDist = 0;
		bool found = false;
		for (int binX = binHigh-1; binX > binHigh-100; binX--){
			for (int binY = 1; binY < hEnergy_vs_efine[gid-gTacStart]->GetNbinsY(); binY++) {
				if(hEnergy_vs_efine[gid-gTacStart]->GetBinContent(binX,binY)!= 0){
					found = true;
					break;
				}				
			}
			if(found) break;
			binDist++;
		}

		int maxRange = binDist > 10 ?  binHigh-binDist : binHigh;

	
		hEnergy_vs_efine[gid-gTacStart]->Fit("pol2","FQ","",float(binLow+1),float(maxRange));
		TF1 *fitFunc = hEnergy_vs_efine[gid-gTacStart]->GetFunction("pol2");

		entry.p0 = fitFunc->GetParameter(0);
		entry.p1 = fitFunc->GetParameter(1);
		entry.p2 = fitFunc->GetParameter(2);
		entry.chi2_E = fitFunc->GetChisquare()/fitFunc->GetNDF();
		entry.valid = true;
	}

	lseek(dataFile1, 0, SEEK_SET);
	while((r = read(dataFile1, eventBuffer, sizeof(EnergyEmpiricalCalibrationEvent)*READ_BUFFER_SIZE)) > 0) {
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
	
		if (std::find(i3mFem256EnergyChannels.begin(), i3mFem256EnergyChannels.end(), gChannelID%256) != i3mFem256EnergyChannels.end()){
			entry.k0 = 0;
			entry.valid = true;
			entry.chi2_T = 0;
			continue;
		}	
		if(hTime_vs_energy[gid-gTacStart]->GetEntries() < 3000){
			fprintf(stderr, "WARNING: Not enough data to calibrate for time walk. Skipping Calibration of TAC (%02u %02u %02u %02u %02u)\n",
					portID, slaveID, asicID, channelID, tacID);
			continue;
		}

		char hName[128];
		
		TF1 *fitFunc1 = new TF1("TimeWalkFitFunction", TimeWalkFitFunction, 0, 50, 1);
		
		fitFunc1->SetParameter(0,20);
	 
		hTime_vs_energy[gid-gTacStart]->Fit(fitFunc1,"Q","",0.2, 20);
		
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
	FILE *tmpListFile1 = fopen(fName, "r");
	if(tmpListFile1 == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
		exit(1);
	}

    struct tmp_entry_t {
		unsigned long gAsicID;
		string fileName;
	};
    
    list<tmp_entry_t> tmp_list1;

	unsigned long gAsicID1;

    off_t max_tmp_file_size;
	off_t max_tmp_file_size1 = 0;

    struct stat tmp_file_stat1;
        
	rewind(tmpListFile1);
    
	while(fscanf(tmpListFile1, "%lu %[^\n]\n", &gAsicID1, fName) == 2) {
		int r = stat(fName,  &tmp_file_stat1);
		if (r == 0) {
			if(tmp_file_stat1.st_size > max_tmp_file_size1) max_tmp_file_size1 = tmp_file_stat1.st_size;
		}

		tmp_entry_t tmp_entry1 = { gAsicID1, string(fName) };
		tmp_list1.push_back(tmp_entry1);
	}
	fclose(tmpListFile1);

	max_tmp_file_size = max_tmp_file_size1 ;
	
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
     
	for( auto it1 = tmp_list1.begin(); it1 != tmp_list1.end(); it1++) {
	
		int tmpDataFile1 = open(it1->fileName.c_str(), O_RDONLY);
                
		if(tmpDataFile1 == -1) {
			fprintf(stderr, "Could not open '%s' for reading: %s\n", it1->fileName.c_str(), strerror(errno));
			exit(1);
		}

		if(fork() == 0) {
			// We are in child		
			unsigned long gAsicID = it1->gAsicID;
			int asicID = gAsicID % 64;
			int slaveID = (gAsicID >> 6) % 32;
			int portID = (gAsicID >> 11) % 32;
			
			char summaryFilePrefix[1024];
			sprintf(summaryFilePrefix, "%s_%02d_%02d_%02d", outputFilePrefix, portID, slaveID, asicID);
			printf("Processing ASIC (%2d %2d %2d)\n", portID, slaveID, asicID);
			fflush(stdout);
		   
			calibrateAsic(gAsicID, tmpDataFile1, -1, calibrationTable, summaryFilePrefix, config);
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

	for(unsigned long gid = 0; gid < MAX_N_TACS; gid++){
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
		if(entry.chi2_E > 0 && entry.chi2_E < 1000 && entry.chi2_T > 0 &&  entry.chi2_T < 5E6){
			h_chi2_T->Fill(entry.chi2_T);
			h_chi2_E->Fill(entry.chi2_E);
			if( entry.chi2_E < 1000 &&  entry.chi2_T < 5E6){
				count++;
			}
		}
	}
	printf("\nReport: Calibration validated for %d TACS, %d channels\n\n", count, count/4);

	fclose(f);
	rootFile->Write();
	delete rootFile;
}

void deleteTemporaryFiles(const char *outputFilePrefix) 
{
   
	char fName[1024];
	sprintf(fName, "%s_energyCal_event_list.tmp", outputFilePrefix);
	FILE *tmpListFile1 = fopen(fName, "r");
	if(tmpListFile1 == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
		exit(1);
	}
	unsigned long gAsicID;
    char tmpDataFileName1[1024];
    while(fscanf(tmpListFile1, "%lu %[^\n]\n", &gAsicID, tmpDataFileName1) == 2) {
		unlink(tmpDataFileName1);
	}
	unlink(fName);
	fclose(tmpListFile1);	


	sprintf(fName, "%s_energyCal_event_list.tmp", outputFilePrefix);
	FILE *tmpListFile2 = fopen(fName, "r");
	if(tmpListFile2 != NULL) {
		char tmpDataFileName2[1024];
		while(fscanf(tmpListFile2, "%lu %[^\n]\n", &gAsicID, tmpDataFileName2) == 2) {
			unlink(tmpDataFileName2);
		}
		unlink(fName);
		fclose(tmpListFile2);	
	}
}
