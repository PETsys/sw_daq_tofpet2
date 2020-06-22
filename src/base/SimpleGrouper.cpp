#include "SimpleGrouper.hpp"
#include <vector>
#include <math.h>

using namespace PETSYS;
using namespace std;

SimpleGrouper::SimpleGrouper(SystemConfig *systemConfig, EventSink<GammaPhoton> *sink) :
	systemConfig(systemConfig), OverlappedEventHandler<Hit, GammaPhoton>(sink, false)
{
	for(int i = 0; i < GammaPhoton::maxHits; i++)
		nPhotonsHits[i] = 0;
	
	nHitsReceived = 0;
	nHitsReceivedValid = 0;
	nPhotonsFound = 0;
	nPhotonsHitsOverflow = 0;
	nPhotonsLowEnergy = 0;
	nPhotonsHighEnergy = 0;
	nPhotonsPassed = 0;
}

SimpleGrouper::~SimpleGrouper()
{
}

void SimpleGrouper::report()
{
	int maxHits = systemConfig->sw_trigger_group_max_hits;
	if (maxHits > GammaPhoton::maxHits) maxHits = maxHits;

	fprintf(stderr, ">> SimpleGrouper report\n");
	fprintf(stderr, " hits received\n");
	fprintf(stderr, "  %10u total\n", nHitsReceived);
	fprintf(stderr, "  %10u (%4.1f%%) invalid\n", nHitsReceived - nHitsReceivedValid, 100.0 * (nHitsReceived - nHitsReceivedValid)/nHitsReceived);
	fprintf(stderr, " photons found\n");
	fprintf(stderr, "  %10u total\n", nPhotonsFound);
	for(int i = 0; i < maxHits; i++) {
		float fraction = nPhotonsHits[i]/((float)nPhotonsFound);
		if(fraction > 0.05) {
			fprintf(stderr, "  %10u (%4.1f%%) with %d hits\n", nPhotonsHits[i], 100.0*fraction, i+1);
		}
	}
	fprintf(stderr, "  %10u (%4.1f%%) with more than %d hits\n", nPhotonsHitsOverflow, 100.0*nPhotonsHitsOverflow/nPhotonsFound, maxHits);
	fprintf(stderr, " photons rejected\n");
	fprintf(stderr, "  %10u (%4.1f%%) failed minimum energy\n", nPhotonsLowEnergy, 100.0*nPhotonsLowEnergy/nPhotonsFound);
	fprintf(stderr, "  %10u (%4.1f%%) failed maximim energy\n", nPhotonsHighEnergy, 100.0*nPhotonsHighEnergy/nPhotonsFound);
	fprintf(stderr, " photons passed\n");
	fprintf(stderr, "  %10u (%4.1f%%) passed\n", nPhotonsPassed, 100.0*nPhotonsPassed/nPhotonsFound);
			
	OverlappedEventHandler<Hit, GammaPhoton>::report();
}

EventBuffer<GammaPhoton> * SimpleGrouper::handleEvents(EventBuffer<Hit> *inBuffer)
{
	
	double timeWindow1 = systemConfig->sw_trigger_group_time_window;
	float radius2 = (systemConfig->sw_trigger_group_max_distance)*(systemConfig->sw_trigger_group_max_distance);
	float minEnergy = systemConfig->sw_trigger_group_min_energy;
	float maxEnergy = systemConfig->sw_trigger_group_max_energy;
	int maxHits = systemConfig->sw_trigger_group_max_hits;
	if (maxHits > GammaPhoton::maxHits) maxHits = maxHits;

	
	uint32_t lPhotonsHits[maxHits];
	for(int i = 0; i < maxHits; i++) {
		lPhotonsHits[i] = 0;
	}
	
	uint32_t lHitsReceived = 0;
	uint32_t lHitsReceivedValid = 0;
	uint32_t lPhotonsFound = 0;
	uint32_t lPhotonsHitsOverflow = 0;
	uint32_t lPhotonsLowEnergy = 0;
	uint32_t lPhotonsHighEnergy = 0;
	uint32_t lPhotonsPassed = 0;

	unsigned N =  inBuffer->getSize();
	EventBuffer<GammaPhoton> * outBuffer = new EventBuffer<GammaPhoton>(N, inBuffer);
	vector<bool> taken(N, false);
	Hit * hits[maxHits];
	
	for(unsigned i = 0; i < N; i++) {
		// Do accounting first
		Hit &hit = inBuffer->get(i);
		lHitsReceived += 1;

		if(!hit.valid) continue;
		lHitsReceivedValid += 1;

		if (taken[i]) continue;
		taken[i] = true;
			
		uint8_t eventFlags = 0x0;
		hits[0] = &hit;
		int nHits = 1;
				
		for(int j = i+1; j < N; j++) {
			Hit &hit2 = inBuffer->get(j);
			if(!hit2.valid) continue;

			if(taken[j]) continue;
			
			// Stop searching for more hits for this photon
			if((hit2.time - hit.time) > (overlap + timeWindow1)) break;
			
			// if(!systemConfig->isMultiHitAllowed(hit2.region, hit.region)) continue;
			if(fabs(hit.time - hit2.time) > timeWindow1) continue;

			float u = hit.x - hit2.x;
			float v = hit.y - hit2.y;
			float w = hit.z - hit2.z;
			float d2 = u*u + v*v + w*w;
			if(d2 > radius2) continue;
			
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
			// Flag this event has having excessive hits	
			eventFlags |= 0x1;
			// and set the number of hits to maximum hits, as code below depends on it
			nHits = maxHits;
		}
		
		// Buble sorting to put highest energy event first
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
		
		
		// Assemble the output structure
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

		if(photon.energy < minEnergy) eventFlags |= 0x2;
		if(photon.energy > maxEnergy) eventFlags |= 0x4;

		
		// Count photons
		lPhotonsFound += 1;
		if((eventFlags & 0x1) == 0) {
			lPhotonsHits[photon.nHits-1] += 1;
		}
		else {
			lPhotonsHitsOverflow += 1;
		}
		
		if((eventFlags & 0x2) != 0) lPhotonsLowEnergy += 1;
		if((eventFlags & 0x4) != 0) lPhotonsHighEnergy += 1;
		
		if(eventFlags == 0) {
			lPhotonsPassed += 1;
			photon.valid = true;
			outBuffer->pushWriteSlot();
		}
	}

	for(int i = 0; i < maxHits; i++)
		atomicAdd(nPhotonsHits[i], lPhotonsHits[i]);
	
	atomicAdd(nHitsReceived, lHitsReceived);
	atomicAdd(nHitsReceivedValid, lHitsReceivedValid);
	atomicAdd(nPhotonsFound, lPhotonsFound);
	atomicAdd(nPhotonsHitsOverflow, lPhotonsHitsOverflow);
	atomicAdd(nPhotonsLowEnergy, lPhotonsLowEnergy);
	atomicAdd(nPhotonsHighEnergy, lPhotonsHighEnergy);
	atomicAdd(nPhotonsPassed, lPhotonsPassed);
	
	return outBuffer;
}

