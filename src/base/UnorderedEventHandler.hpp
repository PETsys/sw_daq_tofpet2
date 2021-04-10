#ifndef __PETSYS_UNORDEREDEVENTHANDLER_HPP__DEFINED__
#define __PETSYS_UNORDEREDEVENTHANDLER_HPP__DEFINED__
#include "EventSourceSink.hpp"
#include "EventBuffer.hpp"
#include <pthread.h>

namespace PETSYS {

	template <class TEventInput, class TEventOutput>
	class UnorderedEventHandler : 
		public EventSink<TEventInput>,
		public EventSource<TEventOutput> {
	public:
		UnorderedEventHandler(EventSink<TEventOutput> *sink) : 
		EventSource<TEventOutput>(sink) {
		};
		
		~UnorderedEventHandler() {
		};
		
		virtual void pushT0(double t0) {
			this->sink->pushT0(t0);
		};
		
		virtual void pushEvents(EventBuffer<TEventInput> *buffer) {
			auto newBuffer = handleEvents(buffer);
			this->sink->pushEvents(newBuffer);
		};
		
		
		virtual void finish() {
			this->sink->finish();
		};
		
		virtual void report() {
			this->sink->report();
		};
		
	protected:
		virtual EventBuffer<TEventOutput> * handleEvents(EventBuffer<TEventInput> *inBuffer) = 0;		

	};

}
#endif
