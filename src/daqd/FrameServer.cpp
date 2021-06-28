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

void FrameServer::allocateSharedMemory(const char * shmName, int &shmfd, RawDataFrame * &shmPtr)
{
	shmfd = shm_open(shmName, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	if(shmfd < 0) {
		perror("Error creating shared memory");
		fprintf(stderr, "Check that no other instance is running and rm /dev/shmPtr%s\n", shmName);
		return;
	}
	
	unsigned long shmSize = MaxRawDataFrameQueueSize * sizeof(RawDataFrame);
	ftruncate(shmfd, shmSize);
	
	shmPtr = (RawDataFrame *)mmap(NULL, 
						  shmSize, 
						  PROT_READ | PROT_WRITE, 
						  MAP_SHARED, 
						  shmfd, 
						  0);
	if(shmPtr == NULL) {
		perror("Error mmaping() shared memory");
		return;
	}
	
}

void FrameServer::freeSharedMemory(const char * shmName, int shmfd, RawDataFrame * shmPtr)
{
	if(shmPtr != NULL) {
		unsigned long shmSize = MaxRawDataFrameQueueSize * sizeof(RawDataFrame);
		munmap(shmPtr, shmSize);
	}
	
	if(shmfd != -1) {
		close(shmfd);
	}
	
	shm_unlink(shmName);
}

FrameServer::FrameServer(const char * shmName, int shmfd, RawDataFrame * shmPtr, int debugLevel)
	: shmName(shmName), shmfd(shmfd), shmPtr(shmPtr), debugLevel(debugLevel)
{



	dataFrameWritePointer = 0;
	dataFrameReadPointer = 0;
	
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&condCleanDataFrame, NULL);
	pthread_cond_init(&condDirtyDataFrame, NULL);
	pthread_cond_init(&condReplyQueue, NULL);
	die = true;
	acquisitionMode = 0;
	hasWorker = false;
	
	printf("Size of frame is %lu\n", sizeof(RawDataFrame));
}

FrameServer::~FrameServer()
{
	printf("FrameServer::~FrameServer()\n");
	// WARNING: stopWorker() should be called from derived class destructors!

	pthread_cond_destroy(&condReplyQueue);
	pthread_cond_destroy(&condDirtyDataFrame);
	pthread_cond_destroy(&condCleanDataFrame);
	pthread_mutex_destroy(&lock);	
}

void FrameServer::startAcquisition(int mode)
{
	// NOTE: By the time we got here, the DAQ card has synced the system and we should be in the
	// 100 ms sync'ing pause
	
	// Now we just have to wipe the buffers
	// It should be done twice, because the FrameServer worker thread may be waiting for a frame slot
	// and it may fill at least one slot
	
	pthread_mutex_lock(&lock);
	acquisitionMode = 0;
	dataFrameWritePointer = 0;
	dataFrameReadPointer = 0;
	pthread_cond_signal(&condCleanDataFrame);
	pthread_mutex_unlock(&lock);
	stopWorker();

	usleep(220000);
	
	pthread_mutex_lock(&lock);
	dataFrameWritePointer = 0;
	dataFrameReadPointer = 0;
	acquisitionMode = mode;
	pthread_cond_signal(&condCleanDataFrame);
	pthread_mutex_unlock(&lock);
	startWorker();
}

void FrameServer::stopAcquisition()
{
	pthread_mutex_lock(&lock);
	acquisitionMode = 0;
	dataFrameWritePointer = 0;
	dataFrameReadPointer = 0;
	pthread_cond_signal(&condCleanDataFrame);
	pthread_mutex_unlock(&lock);
	stopWorker();
}

bool FrameServer::amAcquiring()
{
	return hasWorker;
}


const char *FrameServer::getDataFrameSharedMemoryName()
{
	return shmName;
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
	if(hasWorker) return;

	die = false;
	pthread_create(&worker, NULL, runWorker, (void*)this);
	hasWorker = true;
}

void FrameServer::stopWorker()
{
	if(!hasWorker) return;
	die = true;
	pthread_mutex_lock(&lock);
	pthread_cond_signal(&condCleanDataFrame);
	pthread_cond_signal(&condDirtyDataFrame);
	pthread_mutex_unlock(&lock);
	pthread_join(worker, NULL);
	hasWorker = false;
}

void *FrameServer::runWorker(void *arg)
{
	FrameServer *F = (FrameServer *)arg;
	printf("INFO: FrameServer::runWorker starting...\n");
        return F->doWork();
	printf("INFO: FrameServer::runWorker finished!\n");
}

bool FrameServer::parseDataFrame(RawDataFrame *dataFrame)
{
	
	auto frameID = dataFrame->data[0] & 0xFFFFFFFFFULL;
	auto frameSize = (dataFrame->data[0] >> 36) & 0x7FFF;
	auto nEvents = dataFrame->data[1] & 0xFFFF;
	bool frameLost = (dataFrame->data[1] & 0x10000) != 0;
	
	if (frameSize != 2 + nEvents) {
		printf("Inconsistent size: got %4lu words, expected %4lu words(%lu events).\n", 
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
	return 0;
}

int FrameServer::setGateEnable(unsigned mode)
{
	return -1;
}

