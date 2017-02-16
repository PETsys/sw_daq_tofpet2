#ifndef __PETSYS_THREADPOOL_HPP__DEFINED__
#define __PETSYS_THREADPOOL_HPP__DEFINED__

#include <deque>
#include <vector>
#include <pthread.h>

namespace PETSYS {

	class ThreadPool {
	public:
		class Worker;
		class Job {
		public:
			bool isFinished();
			void wait();

			~Job();
		private:
			Job(void *(*start_routine)(void *), void *arg);

			void *(*start_routine)(void *);
			void *arg;

			bool finished;
			pthread_mutex_t lock;
			pthread_cond_t condJobFinished;

		friend class ThreadPool;
		friend class Worker;
		};

		class Worker {
		public:

			~Worker();
		private:
			Worker(ThreadPool *pool);		
	
			ThreadPool * pool;
			
			
			pthread_mutex_t lock;
			pthread_cond_t workerStart;
			pthread_cond_t workerFinished;
			pthread_t thread;
			
			static void *run(void *arg);
		
			friend class ThreadPool;
		};
	
		ThreadPool(unsigned maxWorkers);
		~ThreadPool();

		void clientIncrease();
		void clientDecrease();
		
		Job *queueJob(void *(*start_routine)(void *), void *arg);
		bool isFull();
		
	private:
		
		int nClients;
		std::vector<Worker *> workers;
		std::deque<Job *> queue;
		unsigned maxWorkers;
		unsigned maxQueueSize;
		
		bool die;
		pthread_mutex_t lock;
		pthread_cond_t condJobQueued;
		pthread_cond_t condJobStarted;
//		pthread_cond_t condJobFinished;
	};

#ifndef __PETSYS_THREADPOOL_CPP__DEFINED__
extern ThreadPool *GlobalThreadPool;
#endif 
}

#endif
