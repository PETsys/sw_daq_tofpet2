#ifndef __ONLINE_MONITOR_MONITOR_HPP__
#define __ONLINE_MONITOR_MONITOR_HPP__

#include <list>
#include <string>
#include <pthread.h>

namespace PETSYS { namespace OnlineMonitor {
	
	using namespace std;

	
	
	class Monitor;


	class Object {
	public:
		Object(string n, Monitor *m, char *p = NULL);
		virtual ~Object();
		
		virtual size_t getSize() = 0;
		virtual void init(char *p);
		virtual void destroy() = 0;
		virtual string getGenerator() = 0;
		virtual void reset() = 0;
		
	protected:
		string name;
		Monitor *monitor;
		char *ptr;
		
	};

	class Monitor {
	public:	
		Monitor(bool m = true);
		~Monitor();
		
		void addObject(Object *o);
		void materialize();
		void writeTOC(string fn);
		void resetAllObjects();
		
		void lock();
		void unlock();
		
		char *getPtr();
		
	private:
		bool master;
		int fd;
		size_t shmSize;
		char *ptr;
		pthread_mutex_t *mutex;
		list<Object *> objectList;
		
		void destroy();
		
		
	};
}}
	

#endif
	
	
