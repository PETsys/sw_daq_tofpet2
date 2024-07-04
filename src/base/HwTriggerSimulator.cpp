#include "HwTriggerSimulator.hpp"
#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <math.h>


using namespace std;
using namespace PETSYS;

HwTriggerSimulator::HwTriggerSimulator(SystemConfig *systemConfig, EventSink<RawHit> *sink) :
	UnorderedEventHandler<RawHit, RawHit>(sink), systemConfig(systemConfig)
{
	nReceived = 0;
	nSent = 0;
}

struct SortEntry {
        long long time;
        RawHit *p;
};

static bool operator< (SortEntry lhs, SortEntry rhs) { return lhs.time < rhs.time; }


EventBuffer<RawHit> * HwTriggerSimulator::handleEvents (EventBuffer<RawHit> *inBuffer)
{
	unsigned N =  inBuffer->getSize();
	
	long long firstFrameID = inBuffer->getTMin()/1024;

	EventBuffer<RawHit> * outBuffer = new EventBuffer<RawHit>(N, inBuffer);
	u_int64_t lReceived = 0;
	u_int64_t lSent = 0;
	
	int count = 0;

	vector<SortEntry> sortList;
	sortList.reserve(N);

	auto pi = inBuffer->getPtr();
	auto pe = pi + N;

	for(; pi < pe; pi++) {
		SortEntry entry = {
			.time = pi->time,
			.p = pi
		};
		sortList.push_back(entry);
	}
	
	sort(sortList.begin(), sortList.end());
	
	long long timeMin = sortList.front().time - 100;
	long long timeMax = sortList.back().time + 100;

	unsigned triggerArraySize = timeMax - timeMin;

	const unsigned nTriggerRegions = systemConfig->mapTriggerRegions.size();
	
	float** energyArray = new float*[nTriggerRegions];
	short** multiplicityArray = new short*[nTriggerRegions];
	for (int i = 0; i < nTriggerRegions; i++) {
		energyArray[i] = new float[triggerArraySize];
		multiplicityArray[i] = new short[triggerArraySize];
		for (int j = 0; j < triggerArraySize; j++) {
			energyArray[i][j] = 0;
			multiplicityArray[i][j] = 0;
		}
	}

	long long t0;
	long long t1;

	bool isFirst = true;
	bool printMore = false;

	for(auto iter = sortList.begin(); iter != sortList.end(); iter++) {

		auto p = (*iter).p;

		SystemConfig::ChannelConfig &cc = systemConfig->getChannelConfig(p->channelID);
		SystemConfig::FirmwareConfig &cf = cc.empConfig[p->tacID];
	
		double energy1 = cf.p0 + cf.p1 * p->efine + cf.p2 * p->efine * p->efine;
		double energy = energy1;
		if(energy1 < 0.1875) energy = 0.1875;

		double timeWalkCorrection =  0.06 + cf.k0 / energy ;
		long long timeCorrected = p->time - (long long)(timeWalkCorrection);
		long long bufferBinnedTime = timeCorrected - timeMin;
		unsigned reducedTriggerRegion = systemConfig->mapTriggerRegions[cc.triggerRegion];
		

		if(bufferBinnedTime <  -100) continue;
		if(bufferBinnedTime >  triggerArraySize + 100) continue;
	
		energyArray[reducedTriggerRegion][bufferBinnedTime] += energy;
		multiplicityArray[reducedTriggerRegion][bufferBinnedTime] += 1;

	}
	
	


	vector<int> *triggerRequestTimeBins = new vector<int>[nTriggerRegions];
	for (int i = 0; i < nTriggerRegions ; i++) {
		for (int j = 0; j < triggerArraySize; j++) {
			long long t = inBuffer->getTMin()+timeMin+j;
			int multiplicity = 0;
			double energySum = 0;
			for (int k = -2 ; k <= 2 ; k++){
				energySum += energyArray[i][j+k];
				multiplicity += multiplicityArray[i][j+k];
			}
			if( energySum > systemConfig->sw_fw_trigger_group_min_energy && energySum < systemConfig->sw_fw_trigger_group_max_energy) { 	
				if(multiplicity >= systemConfig->sw_fw_trigger_group_min_nhits && multiplicity <= systemConfig->sw_fw_trigger_group_max_nhits){
					triggerRequestTimeBins[i].push_back(j);
					count++;
				}						
			}	
		}
	}

	vector<int> *coincidenceRegionsAllowed = new vector<int>[nTriggerRegions];
	vector<int> *triggerRequestCoincidenceTimeBins = new vector<int>[nTriggerRegions];
	vector<int> *triggerAcceptedTimeBins = new vector<int>[nTriggerRegions];
	vector<int> *triggerExtendedAcceptedTimeBins = new vector<int>[nTriggerRegions/4];
	vector<int> *triggerFilteredAcceptedTimeBins = new vector<int>[nTriggerRegions];

	for (int i = 0; i < nTriggerRegions ; i++) {
		for (int j = i; j < nTriggerRegions ; j++){
			unsigned userTriggerRegion1 = systemConfig->mapTriggerRegionsInverted[i];
			unsigned userTriggerRegion2 = systemConfig->mapTriggerRegionsInverted[j];
			if(systemConfig->isCoincidenceAllowed(userTriggerRegion1, userTriggerRegion2)){
				coincidenceRegionsAllowed[j].push_back(i);
				coincidenceRegionsAllowed[i].push_back(j);
			};
		}	
	}

	for (int i = 0; i < nTriggerRegions ; i++) {
		for (int r : coincidenceRegionsAllowed[i]){
			for (int tBin : triggerRequestTimeBins[r]){
				triggerRequestCoincidenceTimeBins[i].push_back(tBin);
			}
		}	
	
		for (int tBin1 : triggerRequestTimeBins[i]){
			for (int tBin2 : triggerRequestCoincidenceTimeBins[i]){
				if (fabs(float(tBin1-tBin2)) <= systemConfig->sw_fw_trigger_coinc_window){
					triggerExtendedAcceptedTimeBins[i/4].push_back(tBin1);
					continue;		
				}
			}
		}
		for (int tBin1 : triggerRequestTimeBins[i]){
			for (int tBin2 : triggerExtendedAcceptedTimeBins[i/4]){
				if (fabs(float(tBin1-tBin2)) <= systemConfig->sw_fw_trigger_coinc_window){
					triggerFilteredAcceptedTimeBins[i].push_back(tBin1);
					continue;			
				}
			}
		}
	}
	
	auto po = outBuffer->getPtr();
	
	for(auto iter = sortList.begin(); iter != sortList.end(); iter++){

		auto p = (*iter).p;

		SystemConfig::ChannelConfig &cc = systemConfig->getChannelConfig(p->channelID);
		SystemConfig::FirmwareConfig &cf = cc.empConfig[p->tacID];
	
		double energy = cf.p0 + cf.p1 * p->efine + cf.p2 * p->efine * p->efine;

		if(energy < 0.1875) energy = 0.1875;

		double timeWalkCorrection =  0.06 + cf.k0 / energy ;

		long long timeCorrected = p->time - (long long)(timeWalkCorrection);

		long long bufferBinnedTime = timeCorrected - timeMin;

		if(bufferBinnedTime < -100){continue;}
		if(bufferBinnedTime > triggerArraySize + 100){continue;}	

		unsigned reducedTriggerRegion = systemConfig->mapTriggerRegions[cc.triggerRegion];

		lReceived++;
		for(int tBin : triggerFilteredAcceptedTimeBins[reducedTriggerRegion]){
			if (((bufferBinnedTime - tBin) <= systemConfig->sw_fw_trigger_post_window) && ((bufferBinnedTime-tBin) >= -systemConfig->sw_fw_trigger_pre_window)){
				*po = *p;
				po++;
				lSent++;
				break;	
			}		
		}
	}

	for (int i = 0; i < nTriggerRegions; i++){ 
		delete[] energyArray[i]; 
		delete[] multiplicityArray[i]; 
	}

	delete[] energyArray;
	delete[] multiplicityArray;
	delete[] triggerRequestTimeBins;
	delete[] coincidenceRegionsAllowed;
	delete[] triggerRequestCoincidenceTimeBins; 
	delete[] triggerAcceptedTimeBins; 
	delete[] triggerExtendedAcceptedTimeBins; 
	delete[] triggerFilteredAcceptedTimeBins;

	atomicAdd(nReceived, lReceived);
	atomicAdd(nSent, lSent);

	outBuffer->setUsed(lSent);
	return outBuffer;
}

void HwTriggerSimulator::report()
{
	fprintf(stderr, ">> HwTriggerSimulator report\n");
	fprintf(stderr, " hits received\n");
	fprintf(stderr, "  %10lu total\n", nReceived);
	fprintf(stderr, " hits passed\n");
	fprintf(stderr, "  %10lu (%4.1f%%)\n", nSent, 100.0 * nSent / nReceived);

	UnorderedEventHandler<RawHit, RawHit>::report();
}
