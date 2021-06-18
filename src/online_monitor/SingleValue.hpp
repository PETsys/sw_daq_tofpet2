#ifndef __ONLINE_MONITOR_SINGLEVALUE_HPP__
#define __ONLINE_MONITOR_SINGLEVALUE_HPP__
#include <list>
#include <string>
#include <pthread.h>

#include "Monitor.hpp"

namespace PETSYS { namespace OnlineMonitor {
	
	using namespace std;
	
	class SingleValue : public Object {
	public:
		SingleValue(string n, Monitor *m, char *p = NULL);
		~SingleValue();
		
		virtual size_t getSize();
		virtual void init(char *p);
		virtual void reset();
		virtual void destroy();
		virtual string getGenerator();
		
		
		double getValue();
		void setValue(double v);
		void addToValue(double v);
	private:
		double value;
	};


}}
	

#endif
	
	
