#ifndef __ONLINE_MONITOR_HISTOGRAM1D_HPP__
#define __ONLINE_MONITOR_HISTOGRAM1D_HPP__

#include <list>
#include <string>
#include <pthread.h>

#include "Monitor.hpp"

namespace PETSYS { namespace OnlineMonitor {

	class Histogram1D : public Object {
	public:		
		Histogram1D(int nBins, double lowerX, double upperX, string n, Monitor *m, char *p = NULL);
		virtual ~Histogram1D();

		virtual size_t getSize();
		virtual void init(char *p);
		virtual void reset();
		virtual void destroy();
		virtual string getGenerator();
		
		void fill(double x, double w);
		int getBin(double x);
		double getBinContent(int b);
		double getSum();
		
		
	private:
		int nBins;
		double lowerX;
		double upperX;
		double binWidthReciprocal;
		
	};

}}
	

#endif
	
	
