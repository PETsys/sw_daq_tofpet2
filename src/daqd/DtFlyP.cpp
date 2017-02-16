#include "DtFlyP.hpp"
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

// typedef unsigned long DWORD;
// typedef unsigned long long UINT64;
// 
// extern "C" {
// #include "dtfly_p_etpu_defs.h"
// #include "dtfly_p_etpu_api.h"
// }

using namespace DTFLY;
using namespace DAQd;

extern "C" {
#include "dtfly_p_etpu_api.h"
#include "dtfly_p_etpu_defs.h"
}

static const unsigned DMA_TRANS_BYTE_SIZE = 262144;
static const unsigned wordBufferSize = DMA_TRANS_BYTE_SIZE / sizeof(uint64_t);

static const int txWrPointerReg = 320;
static const int txRdPointerReg = 384;
static const int rxWrPointerReg = 448;
static const int rxRdPointerReg = 512;
static const int statusReg	= 640;

static void userIntHandler(SIntInfoData * IntInfoData)
{
}

DtFlyP::DtFlyP()
{
	int dtFlyPinitStatus = InitDevice(userIntHandler);
	assert(dtFlyPinitStatus == 0);
	
	//	logFile=fopen("./logfile.txt", "w");
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&condCleanBuffer, NULL);
	pthread_cond_init(&condDirtyBuffer, NULL);

	pthread_mutex_init(&hwLock, NULL);
	
	int status;
	status = ReadAndCheck(txWrPointerReg * 4 , &txWrPointer, 1);
	status = ReadAndCheck(rxRdPointerReg * 4 , &rxRdPointer, 1);
	
	dmaBufferWrPtr = 0;
	dmaBufferRdPtr = 0;

	die = true;
	hasWorker = false;

	setAcquistionOnOff(false);
	startWorker();
}

DtFlyP::~DtFlyP()
{
	stopWorker();

	pthread_mutex_destroy(&hwLock);
	pthread_cond_destroy(&condDirtyBuffer);
	pthread_cond_destroy(&condCleanBuffer);
	pthread_mutex_destroy(&lock);	

	ReleaseDevice();
}

// static void cacthINT1(int signal)
// {
// 	printf("1: SIG caught, %d\n", signal);
// }
// static void cacthINT2(int signal)
// {
// 	printf("2: SIG caught, %d\n", signal);
// }

const uint64_t idleWord = 0xFFFFFFFFFFFFFFFFULL;


bool DtFlyP::dmaBufferQueueIsEmpty()
{
	return dmaBufferWrPtr == dmaBufferRdPtr;
}

bool DtFlyP::dmaBufferQueueIsFull()
{
	return (dmaBufferWrPtr != dmaBufferRdPtr) && ((dmaBufferWrPtr%NB) == (dmaBufferRdPtr%NB));
}

void *DtFlyP::runWorker(void *arg)
{
	const uint64_t idleWord = 0xFFFFFFFFFFFFFFFFULL;

	printf("DtFlyP::runWorker launching...\n");
	DtFlyP *p = (DtFlyP *)arg;

	for(int i = 0; i < NB; i++ ) {
		p->dmaBuffer[i].ByteCount = DMA_TRANS_BYTE_SIZE;
		int r1 = AllocateBuffer(i, &p->dmaBuffer[i]);
		assert(r1 == 0);
	}

	while(!p->die) {
		
		pthread_mutex_lock(&p->lock);	
		while(!p->die && p->dmaBufferQueueIsFull()) {
			pthread_cond_wait(&p->condCleanBuffer, &p->lock);
		}
		pthread_mutex_unlock(&p->lock);
		int status = ReceiveDMAbyBufIndex(DMA1, p->dmaBufferWrPtr%NB, 2);
	       
		if(p->die) {
			status = ENOWORDS;
		}
		else if (status > 0) {
			p->wordBufferUsed[p->dmaBufferWrPtr%NB] = 0;
			p->wordBufferStatus[p->dmaBufferWrPtr%NB] = status / sizeof(uint64_t);
		}
		else if(status == 0) {
			p->wordBufferUsed[p->dmaBufferWrPtr%NB] = ENOWORDS - 1; // Set wordBufferUsed to some number smaller than status
			p->wordBufferStatus[p->dmaBufferWrPtr%NB] = ENOWORDS;
			printf("DtFlyP::worker wordBufferStatus set to %d\n", p->wordBufferStatus[p->dmaBufferWrPtr%NB]);
		}
		else {
			// Error
			p->wordBufferUsed[p->dmaBufferWrPtr%NB] = status - 1; // Set wordBufferUsed to some number smaller than status
			p->wordBufferStatus[p->dmaBufferWrPtr%NB] = status;
			printf("DtFlyP::worker wordBufferStatus set to %d\n", p->wordBufferStatus[p->dmaBufferWrPtr%NB]);
		}
		pthread_mutex_lock(&p->lock);
		p->dmaBufferWrPtr = (p->dmaBufferWrPtr+1) % (2*NB);
	       	pthread_cond_signal(&p->condDirtyBuffer);
		pthread_mutex_unlock(&p->lock);
	}
	
	for(int i = 0; i < NB; i++ ) {
		ReleaseBuffer(i);
	}
	
	printf("DtFlyP::runWorker exiting...\n");
	return NULL;
}

int DtFlyP::getWords(uint64_t *buffer, int count)
{
//	printf("DtFlyP::getWords(%p, %d)\n", buffer, count);
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

int DtFlyP::getWords_(uint64_t *buffer, int count)
{
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
		uint64_t *wordBuffer = (uint64_t *) dmaBuffer[dmaBufferRdPtr%NB].UserAddr;
		memcpy(buffer, wordBuffer + wordBufferUsed[dmaBufferRdPtr%NB], result * sizeof(uint64_t));
		wordBufferUsed[dmaBufferRdPtr%NB] += result;
	}

	return result;
	
}

void DtFlyP::startWorker()
{
	printf("DtFlyP::startWorker() called...\n");
// 	uint32_t regData = 0x10101010;	
// 	int status = ReadWriteUserRegs(WRITE, 0x002, &regData, 1);
// 	if(status != 0) {
// 		fprintf(stderr, "Error writing to register 0x002\n");
// 	}
	if(hasWorker)
		return;
       
	die = false;
	hasWorker = true;
	pthread_create(&worker, NULL, runWorker, (void*)this);

	printf("DtFlyP::startWorker() exiting...\n");	
}
void DtFlyP::stopWorker()
{
	printf("DtFlyP::stopWorker() called...\n");

	die = true;
	pthread_mutex_lock(&lock);
	pthread_cond_signal(&condCleanBuffer);
	pthread_cond_signal(&condDirtyBuffer);
	pthread_mutex_unlock(&lock);
	if(hasWorker) {
		hasWorker = false;

		pthread_join(worker, NULL);
	}

	pthread_mutex_lock(&lock);
	// if(currentBuffer != NULL) {
	// 	cleanBufferQueue.push(currentBuffer);
	// 	currentBuffer = NULL;
	// }
	
	// while(!dirtyBufferQueue.empty()) {
	// 	Buffer_t *buffer = dirtyBufferQueue.front();
	// 	dirtyBufferQueue.pop();
	// 	cleanBufferQueue.push(buffer);
	// }

	pthread_mutex_unlock(&lock);


	printf("DtFlyP::stopWorker() exiting...\n");
}

bool DtFlyP::cardOK()
{
	return true;
}


int DtFlyP::sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength)
{
// 	printf("8 command to send\n");
// 	for(int i = 0; i < commandLength; i++) {
// 		printf("%02x ", unsigned(buffer[i]) & 0xFF);
// 	}
// 	printf("\n");
	
	int status;

	uint64_t outBuffer[8];
	for(int i = 0; i < 8; i++) outBuffer[i] = 0x0FULL << (4*i);

	uint64_t header = 0;
	header = header + (8ULL << 36);
	header = header + (uint64_t(portID) << 59) + (uint64_t(slaveID) << 54);
	
	outBuffer[0] = header;
	outBuffer[1] = commandLength;
	memcpy((void*)(outBuffer+2), buffer, commandLength);
	
// 	printf("64 command to send: ");
// 	for(int i = 0; i < 8; i++) {
// 		printf("%016llx ", outBuffer[i]);
// 	}
// 	printf("\n");
	
	
	uint32_t txRdPointer;

	boost::posix_time::ptime startTime = boost::posix_time::microsec_clock::local_time();
	do {
		pthread_mutex_lock(&hwLock);
		// Read the RD pointer
		//printf("TX Read WR pointer: status is %NBd, result is %08X\n", status, txWrPointer);
		status = ReadAndCheck(txRdPointerReg * 4 , &txRdPointer, 1);
		//printf("TX Read RD pointer: status is %NBd, result is %08X\n", status, txRdPointer);
		pthread_mutex_unlock(&hwLock);

		boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
		if((now - startTime).total_milliseconds() > 10) {
			//printf("REG TX Timeout %04X %04X\n", txWrPointer, txRdPointer);
			return -1;
		}		

		// until there is space to write the command
	} while( ((txWrPointer & 16) != (txRdPointer & 16)) && ((txWrPointer & 15) == (txRdPointer & 15)) );
	
	pthread_mutex_lock(&hwLock);
	uint32_t wrSlot = txWrPointer & 15;
	
	status = WriteAndCheck(wrSlot * 16 * 4 , (uint32_t *)outBuffer, 16);
// 	printf("Wrote data to slot %d (status is %d)\n", wrSlot, status);
		
	txWrPointer += 1;
	txWrPointer = txWrPointer & 31;	// There are only 16 slots, but we use another bit for the empty/full condition
	txWrPointer |= 0xCAFE1500;
	
	status = WriteAndCheck(txWrPointerReg * 4 , &txWrPointer, 1);
// 	printf("Wrote WR pointer: status is %d, result is %d\n", status, txWrPointer);
	pthread_mutex_unlock(&hwLock);
	return 0;

}

int DtFlyP::recvReply(char *buffer, int bufferSize)
{

	int status;
	uint64_t outBuffer[8];
	
	uint32_t rxWrPointer;

	boost::posix_time::ptime startTime = boost::posix_time::microsec_clock::local_time();
	int nLoops = 0;
	do {
		pthread_mutex_lock(&hwLock);
		// Read the WR pointer
		status = ReadAndCheck(rxWrPointerReg * 4 , &rxWrPointer, 1);
		//printf("RX Read WR pointer: status is %d, result is %08X\n", status, rxWrPointer);
		//printf("RX Read RD pointer: status is %d, result is %08X\n", status, rxRdPointer);
		pthread_mutex_unlock(&hwLock);

		boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
		if((now - startTime).total_milliseconds() > 10) {
			//printf("REG RX Timeout %04X %04X\n", rxWrPointer, rxRdPointer);
			return -1;
		}		
		// until there a reply to read
	} while((rxWrPointer & 31) == (rxRdPointer & 31));
	pthread_mutex_lock(&hwLock);
	uint32_t rdSlot = rxRdPointer & 15;
	
	status = ReadAndCheck((768 + rdSlot * 16) * 4 , (uint32_t *)outBuffer, 16);
		
	rxRdPointer += 1;
	rxRdPointer = rxRdPointer & 31;	// There are only 16 slots, but we use another bit for the empty/full condition
	rxRdPointer |= 0xFACE9100;
	
	status = WriteAndCheck(rxRdPointerReg * 4 , &rxRdPointer, 1);
// 	printf("Wrote WR pointer: status is %d, result is %d\n", status, rxWrPointer);
	pthread_mutex_unlock(&hwLock);
	
	int replyLength = outBuffer[1];
	replyLength = replyLength < bufferSize ? replyLength : bufferSize;
	memcpy(buffer, outBuffer+2, replyLength);
	return replyLength;

}

int DtFlyP::setAcquistionOnOff(bool enable)
{
	const int acqStatusPointerReg = 576;
	uint32_t data[1];
	int status;
	
	data[0] = enable ? 0x0000FFFFU : 0xFFFF0000U;
	data[0] = enable ? 0x4444BBBBU : 0xFFFF0000U;
	pthread_mutex_lock(&hwLock);
	status = WriteAndCheck(acqStatusPointerReg * 4, data, 1);	
	pthread_mutex_unlock(&hwLock);
	return status;	
}

int DtFlyP::WriteAndCheck(int reg, uint32_t *data, int count) {
	uint32_t *readBackBuffer = new uint32_t[count];
	bool fail = true;
	int status = -1;
	int iter = 0;
	while(fail) {
		status = ReadWriteUserRegs(WRITE, reg, data, count);
		for(int i = 0; i < count; i++) readBackBuffer[i] = 0;
		ReadWriteUserRegs(READ, reg, readBackBuffer, count);
		fail = false;
		for(int i = 0; i < count; i++) fail |= (data[i] != readBackBuffer[i]);		
// 		if(fail) 
// 			printf("DtFlyP::WriteAndCheck(%3d, %8p, %NBd) readback failed, retrying (%d)\n",
// 				reg, data, count,
// 				iter);
		iter++;
	}
	delete [] readBackBuffer;
	return status;
};

int DtFlyP::ReadAndCheck(int reg, uint32_t *data, int count) {
	uint32_t *readBackBuffer = new uint32_t[count];
	bool fail = true;
	int status = -1;
	int iter = 0;
	while(fail) {
		status = ReadWriteUserRegs(READ, reg, data, count);
		ReadWriteUserRegs(READ, reg, readBackBuffer, count);
		fail = false;
		for(int i = 0; i < count; i++) fail |= (data[i] != readBackBuffer[i]);		
// 		if(fail) 
// 			printf("DtFlyP::ReadAndCheck(%3d, %8p, %NBd) readback failed, retrying (%d)\n",
// 				reg, data, count,
// 				iter);
		iter++;
	}
	delete [] readBackBuffer;
	return status;
};

uint64_t DtFlyP::getPortUp()
{
	uint64_t reply = 0;
	uint32_t channelUp = 0;
	ReadAndCheck(statusReg * 4, &channelUp, 1);
	reply = channelUp;
	return reply;
}

uint64_t DtFlyP::getPortCounts(int channel, int whichCount)
{
	uint64_t reply;
	ReadAndCheck((statusReg + 1 + 6*channel + 2*whichCount)* 4, (uint32_t *)&reply, 2);
	return reply;
}
