#ifndef __DTFLYP_HPP__DEFINED__
#define __DTFLYP_HPP__DEFINED__

#include <stdint.h>
#include <queue>
#include <pthread.h>
#include <boost/crc.hpp>
#include "DAQFrameServer.hpp"

namespace DTFLY {
typedef unsigned long DWORD;
typedef unsigned long long UINT64;

extern "C" {
#include "dtfly_p_etpu_defs.h"
}

}

namespace DAQd {

class DtFlyP : public AbstractDAQCard {
public:
	  DtFlyP();
	  ~DtFlyP();
	  int getWords(uint64_t *buffer, int count);
	  void stopWorker();
	  void startWorker();
	bool cardOK();
	int sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength);
	int recvReply(char *buffer, int bufferSize);
	int setAcquistionOnOff(bool enable);
	uint64_t getPortUp();
	uint64_t getPortCounts(int channel, int whichCount);
	  static const int ETIMEOUT = -1;
	  static const int ENOWORDS = -2;
	  static const int ENOCARD = -10000;

private:
	int getWords_(uint64_t *buffer, int count);
	int WriteAndCheck(int reg, uint32_t *data, int count);
	int ReadAndCheck(int reg, uint32_t *data, int count);

	uint32_t txWrPointer;
	uint32_t rxRdPointer;


	pthread_t worker;
	pthread_mutex_t lock;
	pthread_cond_t condCleanBuffer;
	pthread_cond_t condDirtyBuffer;
	
	static const unsigned NB = 2;
	unsigned dmaBufferRdPtr;
	unsigned dmaBufferWrPtr;
	bool dmaBufferQueueIsEmpty();
	bool dmaBufferQueueIsFull();
	DTFLY::SBufferInit dmaBuffer[NB];
	volatile int wordBufferUsed[NB];
	volatile int wordBufferStatus[NB];
	
	
	volatile bool die;
	volatile bool hasWorker;
	static void *runWorker(void *arg);

	pthread_mutex_t hwLock;

	//FILE *logFile;

};

}
#endif
