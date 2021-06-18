#include "Monitor.hpp"
#include <math.h>
#include <string.h>
#include <list>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

static const char *shmObjectPath = "/ps_monitor";

namespace PETSYS { namespace OnlineMonitor {
	
	Object::Object(string n, Monitor *m, char *p)
	{
		name = n;
		monitor = m;
		ptr = p;
		
		monitor->addObject(this);
	}
	
	Object::~Object()
	{
	}
	

	void Object::init(char *p)
	{
		ptr = p;
	}
	
	Monitor::Monitor(bool m)
	: master(m), fd(-1), ptr(NULL), objectList()
	{
		// We are not the master and thus we'll open the shared memory block
		if(!m) {
			fd = shm_open(shmObjectPath, 
					O_RDWR, 
					S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
			if (fd < 0) {
				fprintf(stderr, "Opening '%s' returned %d (errno = %d)\n", shmObjectPath, fd, errno );		
				exit(1);
			}
			shmSize = lseek(fd, 0, SEEK_END);
		
			auto p = ptr = (char *)mmap(NULL, 
						shmSize,
						PROT_READ | PROT_WRITE, 
						MAP_SHARED, 
						fd,
						0);			
					
			
			mutex = (pthread_mutex_t *)p;
		}
		
	}
	
	Monitor::~Monitor()
	{
		if(master)
			destroy();
	}
	
	char * Monitor::getPtr()
	{
		return ptr;
	}
	
	void Monitor::addObject(Object *o)
	{
		objectList.push_back(o);
	}
	
	void Monitor::destroy()
	{
		for(auto iter = objectList.begin(); iter != objectList.end(); iter++) {
			(*iter)->destroy();
			
		}

		if(ptr != NULL) {
			pthread_mutex_destroy(mutex);
			munmap(ptr, shmSize);
		}
		ptr = NULL;
			
		if(fd != -1) {
			close(fd);
		}
		fd = -1;		
		shm_unlink(shmObjectPath);
	}
	
	void Monitor::lock()
	{
		pthread_mutex_lock(mutex);
	}
	
	void Monitor::unlock()
	{
		pthread_mutex_unlock(mutex);
	}
	
	void Monitor::materialize()
	{
		if(ptr != NULL) {
			destroy();
		}

		shmSize = sizeof(pthread_mutex_t);
		for(auto iter = objectList.begin(); iter != objectList.end(); iter++) {
			shmSize += (*iter)->getSize();
			
		}
		
		fd = shm_open(shmObjectPath, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if(fd < 0) {
			perror("Error creating shared memory");
			fprintf(stderr, "Could not create  /dev/shm%s\n", shmObjectPath);
			exit(1);
		}
	
		ftruncate(fd, shmSize);
		
		auto p = ptr = (char *)mmap(NULL, 
				shmSize, 
				PROT_READ | PROT_WRITE, 
				MAP_SHARED, 
				fd, 
				0);
		if(ptr == NULL) {
			perror("Error mmaping() shared memory");
			exit(1);
		}
		
		mutex = (pthread_mutex_t *)p;
		pthread_mutex_init(mutex, NULL);
		p += sizeof(pthread_mutex_t);
		
		for(auto iter = objectList.begin(); iter != objectList.end(); iter++) {
			(*iter)->init(p);
			p += (*iter)->getSize();
		}
	}
	
	void Monitor::writeTOC(string fn)
	{
		FILE * f = fopen(fn.c_str(), "w");
		for(auto iter = objectList.begin(); iter != objectList.end(); iter++) {
			fprintf(f, "%s\n", (*iter)->getGenerator().c_str());
		}
		fprintf(f, "%lu\tEND\n", shmSize);
		fclose(f);
		
	}
	
	void Monitor::resetAllObjects()
	{
		for(auto iter = objectList.begin(); iter != objectList.end(); iter++) {
			(*iter)->reset();
		}
	}
}}
