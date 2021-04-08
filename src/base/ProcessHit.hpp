#ifndef __PETSYS__PROCESS_HIT_HPP__DEFINED__
#define __PETSYS__PROCESS_HIT_HPP__DEFINED__

#include <UnorderedEventHandler.hpp>
#include <Event.hpp>
#include <SystemConfig.hpp>
#include <Instrumentation.hpp>


namespace PETSYS {
	
class ProcessHit : public UnorderedEventHandler<RawHit, Hit> {
private:
	SystemConfig *systemConfig;
	EventStream *eventStream;
	
	u_int64_t nReceived;
	u_int64_t nReceivedInvalid;
	u_int64_t nTDCCalibrationMissing;
	u_int64_t nQDCCalibrationMissing;
	u_int64_t nEnergyCalibrationMissing;
	u_int64_t nXYZMissing;
	u_int64_t nSent;
public:
	ProcessHit(SystemConfig *systemConfig, EventStream *eventStream, EventSink<Hit> *sink);
	virtual void report();
	
protected:
	virtual EventBuffer<Hit> * handleEvents (EventBuffer<RawHit> *inBuffer);
};
	
}

#endif // __PETSYS__PROCESS_HIT_HPP__DEFINED__
