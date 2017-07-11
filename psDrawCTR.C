#include <TFile.h>
#include <TTree.h>
#include <TH1F.h>
#include <TH2F.h>
#include <TF1.h>
#include <TSpectrum.h>
#include <TCanvas.h>
#include <TStyle.h>
#include <stdio.h>

#include <stdio.h>
#include <stdint.h>
#include <TH1F.h>
#include <TSpectrum.h>
#include <TF1.h>
#include <TGraphErrors.h>

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


int psDrawCTR(char const *filePrefix, Int_t channelA=-1, Int_t channelB=-1)
{

	char fn[1024];
	sprintf(fn, "%s.ldat", filePrefix);
        FILE * dataFile = fopen(fn, "rb");
	sprintf(fn, "%s.lidx", filePrefix);
        FILE * indexFile = fopen(fn, "rb");
		
	Double_t tBinWidth = 45;
	Double_t wMax = 3000;
	TH1F *hDelta = new TH1F("hDelta", "Coincidence time difference", 2*wMax/tBinWidth, -wMax, wMax);
	
	const Int_t N_CHANNELS = 1024;
	TH2F *hC2 = new TH2F("hC2", "C2", N_CHANNELS, 0, N_CHANNELS, N_CHANNELS, 0, N_CHANNELS);
	

	int minToT = 0;
	TH1F * hE1 = new TH1F("hE1", "E 1", 128, 0, 400);
	TH1F * hE2 = new TH1F("hE2", "E 2", 128, 0, 400);
	
	struct CEvent {
		Long64_t	time1;
		Long64_t	time2;
		Float_t		tot1;
		Float_t		tot2;
	};

	const Int_t MAX_EVENTS = 100000;
	const Int_t MAX_EVENTS_TOTAL = 250000000;
	CEvent *eventBuffer = new CEvent[MAX_EVENTS];

	long long stepBegin, stepEnd;
	float step1, step2;

	TCanvas *c1 = new TCanvas();
	TCanvas *c2 = NULL;
	
	sprintf(fn, "%s_stats.txt", filePrefix);
	FILE *stats = fopen(fn, "w");
	
	int stepIndex = 0;
	while(fscanf(indexFile,"%lld\t%lld\t%f\%f\n", &stepBegin, &stepEnd, &step1, &step2) == 4) {
		stepIndex += 1;
		hDelta->Reset();
		hC2->Reset();
		hE1->Reset();
		hE2->Reset();
		
		fseek(dataFile, stepBegin, SEEK_SET);
		Int_t count = 0;
		Event ei;
		int nRead = 0;
		if(channelA==-1 && channelB==-1){
			while(ftell(dataFile) < stepEnd && fread(&ei, sizeof(ei), 1, dataFile) == 1 && count < MAX_EVENTS_TOTAL) {
				if(ei.e1 < minToT || ei.e2 < minToT) continue;
				hC2->Fill(ei.id1, ei.id2);
				count++;
				if(count%1000000==0)cout << count <<endl;
			}	
			fseek(dataFile, stepBegin, SEEK_SET);
			Int_t binMax=hC2->GetMaximumBin();
			Int_t binx, biny, binz;
			hC2->GetBinXYZ(binMax, binx, biny, binz);
			channelA=binx-1;
			channelB=biny-1;
			printf("CA max=%d  CB max=%d\n",channelA, channelB);
		}
		
		count=0;
		char hName[128];

		while(ftell(dataFile) < stepEnd && fread(&ei, sizeof(ei), 1, dataFile) == 1 && count < MAX_EVENTS) {
			nRead ++;
			if(nRead % 10000 == 0) {
				printf("Read %d\r", nRead);
				fflush(stdout);
			}
			if(ei.e1 < minToT || ei.e2 < minToT) continue;
			
			hC2->Fill(ei.id1, ei.id2);
			
			bool selectedPair = ((ei.id1 == channelA) || (ei.id1 == channelB)) && ((ei.id2 == channelA) || (ei.id2 == channelB));
			if(!selectedPair) {
				continue;
			}
			
			CEvent &e = eventBuffer[count];
			e.time1 = ei.time1;
			e.time2 = ei.time2;
			e.tot1 = ei.e1;
			e.tot2 = ei.e2;
			count++;
			
			hE1->Fill(ei.e1);
			hE2->Fill(ei.e2);
		}
		printf("\n");
		
		//hE1->Smooth();
		//hE2->Smooth();
		
		TSpectrum *spectrum = new TSpectrum();
		spectrum->Search(hE1, 3, " ",  0.2);
		Int_t nPeaks = spectrum->GetNPeaks();
		if (nPeaks == 0) {
			printf("No peaks in hE1!!!\n");
			continue;
		}
	#if ROOT_VERSION_CODE > ROOT_VERSION(6,0,0)
		Double_t *xPositions1 = spectrum->GetPositionX();
	#else
		Float_t  *xPositions1 = spectrum->GetPositionX();
	#endif	
		Float_t x1_psc = 0;
		Float_t x1_pe = 0;
		for(Int_t i = 0; i < nPeaks; i++) {
		if(xPositions1[i] > x1_pe) {
			if(x1_pe > x1_psc)
				x1_psc = x1_pe;
				x1_pe = xPositions1[i];
			}
		} 


		spectrum = new TSpectrum();
		spectrum->Search(hE2, 3, " ",  0.2);
		nPeaks = spectrum->GetNPeaks();
		if (nPeaks == 0) {
			printf("No peaks in hE2!!!\n");
			continue;
		}
	#if ROOT_VERSION_CODE > ROOT_VERSION(6,0,0)
		Double_t *xPositions2 = spectrum->GetPositionX();
	#else
		Float_t  *xPositions2 = spectrum->GetPositionX();
	#endif

		Float_t x2_psc = 0;
		Float_t x2_pe = 0;
		for(Int_t i = 0; i < nPeaks; i++) {
		if(xPositions2[i] > x2_pe) {
			if(x2_pe > x2_psc)
				x2_psc = x2_pe;
				x2_pe = xPositions2[i];
			}
		} 
			
		Float_t x1 = x1_pe;
		Float_t x2 = x2_pe;
		

		hE1->Fit("gaus", "", "", x1-10, x1+10);
		if(hE1->GetFunction("gaus") == NULL) {
			return -1;
		}
		x1 = hE1->GetFunction("gaus")->GetParameter(1);
		Float_t sigma1 = hE1->GetFunction("gaus")->GetParameter(2);
		
		

		hE2->Fit("gaus", "", "", x2-10, x2+10);
		if(hE2->GetFunction("gaus") == NULL) { 
			return -1;
		}
		x2 = hE2->GetFunction("gaus")->GetParameter(1);
		Float_t sigma2 = hE2->GetFunction("gaus")->GetParameter(2);

		// Redo, now with sN
		Float_t sN = 2.0;
		hE1->Fit("gaus", "", "", x1-sN*sigma1, x1+sN*sigma1);
		if(hE1->GetFunction("gaus") == NULL) {
			return -1;
		}
		x1 = hE1->GetFunction("gaus")->GetParameter(1);
		sigma1 = hE1->GetFunction("gaus")->GetParameter(2);
		
		

		hE2->Fit("gaus", "", "", x2-sN*sigma2, x2+sN*sigma2);
		if(hE2->GetFunction("gaus") == NULL) { 
			return -1;
		}
		x2 = hE2->GetFunction("gaus")->GetParameter(1);
		sigma2 = hE2->GetFunction("gaus")->GetParameter(2);

		
		sN=1;
		for(Int_t i = 0; i < count; i++) {
			CEvent &e = eventBuffer[i];

			if((e.tot1 < (x1 - sN*sigma1)) || (e.tot1 > (x1 + sN*sigma1))) 
				continue;
			
			if((e.tot2 < (x2 - sN*sigma2)) || (e.tot2 > (x2 + sN*sigma2))) 
				continue;
			
			Float_t delta = e.time1 - e.time2;
			//delta *= 1E-12;
			
			hDelta->Fill(delta);
		}
		int binmax = hDelta->GetMaximumBin();
		Float_t hmax= hDelta->GetXaxis()->GetBinCenter(binmax);
		
		hDelta->Fit("gaus", "", "", hmax-350, hmax+300);


		gStyle->SetPalette(1);
		gStyle->SetOptFit(1);
	//	if(c2 != NULL) delete c2;
	//	TCanvas *c2 = new TCanvas();
		c2 = c1; c2->Clear();
		c2->Divide(3, 1);
		c2->cd(1); hE1->Draw();
		hE1->GetXaxis()->SetTitle("");
		c2->cd(2); hDelta->Draw();
		hDelta->GetXaxis()->SetTitle("time1-time2 [ps]");
		c2->cd(3); hE2->Draw();
		hE2->GetXaxis()->SetTitle("");
		c2->Modified();
		char pdfName[1024];
		sprintf(pdfName, "%s_%08.2f_%08.2f.pdf", filePrefix, step1, step2);
		c2->SaveAs(pdfName);
		sprintf(pdfName, "%s_%08.2f_%08.2f.png", filePrefix, step1, step2);
		c2->SaveAs(pdfName);
		
		float sigma = hDelta->GetRMS();
		float mean = hDelta->GetMean();
		float sigmaError = 0.1 * sigma;
		float meanError = 0.1 * mean;
		TF1 *g = hDelta->GetFunction("gaus");
		if(g != NULL) {
			mean = g->GetParameter(1);
			meanError = g->GetParError(1);
			sigma = g->GetParameter(2);
			sigmaError = g->GetParError(2);
		}
		int counts = hDelta->GetEntries();
		
		printf("---- step1 = %8.3f step2 = %8.3f ----\n", step1, step2);
		printf("CTR = %f sigma (%f FWHM)\n", sigma, 2.354*sigma);
		printf("E1 = %f\n", x1);
		printf("E2 = %f\n", x2);
		fprintf(stats, "%e\t%e\t%e\t%e\t%e\t%e\t%e\t%e\t%d\n", step1, step2, sigma, sigmaError, mean, meanError, x1, x2, counts);

	}

	delete [] eventBuffer;
	return 0;
}
