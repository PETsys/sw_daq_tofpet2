#include "SimpleGrouper.hpp"
#include <vector>
#include <math.h>

using namespace PETSYS;
using namespace std;

SimpleGrouper::SimpleGrouper(SystemConfig *systemConfig, EventSink<GammaPhoton> *sink) :
	systemConfig(systemConfig), OverlappedEventHandler<Hit, GammaPhoton>(sink, false)
{
	for(int i = 0; i < GammaPhoton::maxHits; i++)
		nHits[i] = 0;
	nHitsOverflow = 0;
	nPhotonsLowEnergy = 0;
	nPhotonsHighEnergy = 0;
}

SimpleGrouper::~SimpleGrouper()
{
}

void SimpleGrouper::report()
{
	u_int32_t nPhotons = 0;
	u_int32_t nTotalHits = 0;
	for(int i = 0; i < GammaPhoton::maxHits; i++) {
		nPhotons += nHits[i];
		nTotalHits += nHits[i] * (i+1);
	}
	nPhotons += nHitsOverflow;
	nPhotons += nPhotonsLowEnergy;
	nPhotons += nPhotonsHighEnergy;
		
	fprintf(stderr, ">> SimpleGrouper report\n");
	fprintf(stderr, " photons passed\n");
	fprintf(stderr, "  %10u total\n", nPhotons);
	for(int i = 0; i < GammaPhoton::maxHits; i++) {
		float fraction = nHits[i]/((float)nPhotons);
		if(fraction > 0.10) {
			fprintf(stderr, "  %10u (%4.1f%%) with %d hits\n", nHits[i], 100.0*fraction, i+1);
		}
	}
	fprintf(stderr, " photons rejected\n");
	fprintf(stderr, "  %10u (%4.1f%%) failed minimum energy\n", nPhotonsLowEnergy, 100.0*nPhotonsLowEnergy/nPhotons);
	fprintf(stderr, "  %10u (%4.1f%%) failed maximim energy\n", nPhotonsHighEnergy, 100.0*nPhotonsHighEnergy/nPhotons);
	fprintf(stderr, "  %10u (%4.1f%%) with more than %d hits\n", nHitsOverflow, 100.0*nHitsOverflow/nPhotons, GammaPhoton::maxHits);
	fprintf(stderr, "  %4.1f average hits/photon\n", float(nTotalHits)/float(nPhotons));
			
	OverlappedEventHandler<Hit, GammaPhoton>::report();
}

EventBuffer<GammaPhoton> * SimpleGrouper::handleEvents(EventBuffer<Hit> *inBuffer)
{
	// WARNING Move to SystemConfig
	double timeWindow1 = 100.0/6.25;
	int maxHits = 64;
	float radius2 = 1.0;
	float minEnergy = 0;
	float maxEnergy = 1E6;
	
	unsigned N =  inBuffer->getSize();
	EventBuffer<GammaPhoton> * outBuffer = new EventBuffer<GammaPhoton>(N, inBuffer);
	
	u_int32_t lHits[maxHits];	
	for(int i = 0; i < maxHits; i++)
	lHits[i] = 0;
	u_int32_t lHitsOverflow = 0;
	uint32_t lPhotonsLowEnergy = 0;
	uint32_t lPhotonsHighEnergy = 0;	

	vector<bool> taken(N, false);
	for(unsigned i = 0; i < N; i++) {
		Hit &hit = inBuffer->get(i);
		if(!hit.valid) continue;
	
		if (taken[i]) continue;
		taken[i] = true;
			
		Hit * hits[maxHits];
		hits[0] = &hit;
		int nHits = 1;
				
		for(int j = i+1; j < N; j++) {
			Hit &hit2 = inBuffer->get(j);
			if(!hit2.valid) continue;
			if(taken[j]) continue;
			
			if(!systemConfig->isMultiHitAllowed(hit2.region, hit.region)) continue;
			if((hit2.time - hit.time) > (overlap + timeWindow1)) break;
			
			float u = hit.x - hit2.x;
			float v = hit.y - hit2.y;
			float w = hit.z - hit2.z;
			float d2 = u*u + v*v + w*w;


			if(d2 > radius2) continue;
			if(fabs(hit.time - hit2.time) > timeWindow1) continue;

			taken[j] = true;

			if(nHits >= maxHits) {
				// Increase the hit count but don't actually add a hit
				nHits++;
			}
			else {
				hits[nHits] = &hit2;
				nHits++;
			}
		}
		
		if(nHits > maxHits) {
			// This event had too many hits
			// Count it and discard it
			lHitsOverflow += 1;
			continue;	
		}
		
		// Buble sorting..
		bool sorted = false;
		while(!sorted) {
			sorted = true;
			for(int k = 1; k < nHits; k++) {
				if(hits[k-1]->energy < hits[k]->energy) {
					sorted = false;
					Hit *tmp = hits[k-1];
					hits[k-1] = hits[k];
					hits[k] = tmp;
				}
			}
		}
		
		
		GammaPhoton &photon = outBuffer->getWriteSlot();
		for(int k = 0; k < nHits; k++) {
			photon.hits[k] = hits[k];
		}
		
		photon.nHits = nHits;		
		photon.region = photon.hits[0]->region;
		photon.time = photon.hits[0]->time;
		photon.x = photon.hits[0]->x;
		photon.y = photon.hits[0]->y;
		photon.z = photon.hits[0]->z;
		photon.energy = photon.hits[0]->energy;

		if(photon.energy < minEnergy) {
			lPhotonsLowEnergy += 1;
			continue;
		}
		if(photon.energy >maxEnergy) {
			lPhotonsHighEnergy += 1;
			continue;
		}
		
		photon.valid = true;
		outBuffer->pushWriteSlot();
		lHits[photon.nHits-1]++;
	}

	for(int i = 0; i < maxHits; i++)
		atomicAdd(nHits[i], lHits[i]);
	atomicAdd(nHitsOverflow, lHitsOverflow);
	atomicAdd(nPhotonsLowEnergy, lPhotonsLowEnergy);
	atomicAdd(nPhotonsHighEnergy, lPhotonsHighEnergy);
	
	return outBuffer;
}

