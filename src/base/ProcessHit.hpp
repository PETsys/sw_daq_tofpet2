#ifndef __PETSYS__PROCESS_HIT_HPP__DEFINED__
#define __PETSYS__PROCESS_HIT_HPP__DEFINED__

#include <OverlappedEventHandler.hpp>
#include <Event.hpp>
#include <SystemConfig.hpp>

namespace PETSYS {
	
class ProcessHit : public OverlappedEventHandler<RawHit, Hit> {
private:
	SystemConfig *systemConfig;
	bool qdcMode;
public:
	ProcessHit(SystemConfig *systemConfig, bool qdcMode, EventSink<Hit> *sink);
	
protected:
	virtual EventBuffer<Hit> * handleEvents (EventBuffer<RawHit> *inBuffer);
};
	
}

#endif // __PETSYS__PROCESS_HIT_HPP__DEFINED__