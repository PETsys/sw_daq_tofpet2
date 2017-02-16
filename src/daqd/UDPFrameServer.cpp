#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <boost/crc.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>  
#include <assert.h>
#include <errno.h>

#include "UDPFrameServer.hpp"

static const char *feAddr = "192.168.1.25";
static const unsigned short fePort = 2000;

static int myFeTypeMap[1] = { 0 };

using namespace PETSYS;

UDPFrameServer::UDPFrameServer(int debugLevel)
	: FrameServer(1, myFeTypeMap, debugLevel)
{
	udpSocket = socket(AF_INET,SOCK_DGRAM,0);
	assert(udpSocket != -1);
	
	// "Connect" our socket to the front end service
	struct sockaddr_in localAddress;	
	memset(&localAddress, 0, sizeof(struct sockaddr_in));
	localAddress.sin_family = AF_INET;
	localAddress.sin_addr.s_addr=inet_addr(feAddr);  
	localAddress.sin_port=htons(fePort);
	int r = connect(udpSocket, (struct sockaddr *)&localAddress, sizeof(struct sockaddr_in));
	assert(r == 0);
	
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 100000;
	r = setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv));
	assert(r == 0);
	
	char buffer[32];
	memset(buffer, 0xFF, sizeof(buffer));
	send(udpSocket, buffer, sizeof(buffer), 0);
	r = recv(udpSocket, buffer, sizeof(buffer), 0);
	if (r < 1) {
		fprintf(stderr, "ERROR: Failed to receive a reply from FPGA\n");
		exit(0);
	}
	printf("Got FPGA reply\n");
	
		
	startWorker();
}


UDPFrameServer::~UDPFrameServer()
{
	stopWorker();
}


static bool isFull(unsigned writePointer, unsigned readPointer)
{
	return (writePointer != readPointer) && ((writePointer % MaxRawDataFrameQueueSize) == (readPointer % MaxRawDataFrameQueueSize));
}


void *UDPFrameServer::doWork()
{	
	printf("UDPFrameServer::runWorker starting...\n");
	UDPFrameServer *m = this;

	long nFramesFound = 0;
	long nFramesPushed = 0;
	
	RawDataFrame *devNull = new RawDataFrame;
	
	unsigned char rxBuffer[2048];
	uint64_t dataBuffer[2048/sizeof(uint64_t)];
	while(!m->die) {
		int r = recv(m->udpSocket, rxBuffer, sizeof(rxBuffer), 0);

		if (r == -1 && (errno = EAGAIN || errno == EWOULDBLOCK)) {	
			continue;
		}
		
		assert(r >= 0);

		if(rxBuffer[0] == 0x5A) {
			if(m->debugLevel > 2) printf("Worker:: Found a reply frame with %d bytes\n", r);
			reply_t *reply = new reply_t;
			memcpy(reply->buffer, rxBuffer + 1, r - 1);
			reply->size = r - 1;
			pthread_mutex_lock(&m->lock);
			m->replyQueue.push(reply);
			pthread_mutex_unlock(&m->lock);
		}
		else if(rxBuffer[0] == 0xA5) {
			if(m->debugLevel > 2) printf("Worker:: Found a data frame with %d bytes\n", r);
			if (m->acquisitionMode != 0) {			
				unsigned nWords = (r-1)/sizeof(uint64_t);			
				memcpy(dataBuffer, rxBuffer+1, nWords*sizeof(uint64_t));
/*				uint64_t *p1 = (uint64_t *)rxBuffer+1;
				for(unsigned i = 0; i < nWords; i++)
					dataBuffer[i] = be64toh(p1[i]);*/
				
				uint64_t *p = dataBuffer;
				do {
					unsigned frameSize = (p[0] >> 36) & 0x7FFF;
					
					RawDataFrame *dataFrame = devNull;
					
					pthread_mutex_lock(&m->lock);
					if(!isFull(m->dataFrameWritePointer,  m->dataFrameReadPointer)) {
						dataFrame = &dataFrameSharedMemory[m->dataFrameWritePointer  % MaxRawDataFrameQueueSize];
					}
					pthread_mutex_unlock(&m->lock);
					
					memcpy(dataFrame->data, p, frameSize * sizeof(uint64_t));
					if(!m->parseDataFrame(dataFrame)) break;
					
					pthread_mutex_lock(&m->lock);					
					if(dataFrame != devNull) {
						m->dataFrameWritePointer = (m->dataFrameWritePointer + 1) % (2*MaxRawDataFrameQueueSize);
					}
					pthread_cond_signal(&m->condDirtyDataFrame);
					pthread_mutex_unlock(&m->lock);
					p += frameSize;
					
				} while(p < dataBuffer + nWords);
				
			}
		}
		else {
			printf("Worker: found an unknown frame (0x%02X) with %d bytes\n", unsigned(rxBuffer[0]), r);
			
		}
		pthread_mutex_lock(&m->lock);					
		pthread_cond_signal(&m->condReplyQueue);
		pthread_mutex_unlock(&m->lock);					
			
		
		
	}
	
	delete devNull;
	printf("UDPFrameServer::runWorker exiting...\n");
	return NULL;
}


int UDPFrameServer::sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength)
{
	boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();	
	uint16_t sentSN = (unsigned(buffer[0]) << 8) + unsigned(buffer[1]);	
	boost::posix_time::ptime t1 = boost::posix_time::microsec_clock::local_time();

	send(udpSocket, buffer, commandLength, 0);
	
	boost::posix_time::ptime t2 = boost::posix_time::microsec_clock::local_time();

	int replyLength = 0;	
	int nLoops = 0;
	do {
		pthread_mutex_lock(&lock);
		reply_t *reply = NULL;
	
/*		if(replyQueue.empty()) {
			pthread_cond_wait(&condReplyQueue, &lock);
		}*/

		if(replyQueue.empty()) {
			struct timespec ts; 
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_nsec += 10000000L; // 10 ms
			ts.tv_sec += (ts.tv_nsec / 100000000L);
			ts.tv_nsec = (ts.tv_nsec % 100000000L);                        
			pthread_cond_timedwait(&condReplyQueue, &lock, &ts);
		}

		if (!replyQueue.empty()) {
			reply = replyQueue.front();
			replyQueue.pop();
			
		}
		pthread_mutex_unlock(&lock);
		
		if(reply == NULL) {
			boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
			if ((now - start).total_milliseconds() > CommandTimeout) 
				break;
			else
				continue;
			
		}
		//printf("Found something on queue with size: %d\n", reply->size);
	
		if (reply->size < 2) {
			fprintf(stderr, "WARNING: Reply only had %d bytes\n", reply->size);
		}
		else {
			uint16_t recvSN = (unsigned(reply->buffer[0]) << 8) + reply->buffer[1];
			if(sentSN == recvSN) {
				memcpy(buffer, reply->buffer, reply->size);
				replyLength = reply->size;
			}
			else {
				fprintf(stderr, "Mismatched SN: sent %6hx, got %6hx\n", sentSN, recvSN);
			}
		}
		
		delete reply;		
		nLoops++;

	} while(replyLength == 0);
	
	boost::posix_time::ptime t3 = boost::posix_time::microsec_clock::local_time();
	
	if (replyLength == 0 ) {
		printf("Command reply timing: (t2 - t1) => %ld, (t3 - t2) => %ld, i = %d, status = %s\n", 
			(t2 - t1).total_milliseconds(), 
			(t3 - t2).total_milliseconds(),
			nLoops,
			replyLength > 0 ? "OK" : "FAIL"
		);
	}
	
	return replyLength;
}

uint64_t UDPFrameServer::getPortUp()
{
	return 1;
};

uint64_t UDPFrameServer::getPortCounts(int port, int whichCount)
{
	return 0;
}
