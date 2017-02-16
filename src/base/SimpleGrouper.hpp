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
	
	u_int32_t nHits[GammaPhoton::maxHits];
	u_int32_t nHitsOverflow;
	uint32_t nPhotonsLowEnergy;
	uint32_t nPhotonsHighEnergy;
};

}
#endif // __PETSYS_SIMPLE_GROUPER_HPP__DEFINED__