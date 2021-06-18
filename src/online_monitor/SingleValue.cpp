#include "SingleValue.hpp"

using namespace std;

namespace PETSYS { namespace OnlineMonitor {

	SingleValue::SingleValue(string n, Monitor *m, char *p)
	: Object(n, m, p)
	{
	}
	
	SingleValue::~SingleValue()
	{
	}
	
	size_t SingleValue::getSize()
	{
		return sizeof(long long);
	}
	
	void SingleValue::init(char *p)
	{
		Object::init(p);
		reset();
	}
	
	void SingleValue::reset()
	{
		*(double *)ptr = 0;
	}
	
	void SingleValue::destroy()
	{
	}
	
	string SingleValue::getGenerator()
	{
		char s[256];
		size_t offset = ptr - monitor->getPtr();
		sprintf(s, "%lu\tSINGLEVALUE\t%s", offset, name.c_str());
		return string(s);	}
	
	double SingleValue::getValue()
	{
		return *(double *)ptr;
	}
	
	void SingleValue::setValue(double v)
	{
		*(double *)ptr = v;
	}
	
	void SingleValue::addToValue(double v)
	{
		*(double *)ptr += v;
	}
	
}}
