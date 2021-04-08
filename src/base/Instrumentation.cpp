#include "Instrumentation.hpp"

namespace PETSYS {

	void atomicIncrement(volatile u_int64_t &val)
	{
	#if defined (__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 1)
		__sync_fetch_and_add(&val, 1);
	#else
	#error "No atomic increment defined for this platform!"
	#endif 
	}
	
	void atomicAdd(volatile u_int64_t &val, u_int64_t increment)
	{
	#if defined (__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 1)
		__sync_fetch_and_add(&val, increment);
	#else
	#error "No atomic increment defined for this platform!"
	#endif 
	}

}
