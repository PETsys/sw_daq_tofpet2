#ifndef __PFP_KX7_HPP__DEFINED__
#define __PFP_KX7_HPP__DEFINED__

#include <stdint.h>
#include <queue>
#include <pthread.h>
#include <boost/crc.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"
#include "DAQFrameServer.hpp"


namespace PETSYS {
class PFP_KX7 : public AbstractDAQCard {
public:
	  PFP_KX7();
	  ~PFP_KX7();
	  int getWords(uint64_t *buffer, int count);
	bool cardOK();
	void clearReplyQueue();
	int sendCommand(uint64_t *packetBuffer, int packetBufferSize);
	int recvReply(uint64_t *packetBuffer, int packetBufferSize);
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
	static const int base_addr0		= 0x00280000;
	static const int DMACptSizeReg		= 256;	//BRAM addr 0x100
	static const int ConfigReg		= 288;	//BRAM addr 0x120
	static const int txWrPointerReg		= 320;	//BRAM addr 0x140
	static const int DMAConfigReg		= 352;	//BRAM addr 0x160
	static const int txRdPointerReg		= 384;	//BRAM addr 0x180
	static const int rxWrPointerReg		= 448;	//BRAM addr 0x1C0
	static const int rxRdPointerReg		= 512;	//BRAM addr 0x200
	static const int ThresholdReg		= 544;	//BRAM addr 0x220
	static const int acqStatusPointerReg	= 576;	//BRAM addr 0x240
	static const int CoincWindowReg		= 608;	//BRAM addr 0x260
	static const int CoincMasksReg		= 609;	//BRAM addr 0x261
	static const int statusReg		= 640;	//BRAM addr 0x280
	static const int GateEnableReg		= 577;	//BRAM addr 0x241

	static const int base_addr1		= 0x100000;
	static const int ExtClk0		= 0x20000;
	static const int ExtClk1		= 0x20004;
	static const int ExtClk2		= 0x20008;
	static const int ExtClk3		= 0x2000C;
	static const int Osc0			= 0x200;
	static const int Osc1			= 0x204;
	static const int Osc2			= 0x208;

	
	int fd;
	int dma_fd;
	void *map_base;
	
	struct dma_buffer_t {
		uint64_t *data;
		ssize_t filled;
		ssize_t consumed;
	};
	
	dma_buffer_t *bufferSet;
	unsigned bufferSetWrPtr;
	unsigned bufferSetRdPtr;
	pthread_mutex_t bufferSetMutex;
	pthread_cond_t bufferSetCondFilled;
	pthread_cond_t bufferSetCondConsumed;

	pthread_t bufferSetThread;
	bool bufferSetThreadValid;
	bool bufferSetThreadStop;
	static void * bufferSetThreadRoutine(void * arg);

	dma_buffer_t *currentBuffer;

	int WriteWithoutCheck(int reg, uint32_t *data, int count);
	int WriteAndCheck(int reg, uint32_t *data, int count);
	int ReadAndCheck(int reg, uint32_t *data, int count);
	int ReadWithoutCheck(int reg, uint32_t *data, int count);

	uint32_t txWrPointer;
	uint32_t rxRdPointer;

};
}
#endif
