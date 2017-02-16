#ifndef __PFP_KX7_HPP__DEFINED__
#define __PFP_KX7_HPP__DEFINED__

#include <stdint.h>
#include <queue>
#include <pthread.h>
#include <boost/crc.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"
#include "DAQFrameServer.hpp"

extern "C" {
#include <pfp_api_monitor.h>
}
#ifdef LINUX
	#include <fcntl.h>
	#include <errno.h>
#else
	#include <conio.h>
#endif

namespace DAQd {
class PFP_KX7 : public AbstractDAQCard {
public:
	  PFP_KX7();
	  ~PFP_KX7();
	  int getWords(uint64_t *buffer, int count);
	  void stopWorker();
	  void startWorker();
	bool cardOK();
	int sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength);
	int recvReply(char *buffer, int bufferSize);
	int setAcquistionOnOff(bool enable);
	uint64_t getPortUp();
	uint64_t getPortCounts(int channel, int whichCount);
	virtual int setSorter(unsigned mode);
	virtual int setCoincidenceTrigger(CoincidenceTriggerConfig *config);
	virtual int setGateEnable(unsigned mode);

	  static const int ETIMEOUT = -1;
	  static const int ENOWORDS = -2;
	  static const int ENOCARD = -10000;

private:
	static const int BaseAddrReg		= 0x00280000;
	static const int DMACptSizeReg		= 256;
	static const int ConfigReg		= 288;
	static const int txWrPointerReg		= 320;
	static const int DMAConfigReg		= 352;
	static const int txRdPointerReg		= 384;
	static const int rxWrPointerReg		= 448;
	static const int rxRdPointerReg		= 512;
	static const int ThresholdReg		= 544;
	static const int acqStatusPointerReg	= 576;
	static const int CoincWindowReg		= 608;
	static const int CoincMasksReg		= 609;
	static const int statusReg		= 640;
	static const int GateEnableReg		= 577;

	WDC_DEVICE_HANDLE Card;
	static const unsigned NB = 2;
	WD_DMA *DMA_Point[NB];
	PVOID DMA_Buffer[NB];
	unsigned dmaBufferRdPtr;
	unsigned dmaBufferWrPtr;
	bool dmaBufferQueueIsEmpty();
	bool dmaBufferQueueIsFull();
	int wordBufferUsed[NB];
	int wordBufferStatus[NB];
	uint64_t *wordBuffer[NB];
	
	int getWords_(uint64_t *buffer, int count);
	int WriteAndCheck(int reg, uint32_t *data, int count);
	int ReadAndCheck(int reg, uint32_t *data, int count);

	uint32_t txWrPointer;
	uint32_t rxRdPointer;


	pthread_t worker;
	pthread_mutex_t lock;
	pthread_cond_t condCleanBuffer;
	pthread_cond_t condDirtyBuffer;

	volatile bool die;
	volatile bool hasWorker;
	static void *runWorker(void *arg);

	pthread_mutex_t hwLock;
	uint32_t lastCommandIdleCount;
	void setLastCommandTimeIdleCount();
	uint32_t getLastCommandTimeIdleCount();

};
}
#endif
