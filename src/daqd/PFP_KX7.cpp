#include "PFP_KX7.hpp"
#include <assert.h>

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>
#include "boost/date_time/posix_time/posix_time.hpp"

static unsigned long long TARGET_EXT_CLK_FREQUENCY = 160000000;

using namespace DAQd;

static const unsigned DMA_TRANS_BYTE_SIZE = 262144;  // max for USER_FIFO_THRESHOLD 262128
static const unsigned wordBufferSize = DMA_TRANS_BYTE_SIZE / 8;

static const int COMMAND_TO_DMA_PAUSE = 10;

//static int COM_count = 0;

static void *userIntHandler(WDC_DEVICE_HANDLE Device, PFP_INT_RESULT *Result)
{
	return NULL;
}

PFP_KX7::PFP_KX7()
{
	DWORD Status;
	char version[5];
	Status = PFP_OpenDriver(0);  // open driver
	assert(Status == WD_STATUS_SUCCESS);
	PFP_Get_Version(version);  // get driver version
	Status = PFP_OpenCard(&Card, NULL, NULL);  // open card
	assert(Status == WD_STATUS_SUCCESS);

	unsigned int ExtClkFreq;
	PFP_SM_Get_ECF0(Card, &ExtClkFreq);
	if (ExtClkFreq < 0.99995*TARGET_EXT_CLK_FREQUENCY || ExtClkFreq > 1.00005*TARGET_EXT_CLK_FREQUENCY) {  // check the ext oscilator frequency
		Status = PFP_SM_Set_Freq0(Card, TARGET_EXT_CLK_FREQUENCY/1000);  // if not correct, reprogram
		assert(Status == WD_STATUS_SUCCESS);
	}


	// setting up firmware configuration
	// Filler Activated (26); Disable Interrupt (24); Disable AXI Stream (21)
	// Use User FIFO Threshold (16); User FIFO Threshold in 64B word addressing (11 -> 0)
	uint32_t word32;
	word32 = 0x19000000;  // sorter disable, with filler
	Status = PFP_Write(Card, BaseAddrReg + ConfigReg * 4, 4, &word32, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
	assert(Status == WD_STATUS_SUCCESS);
	
	// setting up ToT Threshold (in clocks)
	word32 = 0x00000000;
	Status = PFP_Write(Card, BaseAddrReg + ThresholdReg * 4, 4, &word32, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
	assert(Status == WD_STATUS_SUCCESS);

	// setting up Coincidence Windows
	word32 = 0x0F000000;  // sent everthing
	Status = PFP_Write(Card, BaseAddrReg + CoincWindowReg * 4, 4, &word32, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
	assert(Status == WD_STATUS_SUCCESS);	

	word32 = 0xFFFFFFFF;
	for (int i = 0; i < 6; i++) {  // 12 channels => 6 addresses
		Status = PFP_Write(Card, BaseAddrReg + (CoincMasksReg + i) * 4, 4, &word32, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
		assert(Status == WD_STATUS_SUCCESS);
	}	

	// setting up DMA configuration
	// DMA Start toggle (26); DMA Size in 16B word addressing (23 -> 0)
	word32 = 0x08000000 + DMA_TRANS_BYTE_SIZE / 16;
	Status = PFP_Write(Card, BaseAddrReg + DMAConfigReg * 4, 4, &word32, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
	assert(Status == WD_STATUS_SUCCESS);

	// read Status
	// Channel Up information
	Status = PFP_Read(Card, BaseAddrReg + statusReg * 4, 4, &word32, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
	assert(Status == WD_STATUS_SUCCESS);
	printf("Channel Up: %03X\n", word32);

	// setting up DMA buffer
	for(int n = 0; n < NB; n++) {
		DMA_Buffer[n] = NULL;
		DMA_Point[n] = NULL;
		Status = PFP_SetupDMA(Card, &DMA_Buffer[n], DMA_FROM_DEVICE, DMA_TRANS_BYTE_SIZE, &DMA_Point[n]);
		assert(Status == WD_STATUS_SUCCESS);
	}
	
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&condCleanBuffer, NULL);
	pthread_cond_init(&condDirtyBuffer, NULL);

	pthread_mutex_init(&hwLock, NULL);
	lastCommandIdleCount = 0;
	
	ReadAndCheck(txWrPointerReg * 4 , &txWrPointer, 1);
	ReadAndCheck(rxRdPointerReg * 4 , &rxRdPointer, 1);

	dmaBufferRdPtr = 0;
	dmaBufferWrPtr = 0;
	
	die = true;
	hasWorker = false;

	setAcquistionOnOff(false);
	startWorker();
}

PFP_KX7::~PFP_KX7()
{
	printf("PFP_KX7::~PFP_KX7\n");
	stopWorker();

	pthread_mutex_destroy(&hwLock);
	pthread_cond_destroy(&condDirtyBuffer);
	pthread_cond_destroy(&condCleanBuffer);
	pthread_mutex_destroy(&lock);	

	// releasing DMA buffers
	for(int n = 0; n < NB; n++) {
		PFP_UnsetupDMA(DMA_Point[n]);
	}

	PFP_CloseCard(Card);  // closing card
	PFP_CloseDriver();  // closing driver
}

bool PFP_KX7::dmaBufferQueueIsEmpty()
{
	return dmaBufferWrPtr == dmaBufferRdPtr;
}

bool PFP_KX7::dmaBufferQueueIsFull()
{
	return (dmaBufferWrPtr != dmaBufferRdPtr) && ((dmaBufferWrPtr%NB) == (dmaBufferRdPtr%NB));
}

void PFP_KX7::setLastCommandTimeIdleCount()
{
	__atomic_store_n(&lastCommandIdleCount, COMMAND_TO_DMA_PAUSE, __ATOMIC_RELEASE);
}

uint32_t PFP_KX7::getLastCommandTimeIdleCount()
{
	return __atomic_exchange_n(&lastCommandIdleCount, 0, __ATOMIC_ACQ_REL);	
}


const uint64_t idleWord = 0xFFFFFFFFFFFFFFFFULL;

void *PFP_KX7::runWorker(void *arg)
{
	const uint64_t idleWord = 0xFFFFFFFFFFFFFFFFULL;

	printf("PFP_KX7::runWorker launching...\n");
	PFP_KX7 *p = (PFP_KX7 *)arg;

	DWORD Status, DMAStatus;
	unsigned int ExtraDMAByteCount = 0;
	uint32_t word32_108, word32_256;
	uint32_t word32_dmat = 0x8000000 + DMA_TRANS_BYTE_SIZE / 16;
	int DMA_count = 0;

	pthread_mutex_lock(&p->hwLock);
	// setting up DMA configuration
	Status = PFP_Write(p->Card, BaseAddrReg + DMAConfigReg * 4, 4, &word32_dmat, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
	assert(Status == WD_STATUS_SUCCESS);
	pthread_mutex_unlock(&p->hwLock);

	while(!p->die) {
		pthread_mutex_lock(&p->lock);	
		while(!p->die && p->dmaBufferQueueIsFull()) {
			pthread_cond_wait(&p->condCleanBuffer, &p->lock);
		}
		pthread_mutex_unlock(&p->lock);
		
		int n = p->dmaBufferWrPtr%NB;
		int lWordBufferStatus = ENOWORDS;
		int lWordBufferUsed = ENOWORDS-1;
		char *lWordBuffer = (char *)p->DMA_Buffer[n];
		assert(lWordBuffer != NULL);

		int sleepTime;
		while((sleepTime = p->getLastCommandTimeIdleCount()) > 0) {
			usleep(sleepTime * 1000);
		}
		pthread_mutex_lock(&p->hwLock);
		// DMA Start toggle (26); DMA Size in 16B word addressing (23 -> 0)
		word32_dmat = word32_dmat ^ 0x08000000;
		Status = PFP_Write(p->Card, BaseAddrReg + DMAConfigReg * 4, 4, &word32_dmat, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
		assert(Status == WD_STATUS_SUCCESS);
		
		DMAStatus = PFP_DoDMA(p->Card, p->DMA_Point[n], 0, DMA_TRANS_BYTE_SIZE, DMA_AXI_STREAM, 1);
		assert(DMAStatus != TW_DMA_ERROR);
		if (DMAStatus == TW_DMA_TIMEOUT || DMAStatus == TW_DMA_EOT)
			ExtraDMAByteCount = 4;
		else
			ExtraDMAByteCount = 0;

		// read DMA transfered size
		// 0x108 on Timeout and End of Transfer will have an extra 4B added
		Status = PFP_Read(p->Card, 0x108, 4, &word32_108, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
		assert(Status == WD_STATUS_SUCCESS);
		// DMACptSizeReg on Timeout and End of Transfer will have an extra 16B added
		Status = PFP_Read(p->Card, BaseAddrReg + DMACptSizeReg * 4, 4, &word32_256, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
		assert(Status == WD_STATUS_SUCCESS);

		lWordBufferUsed = word32_108 / 4;  // converting to 16 byte addressing
		lWordBufferUsed = lWordBufferUsed * 16;  // converting to byte addressing
		lWordBufferStatus = word32_256 / 8;
		if (lWordBufferUsed < DMA_TRANS_BYTE_SIZE) {
			if (word32_256 >= ExtraDMAByteCount * 4)  // prevent negative value to be written
				lWordBufferStatus = (word32_256 - ExtraDMAByteCount * 4) / 8;
		}

		pthread_mutex_unlock(&p->hwLock);
		
		if(p->die || lWordBufferUsed == 0) {
		  	lWordBufferUsed = ENOWORDS - 1; // Set wordBufferUsed to some number smaller than status
			lWordBufferStatus = ENOWORDS;
			printf("PFP_KX7::worker wordBufferStatus set to %d\n", lWordBufferStatus);
		}
		else {
			lWordBufferUsed = 0;
		}
		p->wordBufferStatus[n] = lWordBufferStatus;
		p->wordBufferUsed[n] = lWordBufferUsed;
		p->wordBuffer[n] = (uint64_t *)lWordBuffer;
		
		pthread_mutex_lock(&p->lock);
		p->dmaBufferWrPtr = (p->dmaBufferWrPtr+1) % (2*NB);
		pthread_cond_signal(&p->condDirtyBuffer);
		pthread_mutex_unlock(&p->lock);
		
		DMA_count++;
	}
	pthread_mutex_lock(&p->lock);
	pthread_cond_signal(&p->condDirtyBuffer);
	pthread_mutex_unlock(&p->lock);
	
	printf("PFP_KX7::runWorker exiting...\n");
	return NULL;
}

int PFP_KX7::getWords(uint64_t *buffer, int count)
{
	int r = 0;
	while(r < count) {		
		int status = getWords_(buffer + r, count - r);
		if(status < 0) 
			return status;
	
		else 
			r += status;
	}

	return r;
}

int PFP_KX7::getWords_(uint64_t *buffer, int count)
{
	if(die) return -1;
	// If the DMA buffer queue is empty for reading, wait until we have something.
	// Otherwise, proceed without even touching the lock.
	if(dmaBufferQueueIsEmpty()) {
		pthread_mutex_lock(&lock);
		while(!die && dmaBufferQueueIsEmpty()) {
			pthread_cond_wait(&condDirtyBuffer, &lock);
		}
		pthread_mutex_unlock(&lock);
		if(die) return -1;
	}

	// If this buffer has been fully consumed, increase read pointer and return 0.
	// getWords() will retry again.
	if(wordBufferUsed[dmaBufferRdPtr%NB] >= wordBufferStatus[dmaBufferRdPtr%NB]) {
		pthread_mutex_lock(&lock);
		dmaBufferRdPtr = (dmaBufferRdPtr + 1) % (2*NB);
		pthread_cond_signal(&condCleanBuffer);
		pthread_mutex_unlock(&lock);
		return 0;
	}
	
	int result = -1;
	if(wordBufferStatus[dmaBufferRdPtr%NB] < 0) {
		// This buffer had an error status
		printf("Buffer status was set to %d\n", wordBufferStatus[dmaBufferRdPtr%NB]);
		wordBufferUsed[dmaBufferRdPtr%NB] = wordBufferStatus[dmaBufferRdPtr%NB]; // Set wordBufferUsed to some number equal or greater than status
		result = wordBufferStatus[dmaBufferRdPtr%NB];
	}	
	else {	
		// This buffer has normal data
		int wordsAvailable = wordBufferStatus[dmaBufferRdPtr%NB] - wordBufferUsed[dmaBufferRdPtr%NB];
		result = count < wordsAvailable ? count : wordsAvailable;
		if(result < 0) result = 0;
		memcpy(buffer, wordBuffer[dmaBufferRdPtr%NB] + wordBufferUsed[dmaBufferRdPtr%NB], result * sizeof(uint64_t));
		wordBufferUsed[dmaBufferRdPtr%NB] += result;
	}

	return result;
}

void PFP_KX7::startWorker()
{
	printf("PFP_KX7::startWorker() called...\n");
	if(hasWorker)
		return;
       
	die = false;
	hasWorker = true;
	pthread_create(&worker, NULL, runWorker, (void*)this);

	printf("PFP_KX7::startWorker() exiting...\n");	
}
void PFP_KX7::stopWorker()
{
	printf("PFP_KX7::stopWorker() called...\n");
	setAcquistionOnOff(false);
	die = true;
	pthread_mutex_lock(&lock);
	pthread_cond_signal(&condCleanBuffer);
	pthread_cond_signal(&condDirtyBuffer);
	pthread_mutex_unlock(&lock);
	if(hasWorker) {
		hasWorker = false;

		pthread_join(worker, NULL);
	}
	printf("PFP_KX7::stopWorker() exiting...\n");
}

bool PFP_KX7::cardOK()
{
	return true;
}


int PFP_KX7::sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength)
{
	setLastCommandTimeIdleCount();
	int status;

	uint64_t outBuffer[8];
	for(int i = 0; i < 8; i++) outBuffer[i] = 0x0FULL << (4*i);

	uint64_t header = 0;
	header = header + (8ULL << 36);
	header = header + (uint64_t(portID) << 59) + (uint64_t(slaveID) << 54);

	outBuffer[0] = header;
	outBuffer[1] = commandLength;
	memcpy((void*)(outBuffer+2), buffer, commandLength);
	int nWords = 2 + ceil(float(commandLength)/sizeof(uint64_t));
	
	uint32_t txRdPointer;

	pthread_mutex_lock(&hwLock);
	boost::posix_time::ptime startTime = boost::posix_time::microsec_clock::local_time();
	do {
		// Read the RD pointer
		status = ReadAndCheck(txRdPointerReg * 4 , &txRdPointer, 1);
		setLastCommandTimeIdleCount();

		boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
		if((now - startTime).total_milliseconds() > 10) {
			pthread_mutex_unlock(&hwLock);
			return -1;
		}		


		// until there is space to write the command
	} while( ((txWrPointer & 16) != (txRdPointer & 16)) && ((txWrPointer & 15) == (txRdPointer & 15)) );
	
	uint32_t wrSlot = txWrPointer & 15;
	status = WriteAndCheck(wrSlot * 16 * 4 , (uint32_t *)outBuffer, (nWords * 2));
		
	txWrPointer += 1;
	txWrPointer = txWrPointer & 31;	// There are only 16 slots, but we use another bit for the empty/full condition
	txWrPointer |= 0xCAFE1500;
	
	status = WriteAndCheck(txWrPointerReg * 4 , &txWrPointer, 1);
	setLastCommandTimeIdleCount();
	pthread_mutex_unlock(&hwLock);
	return 0;

}

int PFP_KX7::recvReply(char *buffer, int bufferSize)
{
	setLastCommandTimeIdleCount();
	int status;
	uint64_t outBuffer[8];
	
	uint32_t rxWrPointer;

	pthread_mutex_lock(&hwLock);
	boost::posix_time::ptime startTime = boost::posix_time::microsec_clock::local_time();
	int nLoops = 0;
	do {
		// Read the WR pointer
		status = ReadAndCheck(rxWrPointerReg * 4 , &rxWrPointer, 1);
		setLastCommandTimeIdleCount();

		boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
		if((now - startTime).total_milliseconds() > 10) {
			pthread_mutex_unlock(&hwLock);
			return -1;
		}		

		// until there a reply to read
	} while((rxWrPointer & 31) == (rxRdPointer & 31));
	uint32_t rdSlot = rxRdPointer & 15;
	
	status = ReadAndCheck((768 + rdSlot * 16) * 4 , (uint32_t *)outBuffer, 16);
	rxRdPointer += 1;
	rxRdPointer = rxRdPointer & 31;	// There are only 16 slots, but we use another bit for the empty/full condition
	rxRdPointer |= 0xFACE9100;
	
	status = WriteAndCheck(rxRdPointerReg * 4 , &rxRdPointer, 1);
	setLastCommandTimeIdleCount();
	pthread_mutex_unlock(&hwLock);
	
	int replyLength = outBuffer[1];
	replyLength = replyLength < bufferSize ? replyLength : bufferSize;
	memcpy(buffer, outBuffer+2, replyLength);
	return replyLength;
}

int PFP_KX7::setAcquistionOnOff(bool enable)
{
	setLastCommandTimeIdleCount();
	uint32_t data[1];
	int status;
	
	data[0] = enable ? 0x000FFFFU : 0xFFFF0000U;
	pthread_mutex_lock(&hwLock);
	status = WriteAndCheck(acqStatusPointerReg * 4, data, 1);	
	setLastCommandTimeIdleCount();
	pthread_mutex_unlock(&hwLock);
	return status;	
}

int PFP_KX7::WriteAndCheck(int reg, uint32_t *data, int count) {
	assert(reg >= 0);
	assert((reg/4 + count) >= 0);
	assert((reg/4 + count) <= 1024);

	reg = BaseAddrReg + reg;
	
	uint32_t *readBackBuffer = new uint32_t[count];
	bool fail = true;
	DWORD Status;
	int iter = 0;
	while(fail) {
		//Status = PFP_Write(Card, reg, count * 4, data, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
		for(int i = 0; i < count; i++) PFP_Write(Card, reg + 4*i, 4, data+i, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
		for(int i = 0; i < count; i++) readBackBuffer[i] = 0;
		PFP_Read(Card, reg, count * 4, readBackBuffer, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
		fail = false;
		for(int i = 0; i < count; i++) fail |= (data[i] != readBackBuffer[i]);		
		iter++;
	}
	delete [] readBackBuffer;
	return (int) Status;
};

int PFP_KX7::ReadAndCheck(int reg, uint32_t *data, int count) {
	assert(reg >= 0);
	assert((reg/4 + count) >= 0);
	assert((reg/4 + count) <= 1024);

	reg = BaseAddrReg + reg;

	uint32_t *readBackBuffer = new uint32_t[count];
	bool fail = true;
	DWORD Status;
	int iter = 0;
	while(fail) {
		Status = PFP_Read(Card, reg, count * 4, data, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
		for(int i = 0; i < count; i++) readBackBuffer[i] = 0;
		PFP_Read(Card, reg, count * 4, readBackBuffer, WDC_MODE_32, WDC_ADDR_RW_DEFAULT);
		fail = false;
		for(int i = 0; i < count; i++) fail |= (data[i] != readBackBuffer[i]);		
		iter++;
	}
	delete [] readBackBuffer;
	return (int) Status;
};

uint64_t PFP_KX7::getPortUp()
{
	setLastCommandTimeIdleCount();
	pthread_mutex_lock(&hwLock);
	uint64_t reply = 0;
	uint32_t channelUp = 0;
	ReadAndCheck(statusReg * 4, &channelUp, 1);
	reply = channelUp;	
	setLastCommandTimeIdleCount();
	pthread_mutex_unlock(&hwLock);
	return reply;
}

uint64_t PFP_KX7::getPortCounts(int channel, int whichCount)
{
	setLastCommandTimeIdleCount();
	pthread_mutex_lock(&hwLock);
	uint64_t reply;
	ReadAndCheck((statusReg + 1 + 6*channel + 2*whichCount)* 4, (uint32_t *)&reply, 2);
	setLastCommandTimeIdleCount();
	pthread_mutex_unlock(&hwLock);
	return reply;
}

int PFP_KX7::setSorter(unsigned mode)
{
	setLastCommandTimeIdleCount();
	pthread_mutex_lock(&hwLock);

	// This word is used for multiple purposes
	// Thus, we read it and modify just the sort enable bit
	uint32_t currentSetting;
	ReadAndCheck(ConfigReg * 4, &currentSetting, 1);
	// Clear the sorter enable bit
	currentSetting = currentSetting & 0xEFFFFFFF;
	// Set the sorter disable bit based on the desired mode
	currentSetting = currentSetting | (!mode ? 0x10000000 : 0x00000000);
	WriteAndCheck(ConfigReg * 4, &currentSetting, 1);

	setLastCommandTimeIdleCount();
	pthread_mutex_unlock(&hwLock);
	return 0;
}

int PFP_KX7::setCoincidenceTrigger(CoincidenceTriggerConfig *config)
{
	setLastCommandTimeIdleCount();
	pthread_mutex_lock(&hwLock);

	if(config == NULL || config->enable == 0) {
		// Write a allow all configuration
		uint32_t word32 = 0x0F000000;
		WriteAndCheck(CoincWindowReg * 4, &word32, 1);

		word32 = 0x00000000;
		WriteAndCheck(ThresholdReg * 4, &word32, 1);
		
		word32 = 0x00000000;
		for(int n = 0; n < 6; n++) {
			WriteAndCheck((CoincMasksReg + n) * 4, &word32, 1);
		}
	}
	else {
		if(config->preWindow > 0xF) config->preWindow = 0xF;
		if(config->postWindow > 0x3F) config->postWindow = 0x3F;
		if(config->cWindow > 0x3) config->cWindow = 0x3;

		uint32_t word32 = 0x00000000;
		word32 |= config->preWindow;
		word32 |= config->postWindow << 8;
		word32 |= config->cWindow << 20;
		// WARNING: software support for single window sampling not implemented yet
		WriteAndCheck(CoincWindowReg * 4, &word32, 1);

		word32 = config->minToT;
		WriteAndCheck(ThresholdReg * 4, &word32, 1);

		uint32_t mask[6];
		for(int n = 0; n < 6; n++) {
			word32 = 0x00000000;
			word32 |= config->mask[2*n + 0] & 0xFFF;
			word32 |= (config->mask[2*n + 1] & 0xFFF) << 16;
			mask[n] = word32;
		}
		WriteAndCheck(CoincMasksReg * 4, mask, 6);
	}
	setLastCommandTimeIdleCount();
	pthread_mutex_unlock(&hwLock);
	return 0;
}

int PFP_KX7::setGateEnable(unsigned mode)
{
	setLastCommandTimeIdleCount();
	uint32_t data[1];
	int status;
	
	data[0] = (mode != 0) ? 0x000FFFFU : 0xFFFF0000U;
	pthread_mutex_lock(&hwLock);
	status = WriteAndCheck(GateEnableReg * 4, data, 1);	
	setLastCommandTimeIdleCount();
	pthread_mutex_unlock(&hwLock);
	return status;	
}