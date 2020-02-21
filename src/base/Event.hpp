#ifndef __PETSYS_EVENT_HPP__DEFINED__
#define __PETSYS_EVENT_HPP__DEFINED__

#include <cstddef>
#include <sys/types.h>
#include <stdint.h>

namespace PETSYS {

	struct RawHit {
		bool valid;
		bool qdcMode;
		double time;
		double timeEnd;
		unsigned int channelID;

		unsigned long frameID;
		unsigned short tcoarse;
		unsigned short ecoarse;
		unsigned short tfine;
		unsigned short efine;
		unsigned short tacID;
		
		RawHit() {
			valid = false;
		};
	};

	struct Hit {
		bool valid;
		RawHit *raw;
		double time;
		double timeEnd;
                float qfine;
		float energy;
                
		short region;
		short xi;
		short yi;
		float x;
		float y;
		float z;
		
		Hit() {
			valid = false;
			raw = NULL;
		};
	};

	struct GammaPhoton {
		static const int maxHits = 256;
		bool valid;
		double time;
		float energy;
		short region;
		float x, y, z;
		int nHits;
		Hit *hits[maxHits];

		GammaPhoton() {
			valid = false;
			for(int i = 0; i < maxHits; i++)
				hits[i] = NULL;
		};
	};

	struct Coincidence {
		static const int maxPhotons = 2;
		bool valid;
		double time;
		int nPhotons;
		GammaPhoton *photons[maxPhotons];
		
		Coincidence() {
			valid = false;
			for(int i = 0; i < maxPhotons; i++)
				photons[i] = NULL;
		};
	};
	
	class EventStream {
	public:
		//virtual bool isQDC(unsigned int gChannelID) = 0;
		virtual double getFrequency() = 0;
		virtual int getTriggerID() = 0;

		
	};
}
#endif
