#include "Instrumentation.hpp"

namespace PETSYS {

	void atomicIncrement(volatile u_int32_t &val)
	{
	#if defined (__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 1)
		__sync_fetch_and_add(&val, 1);
	#elif defined (__GNUC__) && (defined (__i386__) || defined (__x86_64__))
		asm volatile("lock incl %0" : "+m" (val));
	#else
	#error "No atomic increment defined for this platform!"
	#endif 
	}
	
	void atomicAdd(volatile u_int32_t &val, u_int32_t increment)
	{
	#if defined (__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 1)
		__sync_fetch_and_add(&val, increment);
	#elif defined (__GNUC__) && (defined (__i386__) || defined (__x86_64__))
		asm volatile("lock addl %1, %0" : "+m" (val) : "r"(increment));
	#else
	#error "No atomic increment defined for this platform!"
	#endif 
	}

}