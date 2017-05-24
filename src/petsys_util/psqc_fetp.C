#include <TFile.h>
#include <TTree.h>
#include <TH1F.h>
#include <TH2F.h>
#include <TF1.h>
#include <TSpectrum.h>
#include <TCanvas.h>
#include <TStyle.h>
#include <TGraph.h>
#include <stdio.h>

#include <stdio.h>
#include <stdint.h>
#include <TProfile.h>

struct Event {
	long long time;
	float e;
	int id;
};


int psqc_fetp(char const *filePrefix)
{
	gStyle->SetOptStat(0);

	char fn[1024];
	sprintf(fn, "%s.ldat", filePrefix);
        FILE * dataFile = fopen(fn, "rb");
	sprintf(fn, "%s.lidx", filePrefix);
        FILE * indexFile = fopen(fn, "rb");
	
	
	const unsigned MAX_ASICS = 32 * 32 * 64;
	const unsigned MAX_CHANNELS = MAX_ASICS * 64;
	
	bool *asicPresent = new bool[MAX_ASICS];
	for(unsigned asic = 0; asic < MAX_ASICS; asic++) {
		asicPresent[asic] = false;
	}
	
	TProfile **pToA = new TProfile *[MAX_CHANNELS];
	TProfile **pA = new TProfile *[MAX_CHANNELS];
	for(unsigned channel = 0; channel < MAX_CHANNELS; channel++) {
		pToA[channel] = NULL;
		pA[channel] = NULL;
	}
	
	const int BS = 128*1024;
	Event *eventBuffer = new Event[BS];
	
	long long stepBegin, stepEnd;
	float step1, step2;
	while(fscanf(indexFile,"%lld\t%lld\t%f\%f\n", &stepBegin, &stepEnd, &step1, &step2) == 4) {
		fseek(dataFile, stepBegin, SEEK_SET);
		
		// Convert step limits from bytes to Events
		stepBegin = stepBegin / sizeof(Event);
		stepEnd = stepEnd / sizeof(Event);
		long long stepCurrent = stepBegin;
		
		while(stepCurrent < stepEnd) {
			int count = (stepEnd - stepCurrent);
			if (count > BS) count = BS;
			int r = fread((void *)eventBuffer, sizeof(Event), count, dataFile);
			if(r < 0) return -1;
			
			// r is good, it's number of events read
			stepCurrent += r;
			
			for(int n = 0; n < r; n++) {
				Event &e = eventBuffer[n];
				int asicID = e.id / 64;
				asicPresent[asicID] = true;
				
				if(pToA[e.id] == NULL) {
					char hName[128];
					sprintf(hName, "pToA_%07d", e.id);
					pToA[e.id] = new TProfile(hName, "Time ofArrival", 32, 0, 32, "s");
					
					sprintf(hName, "pA_%07d", e.id);
					pA[e.id] = new TProfile(hName, "Amplitude", 32, 0, 32, "s");
				}
				pToA[e.id]->Fill(step1, e.time % (5000*1024));
				pA[e.id]->Fill(step1, e.e);
			}
		}
		
		
	}
	delete [] eventBuffer;
	
	TCanvas *c = new TCanvas();

	TH1F *hCounts = new TH1F("hCounts", "Events", 64, 0, 64);
	TH1F *hTRMS = new TH1F("hTRMS", "Time RMS", 64, 0, 64);
	TH1F *hA = new TH1F("hA", "Amplitude", 64, 0, 64);
	TH1F *hARMS = new TH1F("hARMS", "Amplitude RMS", 64, 0, 64);
	
	for(unsigned asic = 0; asic < MAX_ASICS; asic++) {
		if(!asicPresent[asic]) continue;
		
		c->Clear();
		c->Divide(2, 2);

		int N = 0;
		hCounts->Reset();
		hTRMS->Reset();
		hA->Reset();
		hARMS->Reset();
		
		for (unsigned channel = 0; channel < 64; channel++) {
			int gid = 64*asic + channel;
			if(pToA[gid] == NULL) continue;

			int B = 31;
			double counts = pToA[gid]->GetBinEntries(B);
			double trms = pToA[gid]->GetBinError(B);
			double amplitude = pA[gid]->GetBinContent(B);
			double arms = pA[gid]->GetBinError(B);
			
			hCounts->SetBinContent(channel+1, counts);
			hTRMS->SetBinContent(channel+1, trms);
			hA->SetBinContent(channel+1, amplitude);
			hARMS->SetBinContent(channel+1, arms);

			N += 1;
		}

		c->cd(1);
		hCounts->GetXaxis()->SetTitle("Channel");
		hCounts->GetYaxis()->SetTitle("#");
		hCounts->GetYaxis()->SetRangeUser(0, 5000);
		hCounts->Draw();

		c->cd(2);
		hTRMS->GetXaxis()->SetTitle("Channel");
		hTRMS->GetYaxis()->SetTitle("T RMS (ps)");
		hTRMS->GetYaxis()->SetRangeUser(0, 200);
		hTRMS->Draw("");
		
		c->cd(3);
		hA->GetXaxis()->SetTitle("Channel");
		hA->GetYaxis()->SetTitle("Amplitude (ADC)");
		hA->GetYaxis()->SetRangeUser(0, 200);
		hA->Draw("");
		
		c->cd(4);
		hARMS->GetXaxis()->SetTitle("Channel");
		hARMS->GetYaxis()->SetTitle("Amplitude RMS (ADC)");
		hARMS->GetYaxis()->SetRangeUser(0, 4);
		hARMS->Draw("");


		char fName[1024];
		
		unsigned chipID = asic & 63;
		unsigned slaveID = (asic >> 6) & 31;
		unsigned portID = (asic >> 11) & 31;
		sprintf(fName, "%s_%02u_%02u_%02u.pdf", filePrefix, portID, slaveID, chipID);
		c->SaveAs(fName);
		sprintf(fName, "%s_%02u_%02u_%02u.png", filePrefix, portID, slaveID, chipID);
		c->SaveAs(fName);
		
	}
		
	return 0;
}
