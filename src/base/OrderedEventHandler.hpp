#ifndef __PETSYS_ORDEREDEVENTHANDLER_HPP__DEFINED__
#define __PETSYS_ORDEREDEVENTHANDLER_HPP__DEFINED__
#include "EventSourceSink.hpp"
#include "EventBuffer.hpp"
#include <pthread.h>
#include <map>

namespace PETSYS {

	template <class TEventInput, class TEventOutput>
	class OrderedEventHandler : 
		public EventSink<TEventInput>,
		public EventSource<TEventOutput> {
	public:
		OrderedEventHandler(EventSink<TEventOutput> *sink) : 
		EventSource<TEventOutput>(sink) {
			pthread_mutex_init(&lock, NULL);
			expectedSeqN = 0;
			queue = std::map<size_t, pthread_cond_t *>();
		};
		
		~OrderedEventHandler() {
			pthread_mutex_destroy(&lock);
		};
		
		virtual void pushT0(double t0) {
			this->sink->pushT0(t0);
		};
		
		virtual void pushEvents(EventBuffer<TEventInput> *buffer) {
			size_t mySeqN = buffer->getSeqN();
			pthread_cond_t cond;
			pthread_cond_init(&cond, NULL);
			pthread_mutex_lock(&lock);
			queue[mySeqN] = &cond;
			while(mySeqN != expectedSeqN) {
				// Not our turn yet
				pthread_cond_wait(&cond, &lock);
			}
			queue.erase(mySeqN);
			pthread_cond_destroy(&cond);
			pthread_mutex_unlock(&lock);
			// Process the data
			auto newBuffer = handleEvents(buffer);
		
			pthread_mutex_lock(&lock);
			// Increment expected sequence number and signal any waiting workers
			expectedSeqN += 1;
			if(queue.count(expectedSeqN) > 0) {
				pthread_cond_signal(queue[expectedSeqN]);
			}
			pthread_mutex_unlock(&lock);

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

	private:
		u_int64_t expectedSeqN;
		pthread_mutex_t lock;
		std::map<size_t, pthread_cond_t *> queue;
		
	};

}
#endif
