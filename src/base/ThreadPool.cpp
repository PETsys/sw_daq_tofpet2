#define __PETSYS_THREADPOOL_CPP__DEFINED__
#include "ThreadPool.hpp"
#include <unistd.h>
#include <stdio.h>
#include <climits>

using namespace PETSYS;
using namespace std;


namespace PETSYS {
	BaseThreadPool::BaseThreadPool() {
		auto nCPU = sysconf(_SC_NPROCESSORS_ONLN);

		queue = std::deque<job_t>();
		maxQueueSize = nCPU/4;
		if(maxQueueSize < 1) maxQueueSize = 1;

		terminate = false;
		pthread_mutex_init(&lock, NULL);
		pthread_cond_init(&cond_queued, NULL);
		pthread_cond_init(&cond_dequeued, NULL);
		pthread_cond_init(&cond_completed, NULL);

		nWorkers = nCPU;
		workers = new worker_t[nWorkers];
		for(int i = 0; i < nWorkers; i++) {
			workers[i].pool = this;
			workers[i].isBusy = false;
			pthread_create(&workers[i].thread, NULL, thread_routine, &workers[i]);
		}

	};
	
	BaseThreadPool::~BaseThreadPool() {
		this->completeQueue();

		pthread_mutex_lock(&lock);
		terminate = true;
		pthread_cond_broadcast(&cond_queued);
		pthread_mutex_unlock(&lock);

		for(int i = 0; i < nWorkers; i++) {
			pthread_join(workers[i].thread, NULL);
		}

		delete [] workers;

		pthread_cond_destroy(&cond_completed);
		pthread_cond_destroy(&cond_dequeued);
		pthread_cond_destroy(&cond_queued);
		pthread_mutex_destroy(&lock);
	}
	
	void BaseThreadPool::queueTask(void *buffer, void *sink)
	{
		pthread_mutex_lock(&lock);
		while(queue.size() >= maxQueueSize) {
			pthread_cond_wait(&cond_dequeued, &lock);
		}
		
		job_t job = {
				.b = buffer,
				.s = sink
			};

		queue.push_back(job);
		pthread_cond_signal(&cond_queued);
		pthread_mutex_unlock(&lock);
	}
	
	void BaseThreadPool::completeQueue()
	{
		pthread_mutex_lock(&lock);
		while (!queue.empty()) {
			pthread_cond_wait(&cond_dequeued, &lock);
		}

		for(int i = 0; i < nWorkers; i++) {
			if(workers[i].isBusy) {
				pthread_cond_wait(&cond_completed, &lock);
			}	
		}
		pthread_mutex_unlock(&lock);
	}	

	void * BaseThreadPool::thread_routine(void *arg)
	{
		worker_t *self = (worker_t *)arg;
		BaseThreadPool *pool = self->pool;

		pthread_mutex_lock(&pool->lock);
		while(!pool->terminate) {
			if(pool->queue.empty()) {
				pthread_cond_wait(&pool->cond_queued, &pool->lock);
				continue;
			}

			auto job = pool->queue.front();
			pool->queue.pop_front();
			self->isBusy = true;
			pthread_cond_signal(&pool->cond_dequeued);
			pthread_mutex_unlock(&pool->lock);
			
			pool->runTask(job.b, job.s);

			pthread_mutex_lock(&pool->lock);
			self->isBusy = false;
			pthread_cond_signal(&pool->cond_completed);
		}
		pthread_mutex_unlock(&pool->lock);
		return NULL;
	}
	
	
	
}

