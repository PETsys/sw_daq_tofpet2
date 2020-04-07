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

#include <shm_raw.hpp>
#include <event_decode.hpp>
#include <SystemConfig.hpp>


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

#define BUFFER_SIZE	4096

struct RawCalibrationData{
	uint64_t eventWord;
	int freq;
};

struct CalibrationData{
	unsigned long gid;
	unsigned short tcoarse;
	unsigned short tfine;
	unsigned short ecoarse;
	unsigned short qfine;	
	int freq;
	
	float getTime (SystemConfig *config){
		float time, q_T;
		unsigned channelID = (gid >> 2);
		unsigned tacID = (gid >> 0) % 4;
		SystemConfig::ChannelConfig &cc = config->getChannelConfig(channelID);
		SystemConfig::TacConfig &ct = cc.tac_T[tacID];
		float delta = (ct.a1 * ct.a1) - (4.0f * (ct.a0 - tfine) * ct.a2);
		if(delta<0){
			time = tcoarse;
		}
		else{
			q_T =  ( -ct.a1 + sqrtf(delta) ) / (2.0f * ct.a2) ;	
		        time = tcoarse - q_T - ct.t0;
		}
		return time;
	};
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
        float p5;
	float p6;
	float p7;
	float p8;
        float p9;
        float xMin;
	float xMax;
	bool valid;
};



void sortData(char *inputFilePrefix, char *tmpFilePrefix);
void calibrateAsic(SystemConfig *config, unsigned long gAsicID, int dataFile, CalibrationEntry *calibrationTable, char *summaryFilePrefix, int nBins, float xMin, float xMax);

void calibrateAllAsics(SystemConfig *config, CalibrationEntry *calibrationTable, char *outputFilePrefix, int nBins, float xMin, float xMax, char *tmpFilePrefix);
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
	char *tmpFilePrefix = NULL;
	bool doSorting = true;
	bool keepTemporary = false;

        static struct option longOptions[] = {
                { "help", no_argument, 0, 0 },
                { "config", required_argument, 0, 0 },
                { "no-sorting", no_argument, 0, 0 },
                { "keep-temporary", no_argument, 0, 0 },
		{ "tmp-prefix", required_argument, 0, 0 }
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
			case 4:		tmpFilePrefix = optarg; break;
			default:	displayUsage(); exit(1);
			}
		}
		else {
			assert(false);
		}

	}
	if(tmpFilePrefix == NULL)
		tmpFilePrefix = outputFilePrefix;
		
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
		sortData(inputFilePrefix, tmpFilePrefix);
	}
 	calibrateAllAsics(config, calibrationTable, tmpFilePrefix, nBins, xMin, xMax, tmpFilePrefix);

	writeCalibrationTable(calibrationTable, outputFilePrefix);
	if(!keepTemporary) {
		deleteTemporaryFiles(tmpFilePrefix);
	}

	return 0;
}


void sortData(char *inputFilePrefix, char *tmpFilePrefix)
{
	
	printf("Sorting data into temporary files...\n");
	fflush(stdout);
	int maxChannelsPerWorker = 64;
	int maxWorkFiles = int(ceil(float(MAX_N_ASIC*64)/maxChannelsPerWorker));
	
	char **tmpDataFileNames = new char *[maxWorkFiles];
	FILE **tmpDataFiles = new FILE *[maxWorkFiles];
	for(int n = 0; n < maxWorkFiles; n++)
	{
		tmpDataFileNames[n] = NULL;
		tmpDataFiles[n] = NULL;
	}

	unsigned maxgAsicID = 0;
	char fName[1024];
	
	// Open the data index file
	sprintf(fName, "%s.idxf", inputFilePrefix);
	FILE *indexFile = fopen(fName, "r");
	if(indexFile == NULL) {
			fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
			exit(1);
	}
	
	// Open the data file
	sprintf(fName, "%s.rawf", inputFilePrefix);
	FILE *dataFile = fopen(fName, "r");
	if(dataFile == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
		exit(1);
	}

	// Create the temporary list file
	sprintf(fName, "%s_list.tmp", tmpFilePrefix);
	FILE *tmpListFile = fopen(fName, "w");
	if(tmpListFile == NULL) {
		fprintf(stderr, "Could not open '%s' for writing: %s\n", fName, strerror(errno));
		exit(1);
	}
	
	long startOffset, endOffset;
	float step1, step2;


	while(fscanf(indexFile, "%ld %ld %*lld %*lld %f %f\n", &startOffset, &endOffset, &step1, &step2) == 4) {
		fseek(dataFile, startOffset, SEEK_SET);
		long nCalData = (endOffset - startOffset)/sizeof(RawCalibrationData);
		RawCalibrationData *tmpRawCalDataBlock = new RawCalibrationData[nCalData];
		
		fread(tmpRawCalDataBlock, sizeof(RawCalibrationData), nCalData, dataFile);	
		for (int i = 0; i < nCalData; i++) {
			
			RawEventWord eWord(tmpRawCalDataBlock[i].eventWord);   
			unsigned gChannelID = eWord.getChannelID();
			unsigned tacID = eWord.getTacID();	       
			unsigned gAsicID = (gChannelID >> 6);
			unsigned long gid = (gChannelID << 2) | tacID;

			maxgAsicID = (maxgAsicID > gAsicID) ? maxgAsicID : gAsicID;

			FILE * f = tmpDataFiles[gAsicID];
			if(f == NULL) {
				// We haven't opened this file yet.
				unsigned chipID = (gChannelID >> 6) % 64;
				unsigned slaveID = (gChannelID >> 12) % 32;
				unsigned portID = (gChannelID >> 17) % 32;
				char *fn = new char[1024];
				sprintf(fn, "%s_%02u_%02u_%02u_data.tmp", tmpFilePrefix, portID, slaveID, chipID);
				f = fopen(fn, "w");
				if(f == NULL) {
					fprintf(stderr, "Could not open '%s' for reading: %s\n", fn, strerror(errno));
					exit(1);
				}
				setbuffer(f, new char[BUFFER_SIZE], BUFFER_SIZE);
				posix_fadvise(fileno(f), 0, 0, POSIX_FADV_SEQUENTIAL);
				tmpDataFiles[gAsicID] = f;
				tmpDataFileNames[gAsicID] = fn;
			}
			CalibrationData calData;
	        
			calData.gid =  gid;
			calData.tcoarse = eWord.getTCoarse();
			calData.ecoarse = eWord.getECoarse();
			calData.tfine = eWord.getTFine();
			calData.qfine = eWord.getEFine();			
			calData.freq = tmpRawCalDataBlock[i].freq;       	
			fwrite(&calData, sizeof(CalibrationData), 1, f);	
		}
		delete[] tmpRawCalDataBlock; 
	}
	
	
	for(unsigned long gAsicID = 0; gAsicID <= maxgAsicID; gAsicID++) {
		if(tmpDataFiles[gAsicID] != NULL) {
			fprintf(tmpListFile, "%lu %s\n", gAsicID, tmpDataFileNames[gAsicID]);
			fclose(tmpDataFiles[gAsicID]);
		}
		tmpDataFiles[gAsicID] = NULL;
		delete [] tmpDataFileNames[gAsicID];
	}
	fclose(tmpListFile);
}

void calibrateAsic(
        SystemConfig *config, 
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

	unsigned READ_BUFFER_SIZE = BUFFER_SIZE / sizeof(CalibrationData);
	CalibrationData *calDataBuffer = new CalibrationData[READ_BUFFER_SIZE];
	int r;
	bool asicPresent = false;
	lseek(dataFile, 0, SEEK_SET);
	while((r = read(dataFile, calDataBuffer, sizeof(CalibrationData)*READ_BUFFER_SIZE)) > 0) {
		int nEvents = r / sizeof(CalibrationData);
		for(int i = 0; i < nEvents; i++) {
			CalibrationData &calData = calDataBuffer[i];
			
			assert(hFine2_list[calData.gid-gidStart] != NULL);
			if((calData.ecoarse - calData.tcoarse) < -256) calData.ecoarse += 1024;  
			float ti = calData.ecoarse - calData.getTime(config);
		
			for(int j = 0; j < calData.freq; j++)
				hFine2_list[calData.gid-gidStart]->Fill(ti, calData.qfine);
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
		
		float yMin = pFine->GetMinimum(2);
		int bMin = pFine->FindFirstBinAbove(yMin);
		bMin = (bMin > 1) ? bMin : 1;
		float xMin = pFine->GetBinLowEdge(bMin);

		float yMax = pFine->GetMaximum();
		int bMax = pFine->FindFirstBinAbove(0.97 * yMax);
		bMax = (bMax < nBins) ? bMax : nBins;
		float xMax = pFine->GetBinLowEdge(bMax+1);
		
		// Clear entry 
		CalibrationEntry &entry = calibrationTable[gid];
		entry.p0 = 0;
		entry.p1 = 0;
		entry.p2 = 0;
		entry.p3 = 0;
		entry.p4 = 0;
		entry.p5 = 0;
		entry.p6 = 0;
		entry.p7 = 0;
		entry.p8 = 0;
		entry.p9 = 0;

		entry.xMin = xMin;
		entry.xMax = xMax;
		entry.valid = false;

		
		// Attempt to fit 9th order polynomial but fall back down to 3rd order if needed
		for(int order = 9; (order > 3) && !entry.valid; order--) {
			char functionName[16];
			sprintf(functionName, "pol%d", order);

			pFine->Fit(functionName, "Q", "", xMin, xMax);
			TF1 *polN = pFine->GetFunction(functionName);
			if(polN == NULL) // No fit
				continue;

			float chi2 = polN->GetChisquare();
			if(chi2 == 0) // Chi² == 0 is a bad fit
				continue;

			if(order >= 0) entry.p0 = polN->GetParameter(0);
			if(order >= 1) entry.p1 = polN->GetParameter(1);
			if(order >= 2) entry.p2 = polN->GetParameter(2);
			if(order >= 3) entry.p3 = polN->GetParameter(3);
			if(order >= 4) entry.p4 = polN->GetParameter(4);
			if(order >= 5) entry.p5 = polN->GetParameter(5);
			if(order >= 6) entry.p6 = polN->GetParameter(6);
			if(order >= 7) entry.p7 = polN->GetParameter(7);
			if(order >= 8) entry.p8 = polN->GetParameter(8);
			if(order >= 9) entry.p9 = polN->GetParameter(9);
			entry.valid = true;
		}

		if(!entry.valid) {
			fprintf(stderr, "WARNING: Could not make a fit. Skipping TAC (%02u %02d %02d %02d %u)\n",
				portID, slaveID, chipID, channelID, tacID);
		}
		
	}
	

	TProfile **pControlT_list = new TProfile *[nQAC];
	TH1S **hControlE_list = new TH1S *[nQAC];
	
	int ControlHistogramNBins = 512;
	float ControlHistogramRange = 5;
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
		float ControlHistogramRange = 5.0;
		hControlE_list[gid-gidStart] = new TH1S(hName, hName, ControlHistogramNBins, -ControlHistogramRange, ControlHistogramRange);
	}
	
	lseek(dataFile, 0, SEEK_SET);
	while((r = read(dataFile, calDataBuffer, sizeof(CalibrationData)*READ_BUFFER_SIZE)) > 0) {
		int nEvents = r / sizeof(CalibrationData);
		for (int i = 0; i < nEvents; i++) {
			CalibrationData &calData = calDataBuffer[i];
			assert(calData.gid >= gidStart);
			assert(calData.gid < gidEnd);

			CalibrationEntry &entry = calibrationTable[calData.gid];
			if(!entry.valid) continue;
			float ti = calData.ecoarse - calData.getTime(config);
			if(ti < entry.xMin || ti > entry.xMax) continue;
			
			float qExpected = entry.p0
					+ entry.p1 * ti
					+ entry.p2 * ti * ti
					+ entry.p3 * ti * ti * ti
					+ entry.p4 * ti * ti * ti * ti
				        + entry.p5 * ti * ti * ti * ti * ti
					+ entry.p6 * ti * ti * ti * ti * ti * ti
					+ entry.p7 * ti * ti * ti * ti * ti * ti * ti
					+ entry.p8 * ti * ti * ti * ti * ti * ti * ti * ti +
			                + entry.p9 * ti * ti * ti * ti * ti * ti * ti * ti * ti;
			float qError = calData.qfine - qExpected;
			
			
			TProfile *pControlT = pControlT_list[calData.gid-gidStart];
			TH1S *hControlE = hControlE_list[calData.gid-gidStart];
			for(int j = 0; j < calData.freq; j++){
				pControlT->Fill(ti, qError);
				hControlE->Fill(qError);
			}
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

	TCanvas *c = new TCanvas();
	c->Divide(2, 2);
	TH1F *hCounts = new TH1F("hCounts", "", 64*4, 0, 64);
	TH1S *hResolution = new TH1S("hResolution", "QDC resolution histograms", 256, 0, 5.0);
	TGraphErrors *gResolution = new TGraphErrors(64*4);
	gResolution->SetName("gResolution");
	int gResolutionNPoints = 0;
	
	TCanvas *tmp1 = new TCanvas();
	for(unsigned long channelID = 0; channelID < 64; channelID++) {
		for(unsigned long tacID = 0; tacID < 4; tacID++) {
			unsigned long gid = gidStart | (channelID << 2) | tacID;
			CalibrationEntry &entry = calibrationTable[gid];
			if(!entry.valid) continue;

			TH1S *hControlE = hControlE_list[gid-gidStart];
			double counts = hControlE->GetEntries();
			hCounts->SetBinContent(1 + 4*channelID + tacID, counts);
			
			if(hControlE->GetEntries() < 1000) continue;
			hControlE->Fit("gaus", "Q");
			TF1 *fit = hControlE->GetFunction("gaus");
			if(fit == NULL) continue;
				
			float sigma = fit->GetParameter(2);
			float sigmaError = fit->GetParError(2);
			gResolution->SetPoint(gResolutionNPoints, channelID + 0.25*tacID, sigma);
			gResolution->SetPointError(gResolutionNPoints, 0, sigmaError);
			hResolution->Fill(sigma);
			
			gResolutionNPoints += 1;
		}
	}
	delete tmp1;

	c->cd(1);
	hCounts->GetXaxis()->SetTitle("Channel");
	hCounts->GetYaxis()->SetRangeUser(0, maxCounts * 1.10);
	hCounts->Draw("HIST");
	
	c->cd(2);
	gResolution->Draw("AP");
	gResolution->SetTitle("QDC resolution");
	gResolution->GetXaxis()->SetTitle("Channel");
	gResolution->GetXaxis()->SetRangeUser(0, 64);
	gResolution->GetYaxis()->SetTitle("Resolution (ADC RMS)");
	gResolution->GetYaxis()->SetRangeUser(0, 5.0);
	gResolution->Draw("AP");
	gResolution->Write();
	
	c->cd(3);
	hResolution->SetTitle("QDC resolution histogram");
	hResolution->GetXaxis()->SetTitle("TDC resolution (ADC RMS)");
	hResolution->Draw("HIST");
	
	sprintf(fName, "%s.pdf", summaryFilePrefix);
	c->SaveAs(fName);

	sprintf(fName, "%s.png", summaryFilePrefix);
	c->SaveAs(fName);
	delete c;

	rootFile->Write();
	delete rootFile;

	delete tmp0;	
}

void calibrateAllAsics(SystemConfig *config, CalibrationEntry *calibrationTable, char *outputFilePrefix, int nBins, float xMin, float xMax, char *tmpFilePrefix)
{
	char fName[1024];
	sprintf(fName, "%s_list.tmp", tmpFilePrefix);
	FILE *tmpListFile = fopen(fName, "r");
	if(tmpListFile == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
		exit(1);
	}
	
	
	unsigned long gAsicID;
	char tmpDataFileName[1024];
	
	off_t max_tmp_file_size = 0;
	struct stat tmp_file_stat;
	rewind(tmpListFile);
	while(fscanf(tmpListFile, "%lu %[^\n]\n", &gAsicID, tmpDataFileName) == 2) {
		int r = stat(tmpDataFileName,  &tmp_file_stat);
		if (r == 0) {
			if(tmp_file_stat.st_size > max_tmp_file_size) max_tmp_file_size = tmp_file_stat.st_size;
		}
	}

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
	rewind(tmpListFile);
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
			fflush(stdout);
			calibrateAsic(config, gAsicID, tmpDataFile, calibrationTable, summaryFilePrefix, nBins, xMin, xMax);
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

	fprintf(f, "# portID\tslaveID\tchipID\tchannelID\ttacID\tp0\tp1\tp2\tp3\tp4\tp5\tp6\tp7\tp8\n");

	for(unsigned long gid = 0; gid < MAX_N_QAC; gid++) {
		CalibrationEntry &entry = calibrationTable[gid];
		if(!entry.valid) continue;
		unsigned tacID = (gid >> 0) % 4;
		unsigned channelID = (gid >> 2) % 64;
		unsigned chipID = (gid >> 8) % 64;
		unsigned slaveID = (gid >> 14) % 32;
		unsigned portID = (gid >> 19) % 32;
	
		fprintf(f, "%d\t%d\t%d\t%d\t%d\t%8.7e\t%8.7e\t%8.7e\t%8.7e\t%8.7e\t%8.7e\t%8.7e\t%8.7e\t%8.7e\t%8.7e\n",
			portID, slaveID, chipID, channelID, tacID,
			entry.p0, entry.p1, entry.p2, entry.p3, entry.p4, entry.p5, entry.p6, entry.p7, entry.p8, entry.p9
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
