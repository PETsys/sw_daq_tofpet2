#include "CoincidenceGrouper.hpp"
#include <math.h>
#include <algorithm>
#include <vector>

using namespace PETSYS;

CoincidenceGrouper::CoincidenceGrouper(SystemConfig *systemConfig, EventSink<Coincidence> *sink)
	: systemConfig(systemConfig), UnorderedEventHandler<GammaPhoton, Coincidence>(sink)
{
	resetCounters();
}

CoincidenceGrouper::~CoincidenceGrouper()
{
}

EventBuffer<Coincidence> * CoincidenceGrouper::handleEvents(EventBuffer<GammaPhoton> *inBuffer)
{
	double cWindow = systemConfig->sw_trigger_coincidence_time_window;
	double Tps = 5000; //harcoded clock time in ps!!!!!!!!!!
	long long tMin = inBuffer->getTMin() * (long long)Tps; 
	unsigned N =  inBuffer->getSize();
	EventBuffer<Coincidence> * outBuffer = new EventBuffer<Coincidence>(N, inBuffer);
	//bool useListControlData = systemConfig->useListModeControlData();
	u_int64_t lPrompts = 0;
	u_int64_t lHits = 0;
	u_int64_t lCoincPhotopeak = 0;
	for(unsigned i = 0; i < N; i++) {
		GammaPhoton &photon1 = inBuffer->get(i);
		for(unsigned j = i+1; j < N; j++) {
			GammaPhoton &photon2 = inBuffer->get(j);			
			if ((photon2.time - photon1.time) > (cWindow + MAX_UNORDER)) break;
			
			if(!systemConfig->isCoincidenceAllowed(photon1.region, photon2.region)) continue;
			
			if(fabs(photon1.time - photon2.time) <= cWindow) {
				Coincidence &c = outBuffer->getWriteSlot();
				c.nPhotons = 2;
				// if(useListControlData){
				// 	for(int i = 0 ; i < photon1.nHits ; i++){	
				// 		long long timeInPs1 = ((long long)(photon1.hits[i]->time * Tps)) + tMin;
				// 		long long index1 = timeInPs1/0.01E12;
				// 		bool foundCoinc = false;
				// 		for(int j = 0 ; j < photon2.nHits ; j++){
				// 			long long timeInPs2 = ((long long)(photon2.hits[j]->time * Tps)) + tMin;
				// 			if (std::find(systemConfig->CoincPhotopeakTimes[index1].begin(), systemConfig->CoincPhotopeakTimes[index1].end(), std::make_pair(timeInPs1, timeInPs2)) != systemConfig->CoincPhotopeakTimes[index1].end()){
				// 				lCoincPhotopeak++;
				// 				foundCoinc = true;
				// 				break;
				// 			}
				// 			if (std::find(systemConfig->CoincPhotopeakTimes[index1].begin(), systemConfig->CoincPhotopeakTimes[index1].end(), std::make_pair(timeInPs2, timeInPs1)) != systemConfig->CoincPhotopeakTimes[index1].end()){
				// 				lCoincPhotopeak++;
				// 				foundCoinc = true;
				// 				break;
				// 			}
				// 		}
				// 		if(foundCoinc)break;
				// 	}
				// }

				bool first1 = photon1.region > photon2.region;
				c.photons[0] = first1 ? &photon1 : &photon2;
				c.photons[1] = first1 ? &photon2 : &photon1;
				c.valid = true;
				outBuffer->pushWriteSlot();
				lPrompts++;
			}
		}
	}
	atomicAdd(nPrompts, lPrompts);
	atomicAdd(nCoincPhotopeak, lCoincPhotopeak);
	return outBuffer;
}

void CoincidenceGrouper::resetCounters()
{
	nPrompts = 0;
	UnorderedEventHandler<GammaPhoton, Coincidence>::report();
}

void CoincidenceGrouper::report()
{
	fprintf(stderr, ">> CoincidenceGrouper report\n");
	fprintf(stderr, " prompts passed\n");
	fprintf(stderr, "  %10lu \n", nPrompts);
	UnorderedEventHandler<GammaPhoton, Coincidence>::report();
}
