#ifndef __PETSYS_THREADPOOL_HPP__DEFINED__
#define __PETSYS_THREADPOOL_HPP__DEFINED__

#include <deque>
#include <vector>
#include <pthread.h>
#include "EventSourceSink.hpp"
#include "EventBuffer.hpp"

namespace PETSYS {
	
	class BaseThreadPool {
	private:
		struct worker_t{
			BaseThreadPool *pool;
			pthread_t thread;
			bool isBusy;
		};

		struct job_t{
			void *b;
			void *s;
		};
	public:
		BaseThreadPool();
		virtual ~BaseThreadPool();
		void completeQueue();
		
	protected:
		void queueTask(void *buffer, void *sink);
		virtual void runTask(void *b, void *s) = 0;
		
		
	
	private:
		int maxQueueSize;
		std::deque<job_t> queue;

		int nWorkers;
		worker_t *workers;

		pthread_mutex_t lock;
		pthread_cond_t cond_queued;
		pthread_cond_t cond_dequeued;
		pthread_cond_t cond_completed;
		bool terminate;
		
		static void *thread_routine(void *);
		
		
		
	};
	
	template <class TEvent>
	class ThreadPool : public BaseThreadPool {
	public:	
		ThreadPool() { };
		virtual ~ThreadPool() { };
		
		void queueTask(EventBuffer<TEvent> *buffer, EventSink<TEvent> *sink) {
			BaseThreadPool::queueTask((void *)buffer, (void *)sink);
		};
		
	private: 
		virtual void runTask(void *b, void *s) {
			auto buffer = (EventBuffer<TEvent> *)b;
			auto sink = (EventSink<TEvent> *)s;
			sink->pushEvents(buffer);
			
		}
		
	};
}

#endif
