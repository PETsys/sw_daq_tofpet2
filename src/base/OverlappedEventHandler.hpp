#ifndef __PETSYS_OVERLAPPEDEVENTHANDLER_HPP__DEFINED__
#define __PETSYS_OVERLAPPEDEVENTHANDLER_HPP__DEFINED__
#include "EventSourceSink.hpp"
#include "EventBuffer.hpp"
#include <pthread.h>
#include <deque>
#include <time.h>
#include "ThreadPool.hpp"

namespace PETSYS {

	template <class TEventInput, class TEventOutput>
	class OverlappedEventHandler : 
		public EventSink<TEventInput>,
		public EventSource<TEventOutput> {
	public:
#if  __cplusplus >= 201103L		
		static constexpr double overlap = 40.0; // 200 ns @ 200 MHz;
#else
		static const double overlap = 40.0; // 200 ns @ 200 MHz;
#endif
	
		OverlappedEventHandler(EventSink<TEventOutput> *sink, bool singleWorker = false, ThreadPool *pool = GlobalThreadPool);
		~OverlappedEventHandler();
		virtual void pushT0(double t0);
		virtual void pushEvents(EventBuffer<TEventInput> *buffer);
		virtual void finish();
		virtual void report();

	protected:
		virtual EventBuffer<TEventOutput> * handleEvents(EventBuffer<TEventInput> *inBuffer) = 0;
		


	private:
		bool singleWorker;
		ThreadPool *threadPool;
		
		unsigned peakThreads;
		unsigned long long stepProcessingNBlocks;
		double stepProcessingTime;
		unsigned long long stepProcessingNInputEvents;

		void extractWorker();

		class Worker {
		public:
			Worker(OverlappedEventHandler<TEventInput, TEventOutput> *master, 
				EventBuffer<TEventInput> *inBuffer);
			~Worker();

		
			OverlappedEventHandler<TEventInput, TEventOutput> *master;
			EventBuffer<TEventInput> *inBuffer;
			EventBuffer<TEventOutput> *outBuffer;
			
			ThreadPool::Job *job;
			
		
			bool isFinished();
			void wait();
			double runTime;
			long runEvents;
			static void* run(void *);		
		};

		std::deque<Worker *> workers;
	};

#include "OverlappedEventHandler.tpp"

}
#endif
