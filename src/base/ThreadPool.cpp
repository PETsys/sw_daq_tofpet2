#define __PETSYS_THREADPOOL_CPP__DEFINED__
#include "ThreadPool.hpp"
#include <unistd.h>
#include <stdio.h>
#include <climits>

using namespace PETSYS;
using namespace std;


namespace PETSYS {
	BaseThreadPool::BaseThreadPool() {
		this->maxWorkers = sysconf(_SC_NPROCESSORS_ONLN);
		this->queue = std::deque<pthread_t>();
	};
	
	BaseThreadPool::~BaseThreadPool() {
		this->completeQueue();
	}
	
	void BaseThreadPool::queueTask(void *buffer, void *sink)
	{
		while(queue.size() >= maxWorkers) {
			auto worker = queue.front();
			queue.pop_front();
			targ_t *targ;
			pthread_join(worker, NULL);
		}
		
		pthread_t worker;
		auto targ = new targ_t;
		targ->self = this;
		targ->buffer = buffer;
		targ->sink = sink;
		pthread_create(&worker, NULL, thread_routine, targ);
		queue.push_back(worker);
	}
	
	void BaseThreadPool::completeQueue()
	{
		while(queue.size() > 0) {
			auto worker = queue.front();
			queue.pop_front();
			pthread_join(worker, NULL);
		}
	}	

	void * BaseThreadPool::thread_routine(void *arg) {
		
		auto targ = (targ_t *)arg;
		
		targ->self->runTask(targ->buffer, targ->sink);
		
		delete targ;
		return NULL;
	}
	
	
	
}

