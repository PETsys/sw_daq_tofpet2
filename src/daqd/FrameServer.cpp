#include "FrameServer.hpp"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>  
#include <assert.h>
#include <errno.h>
#include "boost/date_time/posix_time/posix_time.hpp"

using namespace PETSYS;

static const char *shmObjectPath = "/daqd_shm";

FrameServer::FrameServer(int nFEB, int *feTypeMap, int debugLevel)
	: debugLevel(debugLevel)
{

	
	dataFrameSharedMemory_fd = shm_open(shmObjectPath, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	if(dataFrameSharedMemory_fd < 0) {
		perror("Error creating shared memory");
		fprintf(stderr, "Check that no other instance is running and rm /dev/shm%s\n", shmObjectPath);
		exit(1);
	}
	
	unsigned long dataFrameSharedMemorySize = MaxRawDataFrameQueueSize * sizeof(RawDataFrame);
	ftruncate(dataFrameSharedMemory_fd, dataFrameSharedMemorySize);
	
	dataFrameSharedMemory = (RawDataFrame *)mmap(NULL, 
						  dataFrameSharedMemorySize, 
						  PROT_READ | PROT_WRITE, 
						  MAP_SHARED, 
						  dataFrameSharedMemory_fd, 
						  0);
	if(dataFrameSharedMemory == NULL) {
		perror("Error mmaping() shared memory");
		exit(1);
	}

	dataFrameWritePointer = 0;
	dataFrameReadPointer = 0;
	
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&condCleanDataFrame, NULL);
	pthread_cond_init(&condDirtyDataFrame, NULL);
	pthread_cond_init(&condReplyQueue, NULL);
	die = true;
	acquisitionMode = 0;
	hasWorker = false;
	
	printf("Size of frame is %u\n", sizeof(RawDataFrame));
}

FrameServer::~FrameServer()
{
	printf("FrameServer::~FrameServer()\n");
	// WARNING: stopWorker() should be called from derived class destructors!

	pthread_cond_destroy(&condReplyQueue);
	pthread_cond_destroy(&condDirtyDataFrame);
	pthread_cond_destroy(&condCleanDataFrame);
	pthread_mutex_destroy(&lock);	
	
	unsigned long dataFrameSharedMemorySize = MaxRawDataFrameQueueSize * sizeof(RawDataFrame);
	munmap(dataFrameSharedMemory, dataFrameSharedMemorySize);
	shm_unlink(shmObjectPath);
	
}

void FrameServer::startAcquisition(int mode)
{
	// NOTE: By the time we got here, the DAQ card has synced the system and we should be in the
	// 100 ms sync'ing pause
	
	// Now we just have to wipe the buffers
	// It should be done twice, because the FrameServer worker thread may be waiting for a frame slot
	// and it may fill at least one slot
	
	pthread_mutex_lock(&lock);
	dataFrameWritePointer = 0;
	dataFrameReadPointer = 0;
	acquisitionMode = 0;
	pthread_cond_signal(&condCleanDataFrame);
	pthread_mutex_unlock(&lock);

	usleep(220000);
	
	pthread_mutex_lock(&lock);
	dataFrameWritePointer = 0;
	dataFrameReadPointer = 0;
	acquisitionMode = mode;
	pthread_cond_signal(&condCleanDataFrame);
	pthread_mutex_unlock(&lock);
}

void FrameServer::stopAcquisition()
{
	pthread_mutex_lock(&lock);
	acquisitionMode = 0;
	dataFrameWritePointer = 0;
	dataFrameReadPointer = 0;
	pthread_cond_signal(&condCleanDataFrame);
	pthread_mutex_unlock(&lock);	
}



const char *FrameServer::getDataFrameSharedMemoryName()
{
	return shmObjectPath;
}

unsigned FrameServer::getDataFrameWritePointer()
{
	pthread_mutex_lock(&lock);
	unsigned r = dataFrameWritePointer;
	pthread_mutex_unlock(&lock);
	return r % (2*MaxRawDataFrameQueueSize);
}

unsigned FrameServer::getDataFrameReadPointer()
{
	pthread_mutex_lock(&lock);
	unsigned r = dataFrameReadPointer;
	pthread_mutex_unlock(&lock);
	return r % (2*MaxRawDataFrameQueueSize);
}

void FrameServer::setDataFrameReadPointer(unsigned ptr)
{
	pthread_mutex_lock(&lock);
	dataFrameReadPointer = ptr % (2*MaxRawDataFrameQueueSize);
	pthread_cond_signal(&condCleanDataFrame);
	pthread_mutex_unlock(&lock);
}

void FrameServer::startWorker()
{
	printf("FrameServer::startWorker called...\n");
	if(hasWorker) return;

	die = false;
	hasWorker = true;
	pthread_create(&worker, NULL, runWorker, (void*)this);
	

	printf("FrameServer::startWorker exiting...\n");

}

void FrameServer::stopWorker()
{
	printf("FrameServer::stopWorker called...\n");
	
	die = true;
	
	pthread_mutex_lock(&lock);
	pthread_cond_signal(&condCleanDataFrame);
	pthread_cond_signal(&condDirtyDataFrame);
	pthread_mutex_unlock(&lock);

	if(hasWorker) {
		hasWorker = false;
		pthread_join(worker, NULL);
	}

	printf("FrameServer::stopWorker exiting...\n");
}

void *FrameServer::runWorker(void *arg)
{
	FrameServer *F = (FrameServer *)arg;
        return F->doWork();
}

bool FrameServer::parseDataFrame(RawDataFrame *dataFrame)
{
	
	unsigned long long frameID = dataFrame->data[0] & 0xFFFFFFFFFULL;
	unsigned long frameSize = (dataFrame->data[0] >> 36) & 0x7FFF;
	unsigned nEvents = dataFrame->data[1] & 0xFFFF;
	bool frameLost = (dataFrame->data[1] & 0x10000) != 0;
	
	if (frameSize != 2 + nEvents) {
		printf("Inconsistent size: got %4d words, expected %4d words(%d events).\n", 
			frameSize, 2 + nEvents, nEvents);
		return false;
	}
	
	return true;
}

int FrameServer::setSorter(unsigned mode)
{
	return -1;
}

int FrameServer::setCoincidenceTrigger(CoincidenceTriggerConfig *config)
{
	return -1;
}

int FrameServer::setIdleTimeCalculation(unsigned mode)
{
	idleTimeMode = mode;
}

int FrameServer::setGateEnable(unsigned mode)
{
	return -1;
}

