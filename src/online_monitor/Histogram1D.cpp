#include "Histogram1D.hpp"
#include <math.h>
#include <string.h>

using namespace std;

namespace PETSYS { namespace OnlineMonitor {

	Histogram1D::Histogram1D(int nBins, double lowerX, double upperX, string n, Monitor *m, char *p)
	: Object(n, m, p)
	{
		this->nBins = nBins;
		this->lowerX = lowerX;
		this->upperX = upperX;
		this->binWidthReciprocal = nBins / (upperX - lowerX);
	}
	
	Histogram1D::~Histogram1D()
	{
	}

	size_t Histogram1D::getSize()
	{
		// Layout: bins, entries, acc
		return (nBins+1) * sizeof(double);
		
	}
	void Histogram1D::init(char *p)
	{
		Object::init(p);
		reset();
	}
	
	void Histogram1D::reset()
	{
		memset(ptr, 0, sizeof(double) * (nBins+1));
	}
	
	void Histogram1D::destroy()
	{
	}
	
	string Histogram1D::getGenerator()
	{
		char s[256];
		size_t offset = ptr - monitor->getPtr();
		sprintf(s, "%lu\tHISTOGRAM1D\t%s\t%d\t%e\t%e", offset, name.c_str(), nBins, lowerX, upperX);
		return string(s);
	}
	
	int Histogram1D::getBin(double x)
	{
		return floor(x * binWidthReciprocal);
	}
	
	
	void Histogram1D::fill(double x, double w)
	{
		int b = getBin(x);
		if(b < 0 || b >= nBins) return;
		
		double *p = (double *)ptr;
		*(p+b) += w;
		
		double *sum = p + nBins;
		*sum += w;
		
	}
	
	double Histogram1D::getBinContent(int b)
	{
		if(b < 0 || b >= nBins) return 0;
		
		double *p = (double *)ptr;
		return *(p+b);
	}
	
	double Histogram1D::getSum()
	{
		double *p = (double *)ptr;
		return *(p + nBins);
	}
	
}}
