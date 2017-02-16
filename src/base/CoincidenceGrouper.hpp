#ifndef __PETSYS__COINCIDENCEGROUPER_HPP__DEFINED__
#define __PETSYS__COINCIDENCEGROUPER_HPP__DEFINED__

#include <Event.hpp>
#include <OverlappedEventHandler.hpp>
#include <Instrumentation.hpp>
#include <SystemConfig.hpp>

namespace PETSYS {

class CoincidenceGrouper : public OverlappedEventHandler<GammaPhoton, Coincidence> {
public:
	CoincidenceGrouper(SystemConfig *systemConfig, EventSink<Coincidence> *sink);
	~CoincidenceGrouper();
	virtual void report();
	
private:
	virtual EventBuffer<Coincidence> * handleEvents(EventBuffer<GammaPhoton> *inBuffer);
		
	SystemConfig *systemConfig;
	u_int32_t nPrompts;
};
}
#endif // __PETSYS__COINCIDENCEGROUPER_HPP__DEFINED__
