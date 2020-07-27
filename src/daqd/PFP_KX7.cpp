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
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>
#include "boost/date_time/posix_time/posix_time.hpp"

#include "../kernel/psdaq.h"

static unsigned long long TARGET_EXT_CLK_FREQUENCY = 200000000;
#define MAP_SIZE (4*1024*1024UL)
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ltohl(x)       (x)
#define ltohs(x)       (x)
#define htoll(x)       (x)
#define htols(x)       (x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define ltohl(x)     __bswap_32(x)
#define ltohs(x)     __bswap_16(x)
#define htoll(x)     __bswap_32(x)
#define htols(x)     __bswap_16(x)
#endif


using namespace PETSYS;

static const unsigned DMA_TRANS_BYTE_SIZE = 262144;  // max for USER_FIFO_THRESHOLD 262128
static const unsigned BUFFER_SIZE = DMA_TRANS_BYTE_SIZE / 8;
static const unsigned N_BUFFER = 8;

PFP_KX7::PFP_KX7()
{
	if ((fd = open("/dev/psdaq0", O_RDWR | O_SYNC)) == -1)
		assert(false);
	
	uint32_t ExtClkFreq;
	ReadWithoutCheck(base_addr1 + ExtClk0, &ExtClkFreq, 1);
	
	if (ExtClkFreq < 0.99995*TARGET_EXT_CLK_FREQUENCY || ExtClkFreq > 1.00005*TARGET_EXT_CLK_FREQUENCY) {  // check the ext oscilator frequency
		uint32_t data;
		data = 1;
		WriteWithoutCheck(base_addr1 + 0x18, &data, 1);
		
		do {
			ReadWithoutCheck(base_addr1 + 0x10, &data, 1);
			usleep(1000);
		} while(data != 0);
		
		data = TARGET_EXT_CLK_FREQUENCY/10000;
		WriteWithoutCheck(base_addr1 + Osc0, &data, 1);
		
		do {
			ReadWithoutCheck(base_addr1 + 0x10, &data, 1);
			usleep(1000);
		} while (data != 0);
	}

	// setting up firmware configuration
	// Filler Activated (26); Disable Interrupt (24); Disable AXI Stream (21)
	// Use User FIFO Threshold (16); User FIFO Threshold in 64B word addressing (11 -> 0)
	uint32_t word32;
	word32 = 0x19000000;  // sorter disable, with filler
	WriteWithoutCheck(base_addr0 + ConfigReg * 4, &word32, 1);

	// setting up ToT Threshold (in clocks)
	word32 = 0x00000000;
	WriteWithoutCheck(base_addr0 + ThresholdReg * 4, &word32, 1);

	// setting up Coincidence Windows
	word32 = 0x0F000000;  // sent everthing
	WriteWithoutCheck(base_addr0 + CoincWindowReg * 4, &word32, 1);

	word32 = 0xFFFFFFFF;
	for (int i = 0; i < 6; i++) {  // 12 channels => 6 addresses
		WriteWithoutCheck(base_addr0 + (CoincMasksReg + i) * 4, &word32, 1);
	}	

	// setting up DMA configuration
	// DMA Start toggle (26); DMA Size in 16B word addressing (23 -> 0)
	word32 = 0x08000000 + DMA_TRANS_BYTE_SIZE / 16;
	WriteWithoutCheck(base_addr0 + DMAConfigReg * 4, &word32, 1);

	// read Status
	// Channel Up information
	ReadWithoutCheck(base_addr0 + statusReg * 4, &word32, 1);
	printf("Channel Up: %03X\n", word32);


 	ReadAndCheck(base_addr0 + txRdPointerReg * 4 , &txWrPointer, 1);
 	WriteAndCheck(base_addr0 + txWrPointerReg * 4, &txWrPointer, 1);
 	ReadAndCheck(base_addr0 + rxWrPointerReg * 4 , &rxRdPointer, 1);
 	WriteAndCheck(base_addr0 + rxRdPointerReg * 4 , &rxRdPointer, 1);

	bufferSet = new dma_buffer_t[N_BUFFER];
	for(unsigned k = 0; k < N_BUFFER; k++) {
		bufferSet[k].data = new uint64_t[BUFFER_SIZE];
		bufferSet[k].consumed = bufferSet[k].filled = 0;
	}

	pthread_mutex_init(&bufferSetMutex, NULL);
	pthread_cond_init(&bufferSetCondFilled, NULL);
	pthread_cond_init(&bufferSetCondConsumed, NULL);
	bufferSetThreadValid = false;
	setAcquistionOnOff(false);
}

PFP_KX7::~PFP_KX7()
{
	setAcquistionOnOff(false);
	for(unsigned k = 0; k < N_BUFFER; k++) {
		delete [] bufferSet[k].data ;
	}
	delete [] bufferSet;
	close(fd);
}


int PFP_KX7::getWords(uint64_t *buffer, int count)
{
	if(!bufferSetThreadValid) {
		// TODO:
		// DAQFrameServer tries to read data even before the acquisiton has been started
		// In such case, sleep a bit and then return 0 words
		// But this should be fixed in DAQFrameServer later
		usleep(100000);
		return 0;
	}
	
	int r = 0;
	while(r < count) {
		if(currentBuffer == NULL) {
			// We have no buffer to copy data from yet
			
			pthread_mutex_lock(&bufferSetMutex);
			bool empty = (bufferSetWrPtr == bufferSetRdPtr);
			if(empty) {
				// We're empty, wait for signal and try again
				pthread_cond_wait(&bufferSetCondFilled, &bufferSetMutex);
				pthread_mutex_unlock(&bufferSetMutex);
				continue;
			}
			
			unsigned index = bufferSetRdPtr % N_BUFFER;
			pthread_mutex_unlock(&bufferSetMutex);
			currentBuffer = bufferSet + index;
		}
		else if(currentBuffer->consumed == currentBuffer->filled) {
			// We have a buffer but it's exausted
			
			currentBuffer = NULL;
			
			pthread_mutex_lock(&bufferSetMutex);
			bufferSetRdPtr = (bufferSetRdPtr + 1) % (2*N_BUFFER);
			pthread_mutex_unlock(&bufferSetMutex);
			pthread_cond_signal(&bufferSetCondConsumed);
			continue;
			
		}
		
		ssize_t missing = count - r;
		ssize_t available = currentBuffer->filled - currentBuffer->consumed;
		int c2 = (missing  < available) ? missing : available;
		memcpy((buffer + r), currentBuffer->data + currentBuffer->consumed, c2 * sizeof(uint64_t));
		currentBuffer->consumed += c2;
		r += c2;
	}
	return r;
}

bool PFP_KX7::cardOK()
{
	return true;
}

void PFP_KX7::clearReplyQueue()
{
	int count = 0;

	while(true) {

		uint32_t rxWrPointer;
		ReadAndCheck(base_addr0 + rxWrPointerReg * 4 , &rxWrPointer, 1);

		if((rxWrPointer & 31) == (rxRdPointer & 31)) break;

		if(count % 100 == 99) fprintf(stderr, "WARNING: Resetting RX pointers (try = %d)\n", count);

		rxRdPointer = rxWrPointer;
		rxRdPointer &= 31;
		rxRdPointer |= 0xFACE9100;
		WriteAndCheck(base_addr0 + rxRdPointerReg * 4 , &rxRdPointer, 1);

		count += 1;
		usleep(100);
	}
}


int PFP_KX7::sendCommand(uint64_t *packetBuffer, int packetBufferSize)
{
	if(packetBufferSize > 64) {
		fprintf(stderr, "ERROR in PFP_KX7::sendCommand(...) packetBufferSize %d is too large.\n", packetBufferSize);
		return -1;
	}

	int status;
	uint32_t txRdPointer;

	boost::posix_time::ptime startTime = boost::posix_time::microsec_clock::local_time();
	do {
		// Read the RD pointer
		status = ReadAndCheck(base_addr0 + txRdPointerReg * 4 , &txRdPointer, 1);

		boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
		if((now - startTime).total_milliseconds() > 10) {
			return -1;
		}		


		// until there is space to write the command
	} while( ((txWrPointer & 16) != (txRdPointer & 16)) && ((txWrPointer & 15) == (txRdPointer & 15)) );
	
	uint32_t wrSlot = txWrPointer & 15;
	status = WriteAndCheck(base_addr0 + wrSlot * 16 * 4 , (uint32_t *)packetBuffer, (packetBufferSize * 2));
		
	txWrPointer += 1;
	txWrPointer = txWrPointer & 31;	// There are only 16 slots, but we use another bit for the empty/full condition
	txWrPointer |= 0xCAFE1500;
	
	status = WriteAndCheck(base_addr0 + txWrPointerReg * 4 , &txWrPointer, 1);
	return 0;

}

int PFP_KX7::recvReply(uint64_t *packetBuffer, int packetBufferSize)
{
	assert(packetBufferSize >= 8);
	int status;
	uint32_t rxWrPointer;

	boost::posix_time::ptime startTime = boost::posix_time::microsec_clock::local_time();
	int nLoops = 0;
	do {
		// Read the WR pointer
		status = ReadAndCheck(base_addr0 + rxWrPointerReg * 4 , &rxWrPointer, 1);

		boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
		if((now - startTime).total_milliseconds() > 10) {
			return -1;
		}		

		// until there a reply to read
	} while((rxWrPointer & 31) == (rxRdPointer & 31));
	uint32_t rdSlot = rxRdPointer & 15;
	
	status = ReadAndCheck(base_addr0 + (768 + rdSlot * 16) * 4 , (uint32_t *)packetBuffer, 16);
	rxRdPointer += 1;
	rxRdPointer = rxRdPointer & 31;	// There are only 16 slots, but we use another bit for the empty/full condition
	rxRdPointer |= 0xFACE9100;
	
	status = WriteAndCheck(base_addr0 + rxRdPointerReg * 4 , &rxRdPointer, 1);

	return 8;
}

void * PFP_KX7::bufferSetThreadRoutine(void * arg)
{
	fprintf(stderr,"worker started...\n");
	PFP_KX7 *p = (PFP_KX7 *)arg;
	
	while(true) {
		pthread_mutex_lock(&p->bufferSetMutex);
		if (p->bufferSetThreadStop) {
			// Time to quit
			pthread_mutex_unlock(&p->bufferSetMutex);
			fprintf(stderr,"worker terminated...\n");
			return NULL;
		}

		bool full = ((p->bufferSetWrPtr % N_BUFFER) == (p->bufferSetRdPtr % N_BUFFER)) && (p->bufferSetWrPtr != p->bufferSetRdPtr);
		if(full) {
			// We're full, wait for signal and then re-check
			pthread_cond_wait(&p->bufferSetCondConsumed, &p->bufferSetMutex);
			pthread_mutex_unlock(&p->bufferSetMutex);
			continue;
		}
		
		unsigned index = p->bufferSetWrPtr % N_BUFFER;
		dma_buffer_t *buffer = p->bufferSet + index;

		pthread_mutex_unlock(&p->bufferSetMutex);
		int r = read(p->fd, buffer->data, BUFFER_SIZE * sizeof(uint64_t));
		assert(r >= 0);
		buffer->filled = r / sizeof(uint64_t);
		buffer->consumed = 0;

		pthread_mutex_lock(&p->bufferSetMutex);
		p->bufferSetWrPtr = (p->bufferSetWrPtr + 1) % (2*N_BUFFER);
		pthread_mutex_unlock(&p->bufferSetMutex);
		pthread_cond_signal(&p->bufferSetCondFilled);
	}
	fprintf(stderr,"worker terminated...\n");
	return NULL;
}

int PFP_KX7::setAcquistionOnOff(bool enable)
{
	uint32_t data[1];
	int status;
	
	data[0] = enable ? 0x000FFFFU : 0xFFFF0000U;
	status = WriteAndCheck(base_addr0 + acqStatusPointerReg * 4, data, 1);
	
	
	if(enable) {
		data[0] =  0x000FFFFU;
		status = WriteAndCheck(base_addr0 + acqStatusPointerReg * 4, data, 1);
		
		if(!bufferSetThreadValid) {
			bufferSetWrPtr = 0;
			bufferSetRdPtr = 0;
			currentBuffer = NULL;
			for(unsigned k = 0; k < N_BUFFER; k++) {
				bufferSet[k].consumed = bufferSet[k].filled = 0;
			}
			bufferSetThreadStop = false;
			pthread_create(&bufferSetThread, NULL, bufferSetThreadRoutine, (void *)this);
			bufferSetThreadValid = true;
		}
	}
	else {
		data[0] = 0xFFFF0000U;
		status = WriteAndCheck(base_addr0 + acqStatusPointerReg * 4, data, 1);
		
		if(bufferSetThreadValid) {
			pthread_mutex_lock(&bufferSetMutex);
			bufferSetThreadStop = true;
			pthread_mutex_unlock(&bufferSetMutex);
			pthread_cond_signal(&bufferSetCondConsumed);
			pthread_join(bufferSetThread, NULL);
			bufferSetThreadValid = false;
		}
	}
	
	
	return status;	
}


// enum {
// 
//   INITCARD,
//   INITRST,
//   DISPREGS,
//   RDDCSR,
//   RDDDMACR,
//   RDWDMATLPA,
//   RDWDMATLPS,
//   RDWDMATLPC,
//   RDWDMATLPP,
//   RDRDMATLPP,
//   RDRDMATLPA,
//   RDRDMATLPS,
//   RDRDMATLPC,
//   RDWDMAPERF,
//   RDRDMAPERF,
//   RDRDMASTAT,
//   RDNRDCOMP,
//   RDRCOMPDSIZE,
//   RDDLWSTAT,
//   RDDLTRSSTAT,
//   RDDMISCCONT,
//   RDDMISCONT,
//   RDDLNKC,
//   DFCCTL,
//   DFCPINFO,
//   DFCNPINFO,
//   DFCINFO,
// 
//   RDCFGREG,
//   WRCFGREG,
//   RDBMDREG,
//   WRBMDREG,
// 
//   RDBARREG,
//   WRBARREG,
// 
//   WRDDMACR,
//   WRWDMATLPS,
//   WRWDMATLPC,
//   WRWDMATLPP,
//   WRRDMATLPS,
//   WRRDMATLPC,
//   WRRDMATLPP,
//   WRDMISCCONT,
//   WRDDLNKC,
// 
//   NUMCOMMANDS
// 
// };
// 
// typedef struct bmdwrite {
// 	int reg;
// 	int value;
// } bmdwr;

int PFP_KX7::WriteWithoutCheck(int reg, uint32_t *data, int count) 
{
	struct ioctl_reg_t ioctl_reg;
	for (unsigned i = 0; i < count; i++) {
		ioctl_reg.offset = reg + i*4;	// Offset in BAR1 in bytes
		ioctl_reg.value = data[i];
		ioctl(fd, PSDAQ_IOCTL_WRITE_REGISTER, &ioctl_reg);
	}

	return count;
	
	
};

int PFP_KX7::ReadWithoutCheck(int reg, uint32_t *data, int count)
{
	struct ioctl_reg_t ioctl_reg;
	for (unsigned i = 0; i < count; i++) {
		ioctl_reg.offset = reg + i*4;	// Offset in BAR1 in bytes
		
		ioctl(fd, PSDAQ_IOCTL_READ_REGISTER, &ioctl_reg);
		data[i] = ioctl_reg.value;
	}

	return count;
};
int PFP_KX7::WriteAndCheck(int reg, uint32_t *data, int count)
{
	uint32_t *readBackBuffer = new uint32_t[count];
	bool fail = true;
	while(fail) {
		WriteWithoutCheck(reg, data, count);
		ReadWithoutCheck(reg, readBackBuffer, count);
		fail = false;
		for(int i = 0; i < count; i++) fail |= (data[i] != readBackBuffer[i]);
	}
	delete [] readBackBuffer;
	return count;
};

int PFP_KX7::ReadAndCheck(int reg, uint32_t *data, int count)
{
	uint32_t *readBackBuffer = new uint32_t[count];
	bool fail = true;
	while(fail) {
		ReadWithoutCheck(reg, data, count);
		ReadWithoutCheck(reg, readBackBuffer, count);
		fail = false;
		for(int i = 0; i < count; i++) fail |= (data[i] != readBackBuffer[i]);
	}
	delete [] readBackBuffer;
	return count;

};

uint64_t PFP_KX7::getPortUp()
{
	uint64_t reply = 0;
	uint32_t channelUp = 0;
	ReadAndCheck(base_addr0 + statusReg * 4, &channelUp, 1);
	reply = channelUp;	
	return reply;
}

uint64_t PFP_KX7::getPortCounts(int channel, int whichCount)
{
	uint64_t reply;
	ReadWithoutCheck(base_addr0 + (statusReg + 1 + 6*channel + 2*whichCount)* 4, (uint32_t *)&reply, 2);
	return reply;
}

int PFP_KX7::setSorter(unsigned mode)
{

	// This word is used for multiple purposes
	// Thus, we read it and modify just the sort enable bit
	uint32_t currentSetting;
	ReadAndCheck(base_addr0 + ConfigReg * 4, &currentSetting, 1);
	// Clear the sorter enable bit
	currentSetting = currentSetting & 0xEFFFFFFF;
	// Set the sorter disable bit based on the desired mode
	currentSetting = currentSetting | (!mode ? 0x10000000 : 0x00000000);
	WriteAndCheck(base_addr0 + ConfigReg * 4, &currentSetting, 1);

	return 0;
}

int PFP_KX7::setCoincidenceTrigger(CoincidenceTriggerConfig *config)
{

	if(config == NULL || config->enable == 0) {
		// Write a allow all configuration
		uint32_t word32 = 0x0F000000;
		WriteAndCheck(base_addr0 + CoincWindowReg * 4, &word32, 1);

		word32 = 0x00000000;
		WriteAndCheck(base_addr0 + ThresholdReg * 4, &word32, 1);
		
		word32 = 0x00000000;
		for(int n = 0; n < 6; n++) {
			WriteAndCheck(base_addr0 + (CoincMasksReg + n) * 4, &word32, 1);
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
		WriteAndCheck(base_addr0 + CoincWindowReg * 4, &word32, 1);

		word32 = config->minToT;
		WriteAndCheck(base_addr0 + ThresholdReg * 4, &word32, 1);

		uint32_t mask[6];
		for(int n = 0; n < 6; n++) {
			word32 = 0x00000000;
			word32 |= config->mask[2*n + 0] & 0xFFF;
			word32 |= (config->mask[2*n + 1] & 0xFFF) << 16;
			mask[n] = word32;
		}
		WriteAndCheck(base_addr0 + CoincMasksReg * 4, mask, 6);
	}
	return 0;
}

int PFP_KX7::setGateEnable(unsigned mode)
{
	uint32_t data[1];
	int status;
	
	data[0] = (mode != 0) ? 0x000FFFFU : 0xFFFF0000U;
	status = WriteAndCheck(base_addr0 + GateEnableReg * 4, data, 1);	
	return status;	
}
