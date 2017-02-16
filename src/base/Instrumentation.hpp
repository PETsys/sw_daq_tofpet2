#ifndef __PETSYS__INSTRUMENTATION_H__DEFINED__
#define __PETSYS__INSTRUMENTATION_H__DEFINED__

#include <sys/types.h>
namespace PETSYS {
	
	void atomicIncrement(volatile u_int32_t &val);
	void atomicAdd(volatile u_int32_t &val, u_int32_t increment);
	
}

#endif