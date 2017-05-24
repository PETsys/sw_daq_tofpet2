#ifndef __PETSYS__SHM_RAW_HPP__DEFINED__
#define __PETSYS__SHM_RAW_HPP__DEFINED__

#include <stdint.h>
#include <string>
#include <sys/mman.h>

namespace PETSYS {
	
static const int MaxRawDataFrameSize = 2048;
static const unsigned MaxRawDataFrameQueueSize = 16*1024;


struct RawDataFrame {
	uint64_t data[MaxRawDataFrameSize];

	unsigned getFrameSize() {
		uint64_t eventWord = data[0];
		return (eventWord >> 36) & 0x7FFF;
	};
	

	unsigned long long getFrameID() {
		uint64_t eventWord = data[0];
		return eventWord & 0xFFFFFFFFFULL;
	};

	bool getFrameLost() {
		uint64_t eventWord = data[1];
		return (eventWord & 0x18000) != 0;
	};

	int getNEvents() {
		uint64_t eventWord = data[1];
		return eventWord & 0x7FFF;
	}; 
	
	unsigned getEFine(int event) {
		uint64_t eventWord = data[event+2];
		unsigned v = eventWord % 1024;
		v = (v + 27) % 1024;	// rd_clk_en
		return v;
	};

	unsigned getTFine(int event) {
		uint64_t eventWord = data[event+2];
		unsigned v = (eventWord>>10) % 1024;
		v = (v + 27) % 1024;	// rd_clk_en
		return v;
	};

	unsigned getECoarse(int event) {
		uint64_t eventWord = data[event+2];
		return (eventWord>>20) % 1024;
	};
	
	unsigned getTCoarse(int event) {
		uint64_t eventWord = data[event+2];
		return (eventWord>>30) % 1024;
	};
	
	unsigned getTacID(int event) {
		uint64_t eventWord = data[event+2];
		return (eventWord>>40) % 4;
	};
	
	unsigned getChannelID(int event) {
		uint64_t eventWord = data[event+2];
		return eventWord>>42;
	};
	
};

class SHM_RAW {
public:
	SHM_RAW(std::string path);
	~SHM_RAW();

	unsigned long long getSizeInBytes();
	unsigned long long  getSizeInFrames() { 
		return MaxRawDataFrameQueueSize;
	};
	
	RawDataFrame *getRawDataFrame(int index) {
		RawDataFrame *dataFrame = &shm[index];
		return dataFrame;
	}

	unsigned long long getFrameWord(int index, int n) {
		RawDataFrame *dataFrame = &shm[index];
		uint64_t eventWord = dataFrame->data[n];
		return eventWord;
	};

	unsigned getFrameSize(int index) {
		return  getRawDataFrame(index)->getFrameSize();
	};
	

	unsigned long long getFrameID(int index) {
		return  getRawDataFrame(index)->getFrameID();
	};

	bool getFrameLost(int index) {
		return  getRawDataFrame(index)->getFrameLost();
	};

	int getNEvents(int index) {
		return  getRawDataFrame(index)->getNEvents();
	}; 
	
	unsigned getEFine(int index, int event) {
		return  getRawDataFrame(index)->getEFine(event);
	};

	unsigned getTFine(int index, int event) {
		return  getRawDataFrame(index)->getTFine(event);
	};

	unsigned getECoarse(int index, int event) {
		return  getRawDataFrame(index)->getECoarse(event);
	};
	
	unsigned getTCoarse(int index, int event) {
		return  getRawDataFrame(index)->getTCoarse(event);
	};
	
	unsigned getTacID(int index, int event) {
		return  getRawDataFrame(index)->getTacID(event);
	};
	
	unsigned getChannelID(int index, int event) {
		return  getRawDataFrame(index)->getChannelID(event);
	};

private:
	int shmfd;
	RawDataFrame *shm;
	off_t shmSize;
};

}
#endif
