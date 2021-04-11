#ifndef __PETSYS_EVENTBUFFER_HPP__DEFINED__
#define __PETSYS_EVENTBUFFER_HPP__DEFINED__
#include "Event.hpp"
#include <stdlib.h>

namespace PETSYS {

	class AbstractEventBuffer {
	public:
		AbstractEventBuffer(AbstractEventBuffer *parent) 
		: parent(parent), bufferSeqN(parent->bufferSeqN), bufferTMin(parent->bufferTMin) 
		{
		};
		
		AbstractEventBuffer(u_int64_t seqN, long long tMin)
		: parent(NULL), bufferSeqN(seqN), bufferTMin(tMin)
		{
		};

		virtual ~AbstractEventBuffer()
		{
			delete parent;
		};

		size_t getSeqN() {
			return bufferSeqN;
		};
		
		long long getTMin() {
			return bufferTMin;
		};

		long long getTMax() {
			return bufferTMax;
		};
		
		void setTMax(long long t) {
			bufferTMax = t;
		}

		private:
		AbstractEventBuffer * parent;
		u_int64_t bufferSeqN;
		long long bufferTMin;
		long long bufferTMax;
		
		
	};
	
	template <class TEvent>
	class EventBuffer : public AbstractEventBuffer {
	public:

		EventBuffer(unsigned initialCapacity, AbstractEventBuffer *parent)
			: AbstractEventBuffer(parent) 
		{
			initialCapacity = ((initialCapacity / 1024) + 1) * 1024;
			buffer = (TEvent *)malloc(sizeof(TEvent)*initialCapacity);
			capacity = initialCapacity;
			used = 0;
		};
		
		EventBuffer(unsigned initialCapacity, unsigned seqN, long long tMin)
			: AbstractEventBuffer(seqN, tMin) 
		{
			initialCapacity = ((initialCapacity / 1024) + 1) * 1024;
			buffer = (TEvent *)malloc(sizeof(TEvent)*initialCapacity);
			capacity = initialCapacity;
			used = 0;
		};
		
		void setCapacity(size_t n) {
			reserve(n);
		};

		size_t getCapacity() {
			return capacity;
		};

		void reserve(size_t newCapacity) {
			if (newCapacity <= capacity) 
				return;
			
			TEvent * reBuffer = (TEvent *)realloc((void*)buffer, sizeof(TEvent)*newCapacity);
			buffer = reBuffer;
			capacity = newCapacity;			
		};

		TEvent &getWriteSlot() {
			if(used >= capacity) {
				size_t increment = ((capacity / 10240) + 1) * 1024;
				capacity += increment;				
				TEvent * reBuffer = (TEvent *)realloc((void*)buffer, sizeof(TEvent)*capacity);
				buffer = reBuffer;
			}
			return buffer[used];	
		};

		void pushWriteSlot() {
			used++;
		};

		void push(TEvent &e) {
			getWriteSlot() = e;
			pushWriteSlot();
		};

		TEvent & get(size_t index) {
			return buffer[index];
		};

		TEvent & getLast() {
			return get(getSize()-1);
		};

		size_t getSize() {
			return used;
		};

		size_t getUsed() {
			return used;
		}

		void setUsed(size_t n) { 
			used = n;
		};

		size_t getFree() {
			return getCapacity() - getUsed();
		};

		TEvent *getPtr() {
			return buffer;
		};

		virtual ~EventBuffer() {
			free((void*)buffer);
		};
		

	private:
		TEvent *buffer;
		size_t capacity;
		size_t used;
		
		
	};

}
#endif
