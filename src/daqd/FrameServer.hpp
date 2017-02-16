#ifndef __PETSYS__FRAMESERVER_HPP__DEFINED__
#define __PETSYS__FRAMESERVER_HPP__DEFINED__

#include <pthread.h>
#include <queue>

#include <shm_raw.hpp>

namespace PETSYS {

struct CoincidenceTriggerConfig {
	uint32_t enable;
	uint32_t minToT;
	uint32_t cWindow;
	uint32_t preWindow;
	uint32_t postWindow;
	uint32_t singlesFraction;
	uint32_t mask[32];
};

class FrameServer {
public:
	FrameServer(int nFEB, int *feTypeMap, int debugLevel);
        virtual ~FrameServer();	

	// Sends a command and gets the reply
	// buffer is used to carry the command and the reply
	// bufferSize is the max capacity of the size
	// commandLength is the length of the command in
	// return the reply length or -1 if error
	virtual int sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength) = 0;
	
	virtual const char *getDataFrameSharedMemoryName();
	virtual unsigned getDataFrameWritePointer();
	virtual unsigned getDataFrameReadPointer();
	virtual void setDataFrameReadPointer(unsigned ptr);
	
	virtual void startAcquisition(int mode);
	virtual void stopAcquisition();
	
	virtual uint64_t getPortUp() = 0;
	virtual uint64_t getPortCounts(int port, int whichCount) = 0;

	virtual int setSorter(unsigned mode);
	virtual int setCoincidenceTrigger(CoincidenceTriggerConfig *config);
	virtual int setIdleTimeCalculation(unsigned mode);
	virtual int setGateEnable(unsigned mode);

	
private: 
	unsigned idleTimeMode;
	
protected:	
	static const int CommandTimeout = 250; // ms
	
	bool parseDataFrame(RawDataFrame *dataFrame);
	
	int debugLevel;
	
	int8_t *feTypeMap;
	
	int dataFrameSharedMemory_fd;
	RawDataFrame *dataFrameSharedMemory;
	
	static void *runWorker(void *);
	virtual void * doWork() = 0;
	void startWorker();
	void stopWorker();
	
	volatile bool die;
	pthread_t worker;
	volatile bool hasWorker;
	volatile int acquisitionMode;
	pthread_mutex_t lock;
	pthread_cond_t condCleanDataFrame;
	pthread_cond_t condDirtyDataFrame;
	unsigned dataFrameWritePointer;
	unsigned dataFrameReadPointer;
	
	

	struct reply_t {
		int size;
		char buffer[128];
	};
	std::queue<reply_t *> replyQueue;
	pthread_cond_t condReplyQueue;
	
	
};

}
#endif
