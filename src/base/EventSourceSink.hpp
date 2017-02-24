#ifndef __PETSYS__EVENTSOURCESINK_HPP__DEFINED__
#define __PETSYS__EVENTSOURCESINK_HPP__DEFINED__
#include <stdio.h>
#include "EventBuffer.hpp"

namespace PETSYS {
	template <class TEventInput>
	class EventSink {
	public:
		virtual void pushT0(double t0) = 0;
		virtual void pushEvents(EventBuffer<TEventInput> *buffer) = 0;
		virtual void finish() = 0;
		virtual void report() = 0;
		virtual ~EventSink() {};
	};

	template <class TEventOutput>
	class EventSource {
	public:
		EventSource(EventSink<TEventOutput> *sink) {
			this->sink = sink;
		};
		
		virtual ~EventSource() {
			delete this->sink;
		};
	protected:
		EventSink<TEventOutput> *sink;

	};
	
	template <class TEventInput> 
	class NullSink : public EventSink<TEventInput> {
	public:
		NullSink() { lastBuffer = NULL; };
		virtual void pushT0(double t0) {};
		virtual void pushEvents(EventBuffer<TEventInput> *buffer) {
			// Do not delete this buffer immediable, because the next buffer 
			// may hold pointers into one of the parent buffers.
			delete lastBuffer;
			lastBuffer = buffer;
		};
		
		virtual void finish() {
		};
		
		virtual void report() {
		};
		~NullSink() {
			delete lastBuffer;
		};
	private:
		AbstractEventBuffer *lastBuffer;
	};
	
}
#endif
