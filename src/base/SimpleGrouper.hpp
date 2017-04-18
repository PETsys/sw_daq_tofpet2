#ifndef __PETSYS_SIMPLE_GROUPER_HPP__DEFINED__
#define __PETSYS_SIMPLE_GROUPER_HPP__DEFINED__

#include <SystemConfig.hpp>
#include <OverlappedEventHandler.hpp>
#include <Event.hpp>
#include <Instrumentation.hpp>

namespace PETSYS {
	
class SimpleGrouper : public OverlappedEventHandler<Hit, GammaPhoton> {
public:
	SimpleGrouper(SystemConfig *systemConfig, EventSink<GammaPhoton> *sink);
	~SimpleGrouper();
	
	virtual void report();
	
protected:
	virtual EventBuffer<GammaPhoton> * handleEvents(EventBuffer<Hit> *inBuffer);
		
private:
	SystemConfig *systemConfig;
	
	uint32_t nHitsReceived;
	uint32_t nHitsReceivedValid;
	uint32_t nPhotonsFound;
	uint32_t nPhotonsHits[GammaPhoton::maxHits];
	uint32_t nPhotonsHitsOverflow;
	uint32_t nPhotonsLowEnergy;
	uint32_t nPhotonsHighEnergy;
	uint32_t nPhotonsPassed;
};

}
#endif // __PETSYS_SIMPLE_GROUPER_HPP__DEFINED__