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

#include <boost/random.hpp>
#include <boost/nondet_random.hpp>

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
	unsigned short coarse;
	unsigned short fine;
	float phase;
};

// TODO Put this somewhere else
const unsigned long MAX_N_ASIC = 32*32*64;
const unsigned long MAX_N_TAC = MAX_N_ASIC * 64 * 2 * 4;

struct CalibrationEntry {
	float t0;
	float tEdge;
	float a0;
	float a1;
	float a2;
	bool valid;
};


void sortData(char *inputFilePrefix, char *outputFilePrefix);
void calibrateAsic(
	unsigned long gAsicID,
	int linearityDataFile, int linearityNbins, float linearityRangeMinimum, float linearityRangeMaximum,
	CalibrationEntry *calibrationTable,
	float nominalM,
	char *summaryFilePrefix
);

void calibrateAllAsics(int linearityNbins, float linearityRangeMinimum, float linearityRangeMaximum,
		CalibrationEntry *calibrationTable,  float nominalM, char *outputFilePrefix);
		
void adjustCalibrationTable(CalibrationEntry *calibrationTable);

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
	
	
	CalibrationEntry *calibrationTable = (CalibrationEntry *)mmap(NULL, sizeof(CalibrationEntry)*MAX_N_TAC, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	for(int gid = 0; gid < MAX_N_TAC; gid++) {
		calibrationTable[gid].valid = false;
		calibrationTable[gid].t0 = 0.0;
		calibrationTable[gid].tEdge = 0.0;
		calibrationTable[gid].a0 = 0.0;
		calibrationTable[gid].a1 = 0.0;
		calibrationTable[gid].a2 = 0.0;
	}

	if(doSorting) {
		sortData(inputFilePrefix, outputFilePrefix);
	}
	calibrateAllAsics(nBins, xMin, xMax, calibrationTable, nominalM, outputFilePrefix);
	adjustCalibrationTable(calibrationTable);
	writeCalibrationTable(calibrationTable, outputFilePrefix);
	if(!keepTemporary) {
		deleteTemporaryFiles(outputFilePrefix);
	}

	return 0;
}

void sortData(char *inputFilePrefix, char *outputFilePrefix)
{
	printf("Sorting data into temporary files...\n");
	 
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
	sprintf(fName, "%s_list.tmp", outputFilePrefix);
	FILE *tmpListFile = fopen(fName, "w");
	if(tmpListFile == NULL) {
		fprintf(stderr, "Could not open '%s' for writing: %s\n", fName, strerror(errno));
		exit(1);
	}
	
	long startOffset, endOffset;
	float step1, step2;
	RawDataFrame *tmpRawDataFrame = new RawDataFrame;
	while(fscanf(indexFile, "%ld %ld %*lld %*lld %f %f\n", &startOffset, &endOffset, &step1, &step2) == 4) {
		fseek(dataFile, startOffset, SEEK_SET);
		while(ftell(dataFile) < endOffset) {
			fread((void *)(tmpRawDataFrame->data), sizeof(uint64_t), 2, dataFile);
			long frameSize = tmpRawDataFrame->getFrameSize();
			fread((void *)((tmpRawDataFrame->data)+2), sizeof(uint64_t), frameSize-2, dataFile);
			
			int nEvents = tmpRawDataFrame->getNEvents();
			for (int i = 0; i < nEvents; i++) {
				unsigned gChannelID = tmpRawDataFrame->getChannelID(i);
				unsigned tacID = tmpRawDataFrame->getTacID(i);
				unsigned gAsicID = (gChannelID >> 6);

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
				// Write data for T branch
				e.gid = (gChannelID << 3) | (tacID << 1) | 0x0;
				e.coarse = tmpRawDataFrame->getTCoarse(i);
				e.fine = tmpRawDataFrame->getTFine(i);
				e.phase = step1;
				fwrite((void*)&e, sizeof(e), 1, f);

				// Write data for E branch
				e.gid = (gChannelID << 3) | (tacID << 1) | 0x1;
				e.coarse = tmpRawDataFrame->getECoarse(i);
				e.fine = tmpRawDataFrame->getEFine(i);
				e.phase = step1;
				fwrite((void*)&e, sizeof(e), 1, f);
			}
			
		}
	}
	delete tmpRawDataFrame;
	
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

static const int TDC_PERIOD = 1;
static const float TDC_SYNC_OFFSET = 0.5;

static const int nPar1 = 3;
const char * paramNames1[nPar1] =  { "x0", "b", "m" };
static Double_t periodicF1 (Double_t *xx, Double_t *pp)
{
	float x = fmod(1024.0 + xx[0] - pp[0], TDC_PERIOD);	
	return pp[1] + pp[2]*TDC_PERIOD - pp[2]*x;
}


static const int nPar2 = 4;
const char *paramNames2[nPar2] = {  "tEdge", "a0", "a1", "a2" };
static Double_t periodicF2 (Double_t *xx, Double_t *pp)
{
	double tDelay = xx[0];	
	double tEdge = pp[0];
	double a0 = pp[1];
	double a1 = pp[2];
	double a2 = pp[3];
	
	double tQ = (TDC_PERIOD + TDC_SYNC_OFFSET) - fmod(1024.0 + tDelay - tEdge, TDC_PERIOD);
	double tFine = a0 + a1*tQ + a2*tQ*tQ;
	return tFine;
}



void calibrateAsic(
	unsigned long gAsicID,
	int linearityDataFile, int linearityNbins, float linearityRangeMinimum, float linearityRangeMaximum,
	CalibrationEntry *calibrationTable,
	float nominalM,
	char *summaryFilePrefix
)
{
	// Allocate a dummy canvas, otherwise one will be created for fits
	// and print a message
	TCanvas *tmp0 = new TCanvas();
	
	char fName[1024];
	sprintf(fName, "%s.root", summaryFilePrefix);
	TFile *rootFile = new TFile(fName, "RECREATE");

	
	unsigned long gidStart = gAsicID * 64 * 2 * 4;
	unsigned long gidEnd = (gAsicID+1) * 64 * 4 * 2;
	unsigned long nTAC = gidEnd - gidStart;

	// Build the histograms
	TH2S **hFine2_list = new TH2S *[nTAC];
	TH2S **hCoarse2_list = new TH2S *[nTAC];
	for(int n = 0; n < nTAC; n++) {
		hFine2_list[n] = NULL;
		hCoarse2_list[n] = NULL;
	}
	
	for(unsigned gid = gidStart; gid < gidEnd; gid++) {
		unsigned branchID = gid % 2;
		char bStr = (branchID == 0) ? 'T' : 'E';
		unsigned tacID = (gid >> 1) % 4;
		unsigned channelID = (gid >> 3) % 64;
		unsigned chipID = (gid >> 9) % 64;
		unsigned slaveID = (gid >> 15) % 32;
		unsigned portID = (gid >> 20) % 32;
		char hName[128];
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_%c_hFine2", portID, slaveID, chipID, channelID, tacID, bStr);
		hFine2_list[gid-gidStart] = new TH2S(hName, hName, linearityNbins, linearityRangeMinimum, linearityRangeMaximum, 1024, 0, 1024);
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_%c_hCoarse2", portID, slaveID, chipID, channelID, tacID, bStr);
		hCoarse2_list[gid-gidStart] = new TH2S(hName, hName, linearityNbins, linearityRangeMinimum, linearityRangeMaximum, 1024, 0, 1024);

	}
	struct timespec t0;
	clock_gettime(CLOCK_REALTIME, &t0);

	unsigned READ_BUFFER_SIZE = 32*1024*1024 / sizeof(CalibrationEvent);
	CalibrationEvent *eventBuffer = new CalibrationEvent[READ_BUFFER_SIZE];
	int r;
	bool asicPresent = false;
	lseek(linearityDataFile, 0, SEEK_SET);
	while((r = read(linearityDataFile, eventBuffer, sizeof(CalibrationEvent)*READ_BUFFER_SIZE)) > 0) {
		int nEvents = r / sizeof(CalibrationEvent);
		for(int i = 0; i < nEvents; i++) {
			CalibrationEvent &event = eventBuffer[i];
			assert(hFine2_list[event.gid-gidStart] != NULL);
			//if(event.fine < 0.5 * nominalM || event.fine > 4 * nominalM) continue;
			hFine2_list[event.gid-gidStart]->Fill(event.phase, event.fine);
			hCoarse2_list[event.gid-gidStart]->Fill(event.phase, event.coarse);
			asicPresent = true;
		}
	}
	
	struct timespec t1;
	clock_gettime(CLOCK_REALTIME, &t1);
	
	boost::mt19937 generator;
	
	if (!asicPresent) return;

	for(unsigned long gid = gidStart; gid < gidEnd; gid++) {
		unsigned branchID = gid % 2;
		char bStr = (branchID == 0) ? 'T' : 'E';
		unsigned tacID = (gid >> 1) % 4;
		unsigned channelID = (gid >> 3) % 64;
		unsigned chipID = (gid >> 9) % 64;
		unsigned slaveID = (gid >> 15) % 32;
		unsigned portID = (gid >> 20) % 32;
		char hName[128];
		

		//if(channelID!=36)continue;
		 
		TH2S *hFine2 = hFine2_list[gid-gidStart];
		if(hFine2 == NULL) continue;
		if(hFine2->GetEntries() < 1000) {
			fprintf(stderr, "WARNING: Not enough data to calibrate. Skipping TAC (%02u %02d %02d %02d %u %c)\n",
					portID, slaveID, chipID, channelID, tacID, bStr);
			continue;
		}
		
		// Obtain a rough estimate of the TDC range
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_%c_hFine", portID, slaveID, chipID, channelID, tacID, bStr);
		TH1D *hFine = hFine2->ProjectionY(hName);
		hFine->Rebin(8);
		hFine->Smooth(4);
		float adcMean = hFine->GetMean();
		int adcMeanBin = hFine->FindBin(adcMean);
		int adcMeanCount = hFine->GetBinContent(adcMeanBin);
		
		int adcMinBin = adcMeanBin;
		while(hFine->GetBinContent(adcMinBin) > 0.20 * adcMeanCount)
			adcMinBin--;
		
		int adcMaxBin = adcMeanBin;
		while(hFine->GetBinContent(adcMaxBin) > 0.20 * adcMeanCount)
			adcMaxBin++;
		
		float adcMin = hFine->GetBinCenter(adcMinBin);
		float adcMax = hFine->GetBinCenter(adcMaxBin);
		
		/*
		 * WARNING Hopefully, the following is not needed with TOFPET 2
		 */
// 		// Set limits on ADC range to exclude spurious things.
// 		hFine->GetYaxis()->SetRangeUser(
// 			adcMin - 32 > 0.5 * nominalM ? adcMin - 32 : 0.5 * nominalM,
// 			adcMax + 32 < 4.0 * nominalM ? adcMax + 32 : 4.0 * nominalM
// 			);
			
		
			
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_%c_pFine_X", portID, slaveID, chipID, channelID, tacID, bStr);
		TProfile *pFine = hFine2->ProfileX(hName, 1, -1, "s");
			
		int nBinsX = pFine->GetXaxis()->GetNbins();
		float xMin = pFine->GetXaxis()->GetXmin();
		float xMax = pFine->GetXaxis()->GetXmax();
		
		
		// Obtain a rough estimate of the edge position
		float tEdge = 0.0;
		float lowerT0 = 0.0;
		float upperT0 = 0.0;
		float maxSlope = 0;
		adcMin = 1024.0;
		for(int n = 10; n >= 1; n--) {
			for(int j = 1; j < (nBinsX - 1 - n); j++) {
				float v1 = pFine->GetBinContent(j);
				float v2 = pFine->GetBinContent(j+n);
				float e1 =  pFine->GetBinError(j);
				float e2 =  pFine->GetBinError(j+n);
				int c1 = pFine->GetBinEntries(j);
				int c2 = pFine->GetBinEntries(j+n);
				float t1 = pFine->GetBinCenter(j);
				float t2 = pFine->GetBinCenter(j+n);
				
				if(c1 == 0 || c2 == 0) continue;
				if(e1 > 5.0 || e2 > 5.0) continue;
				
				float slope = (v2 - v1)/(t2 - t1);
				if(slope > maxSlope) {
					tEdge = (t2 + t1)/2;
					lowerT0 = t1;// - 0.5 * pFine->GetXaxis()->GetBinWidth(0);
					upperT0 = t2;// + 0.5 * pFine->GetXaxis()->GetBinWidth(0);
					adcMin = fminf (adcMin, pFine->GetBinContent(j));
					maxSlope = slope;
				}
			}
		}
		
		if(adcMin == 1024.0) {
			fprintf(stderr, "WARNING: Could not find a suitable edge position. Skipping TAC (%02u %02d %02d %02d %u %c)\n",
					portID, slaveID, chipID, channelID, tacID, bStr);
			continue;
		}
		
		while(lowerT0 > TDC_PERIOD && tEdge > TDC_PERIOD && upperT0 > TDC_PERIOD) {
				lowerT0 -= TDC_PERIOD;
				tEdge -= TDC_PERIOD;
				upperT0 -= TDC_PERIOD;
		}
			
		float tEdgeTolerance = upperT0 - lowerT0;
		// Fit a line to a TDC period to determine the interpolation factor
		pFine->Fit("pol1", "Q", "", tEdge + tEdgeTolerance, tEdge + TDC_PERIOD  - tEdgeTolerance);
		TF1 *fPol = pFine->GetFunction("pol1");
		if(fPol == NULL) {
			fprintf(stderr, "WARNING: Could not make a linear fit. Skipping TAC (%02u %02d %02d %02d %u %c)\n",
				portID, slaveID, chipID, channelID, tacID, bStr);
			continue;
			
		}
		
		float estimatedM = - fPol->GetParameter(1);
		if(estimatedM < 100.0 || estimatedM > 256.0) {
			fprintf(stderr, "WARNING: M (%6.1f) is out of range[%6.1f, %6.1f]. Skipping TAC (%02u %02d %02d %02d %u %c)\n",
				estimatedM, 100.0, 256.0,
				portID, slaveID, chipID, channelID, tacID, bStr);
			continue;
		}
		
		boost::uniform_real<> range(lowerT0, upperT0);
		boost::variate_generator<boost::mt19937&, boost::uniform_real<> > nextRandomTEdge(generator, range);
	
		TF1 *pf = new TF1("periodicF1", periodicF1, xMin, xMax, nPar1);
		for(int p = 0; p < nPar1; p++) pf->SetParName(p, paramNames1[p]);
		pf->SetNpx(2 * nBinsX);

		
		float b;
		float m;
		float tB;
		float p2;
		float a0, a1, a2;
		float currChi2 = INFINITY;
		float prevChi2 = INFINITY;
		int nTry = 0;
		float maxChi2 = 2E6;
		do {
			pf->SetParameter(0, tEdge);		pf->SetParLimits(0, lowerT0, upperT0);
			pf->SetParameter(1, adcMin);		pf->SetParLimits(1, adcMin - estimatedM * tEdgeTolerance, adcMin);
			pf->SetParameter(2, estimatedM);	pf->SetParLimits(2, 0.98 * estimatedM, 1.02 * estimatedM),
			pFine->Fit("periodicF1", "Q", "", xMin, xMax);
			
			TF1 *pf_ = pFine->GetFunction("periodicF1");
			if(pf_ != NULL) {
				prevChi2 = currChi2;
				currChi2 = pf_->GetChisquare() / pf_->GetNDF();	
				
				if((currChi2 < prevChi2)) {
					tEdge = pf->GetParameter(0);
					b  = pf->GetParameter(1);
					m  = pf->GetParameter(2);
					tB = - (b/m - TDC_SYNC_OFFSET);
					p2 = 0;
				}
			}
			else {
				//	tEdge = nextRandomTEdge();
			}
			nTry += 1;
			
		} while((currChi2 <= 0.95*prevChi2) && (nTry < 10));
		
		


		if(prevChi2 > maxChi2 && currChi2 > maxChi2) {
			fprintf(stderr, "WARNING: NO FIT OR VERY BAD FIT (1). Skipping TAC (%02u %02d %02d %02d %u %c)\n",
				portID, slaveID, chipID, channelID, tacID, bStr);
			delete pf;
			continue;
		}
		
	
		
		TF1 *pf2 = new TF1("periodicF2", periodicF2, xMin, xMax,  nPar2);
		pf2->SetNpx(2*nBinsX);
		for(int p = 0; p < nPar2; p++) pf2->SetParName(p, paramNames2[p]);
		
		currChi2 = INFINITY;
		prevChi2 = INFINITY;
		nTry = 0;
	
		a0 = b;
		a1 = m;
		a2 = -3;

		//std::cout << b << " " << m << " " <<std::endl;
 
		tEdge = tEdge - 0.05;

		do{
				
			pf2->SetParameter(0, tEdge);       pf2->SetParLimits(0, tEdge-0.06, tEdge+0.06); 
			pf2->SetParameter(1, a0);	   //pf2->SetParLimits(1, 0.1, 200.0);
			pf2->SetParameter(2, a1);	   //pf2->SetParLimits(2, 0.1, 300.0);
			pf2->SetParameter(3, a2);	   pf2->SetParLimits(3, -20, -0.001); // Very small values of a2 cause rouding errors
			
				
			pFine->Fit("periodicF2", "Q", "", xMin, xMax);
			
			TF1 *pf_ = pFine->GetFunction("periodicF2");
			if(pf_ != NULL) {
				prevChi2 = currChi2;
				currChi2 = pf_->GetChisquare() / pf_->GetNDF();					
				tEdge += 0.01;
			}
			
			nTry += 1;
			
				
		} while( (nTry < 10) && (currChi2 > 4));
		
		if(prevChi2 > maxChi2 && currChi2 > maxChi2) {
			fprintf(stderr, "WARNING: NO FIT OR VERY BAD FIT (2). Skipping TAC (%02u %02d %02d %02d %u %c)\n",
				portID, slaveID, chipID, channelID, tacID, bStr);
			delete pf;
			delete pf2;
			continue;
		}
		
		CalibrationEntry &entry = calibrationTable[gid];
		entry.t0 = 0;
		entry.tEdge = tEdge = pf2->GetParameter(0);
		entry.a0 = a0 = pf2->GetParameter(1);
		entry.a1 = a1 = pf2->GetParameter(2);
		entry.a2 = a2 = pf2->GetParameter(3);
		entry.valid = true;
	
		delete pf;
		delete pf2;
	}

	TProfile **pControlT_list = new TProfile *[nTAC];
	TH1S **hControlE_list = new TH1S *[nTAC];
	
	int ControlHistogramNBins = 512;
	float ControlHistogramRange = 0.5;
	for(unsigned long gid = gidStart; gid < gidEnd; gid++) {
		pControlT_list[gid-gidStart] = NULL;
		hControlE_list[gid-gidStart] = NULL;
		
		CalibrationEntry &entry = calibrationTable[gid];
		if(!entry.valid) continue;
		
		unsigned branchID = gid % 2;
		char bStr = (branchID == 0) ? 'T' : 'E';
		unsigned tacID = (gid >> 1) % 4;
		unsigned channelID = (gid >> 3) % 64;
		unsigned chipID = (gid >> 9) % 64;
		unsigned slaveID = (gid >> 15) % 32;
		unsigned portID = (gid >> 20) % 32;
		char hName[128];
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_%c_control_T", portID, slaveID, chipID, channelID, tacID, bStr);
		pControlT_list[gid-gidStart] = new TProfile(hName, hName, linearityNbins, linearityRangeMinimum, linearityRangeMaximum, "s");
		sprintf(hName, "c_%02d_%02d_%02d_%02d_%d_%c_control_E", portID, slaveID, chipID, channelID, tacID, bStr);
		hControlE_list[gid-gidStart] = new TH1S(hName, hName, ControlHistogramNBins, -ControlHistogramRange, ControlHistogramRange);
	}
	
	
	
	// Need two iterations to correct t0, then one more to build final histograms
	int nIterations = 3;
	for(int iter = 0; iter <= nIterations; iter++) {
		for(unsigned long gid = gidStart; gid < gidEnd; gid++) {
			if(!calibrationTable[gid].valid) continue;
			pControlT_list[gid-gidStart]->Reset();
			hControlE_list[gid-gidStart]->Reset();
		}
	
		lseek(linearityDataFile, 0, SEEK_SET);
		int r;
		while((r = read(linearityDataFile, eventBuffer, sizeof(CalibrationEvent)*READ_BUFFER_SIZE)) > 0) {
			int nEvents = r / sizeof(CalibrationEvent);
			for (int i = 0; i < nEvents; i++) {
				CalibrationEvent &event = eventBuffer[i];
				assert(event.gid >= gidStart);
				assert(event.gid < gidEnd);

				CalibrationEntry &entry = calibrationTable[event.gid];
				if(!entry.valid) continue;
				
				
				float t = event.phase;
				float t_ = fmod(1024.0 + t - entry.tEdge, TDC_PERIOD);
				
				//float qEstimate = +( 2.0f * entry.p2 * entry.tB + sqrtf(4.0f * event.fine * entry.p2 + entry.m*entry.m) - entry.m)/(2.0f * entry.p2);
				float qEstimate = ( -entry.a1 + sqrtf((entry.a1 * entry.a1) - (4.0f * (entry.a0 - event.fine) * entry.a2))) / (2.0f * entry.a2) ;  


				assert(TDC_PERIOD == 1); // No support for TDC_PERIOD != 1 operation
				
				float tEstimate = event.coarse - qEstimate - entry.t0;
				float tError = tEstimate - event.phase;

				TProfile *pControlT = pControlT_list[event.gid-gidStart];
				TH1S *hControlE = hControlE_list[event.gid-gidStart];
				
				pControlT->Fill(event.phase, tError);
				// Don't fill if out of histogram's range
				if(fabs(tError) < ControlHistogramRange ) {
					hControlE->Fill(tError);
				}
			}
		}
		
		if(iter >= nIterations) continue;
 
		for(unsigned long  gid = gidStart; gid < gidEnd; gid++) {
			CalibrationEntry &entry = calibrationTable[gid];
			if(!entry.valid) continue;
			
			TProfile *pControlT = pControlT_list[gid-gidStart];
			TH1S *hControlE = hControlE_list[gid-gidStart];
			
			// Initial offset estimate is zero
			float offset = 0;
			
			// Stage 1: Extract offset from pControlT if it has at least 100 entries
			if(pControlT->GetEntries() > 100) {
				offset = pControlT->GetMean(2);
			}
			
			// Stage 2: Extract offset from hControlE with gaussian fit, if it has more than 100 entries
			// and the function fits.
			if (hControlE->GetEntries() > 1000) {
				TF1 *f = NULL;
				hControlE->Fit("gaus", "Q");
				f = hControlE->GetFunction("gaus");
				if (f != NULL) {
					offset = f->GetParameter(1);
				}
			}
			
			entry.t0 += offset;
		}
	}

	delete [] eventBuffer;
	
	for(unsigned long gChannelID = gidStart/8; gChannelID < gidEnd/8; gChannelID++) {
		unsigned long channelID = gChannelID % 64;
		unsigned long chipID = (gChannelID >> 6) % 64;
		unsigned long slaveID = (gChannelID >> 11) % 32;
		unsigned long portID = (gChannelID >> 16) % 32;
		
		bool channelOK = true;
		for(unsigned tac = 0; tac < 8; tac++) {
			unsigned long gid = (gChannelID << 3) | tac;
			CalibrationEntry &entry = calibrationTable[gid];
			channelOK &= entry.valid;
		}
		if(!channelOK) {
			fprintf(stderr, "WARNING Channel (%2d %2d %2d %2d) has one or more uncalibrated TAC. Zero'ing out channel.\n", portID, slaveID, chipID, channelID);
			for(int tac = 0; tac < 8; tac++) {
				unsigned long gid = (gChannelID << 3) | tac;
				CalibrationEntry &entry = calibrationTable[gid];
				entry.valid = false;
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
	c->Divide(3,2);
	TH1F *hCount_list[2];
	for(unsigned long branchID = 0; branchID < 2; branchID++) {
		char bStr = (branchID == 0) ? 'T' : 'E';
		char hName[128];
		sprintf(hName, "hCount_%c", bStr);
		TH1F *hCounts = hCount_list[branchID] = new TH1F(hName, "", 64*4, 0, 64);
		
		sprintf(hName, "hResolution_%c", bStr);
		TH1S *hResolution = new TH1S(hName, "TDC resolution histogram", 256, 0.0, 0.1);
		
		TGraphErrors *gResolution = new TGraphErrors(64*4);
		int gResolutionNPoints = 0;
		
		TCanvas *tmp1 = new TCanvas(); // We need this, otherwise the fitting will overwrite into "c"
		for(unsigned long channelID = 0; channelID < 64; channelID++) {
			for(unsigned long tacID = 0; tacID < 4; tacID++) {
				unsigned long gid = gidStart | (channelID << 3) | (tacID << 1) | branchID;
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
		
		c->cd(3*branchID + 1);
		hCounts->GetXaxis()->SetTitle("Channel");
		hCounts->GetYaxis()->SetRangeUser(0, maxCounts * 1.10);
		hCounts->Draw("HIST");
		
		c->cd(3*branchID + 2);
		gResolution->Draw("AP");
		gResolution->SetTitle("TDC resolution");
		gResolution->GetXaxis()->SetTitle("Channel");
		gResolution->GetXaxis()->SetRangeUser(0, 64);
		gResolution->GetYaxis()->SetTitle("Resolution (clk RMS)");
		gResolution->GetYaxis()->SetRangeUser(0, 0.1);
		gResolution->Draw("AP");
		
		c->cd(3*branchID + 3);
		hResolution->SetTitle("TDC resolution histogram");
		hResolution->GetXaxis()->SetTitle("TDC resolution (clk RMS)");
		hResolution->Draw("HIST");
		
	}
	sprintf(fName, "%s.pdf", summaryFilePrefix);
	c->SaveAs(fName);

	sprintf(fName, "%s.png", summaryFilePrefix);
	c->SaveAs(fName);

	rootFile->Write();
	delete rootFile;

	delete tmp0;
}

void calibrateAllAsics(int linearityNbins, float linearityRangeMinimum, float linearityRangeMaximum,
		       CalibrationEntry *calibrationTable, float nominalM, char *outputFilePrefix)
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
			calibrateAsic(gAsicID, tmpDataFile, linearityNbins, linearityRangeMinimum, linearityRangeMaximum,
				      calibrationTable, nominalM, summaryFilePrefix);
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

void adjustCalibrationTable(CalibrationEntry *calibrationTable)
{
	for(unsigned long branchID = 0; branchID < 2; branchID++) {
		double sum = 0;
		unsigned long n = 0;
		
		for(unsigned long gid = branchID; gid < MAX_N_TAC; gid += 2) {
			CalibrationEntry &entry = calibrationTable[gid];
			if(!entry.valid) continue;
			sum += entry.t0;
			n += 1;
		}
		char bStr = (branchID == 0) ? 'T' : 'E';

		double average = sum / n;
		for(unsigned long gid = branchID; gid < MAX_N_TAC; gid += 2) {
                        CalibrationEntry &entry = calibrationTable[gid];
                        if(!entry.valid) continue;
                        entry.t0 -= average;
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

	fprintf(f, "# portID\tslaveID\tchipID\tchannelID\ttacID\tbranch\tt0\ta0\ta1\ta2\n");

	for(unsigned long gid = 0; gid < MAX_N_TAC; gid++) {
		CalibrationEntry &entry = calibrationTable[gid];
		if(!entry.valid) continue;

		unsigned branchID = gid % 2;
		char bStr = (branchID == 0) ? 'T' : 'E';
		unsigned tacID = (gid >> 1) % 4;
		unsigned channelID = (gid >> 3) % 64;
		unsigned chipID = (gid >> 9) % 64;
		unsigned slaveID = (gid >> 15) % 32;
		unsigned portID = (gid >> 20) % 32;
	
		fprintf(f, "%d\t%d\t%d\t%d\t%d\t%c\t%8.7e\t%8.7e\t%8.7e\t%8.7e\n",
			portID, slaveID, chipID, channelID, tacID, bStr,
			entry.t0, entry.a0, entry.a1, entry.a2
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
