#ifndef __PETSYS_ENERGY_TRIGGER_HPP__DEFINED__
#define __PETSYS_ENERGY_TRIGGER_HPP__DEFINED__
#include <Event.hpp>
#include <UnorderedEventHandler.hpp>
#include <Instrumentation.hpp>
#include <SystemConfig.hpp>
#include <vector>

namespace PETSYS {

	 
	class HwTriggerSimulator : public UnorderedEventHandler<RawHit, RawHit> {
	public:
		HwTriggerSimulator (SystemConfig *systemConfig, EventSink<RawHit> *sink);
		void report();
	protected:
		virtual EventBuffer<RawHit> * handleEvents (EventBuffer<RawHit> *inBuffer);
	private:
		u_int64_t nReceived;
		u_int64_t nSent;
		SystemConfig *systemConfig;
		EventStream *eventStream;
	};
}
#endif 
