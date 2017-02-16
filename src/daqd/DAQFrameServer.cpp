#include "DAQFrameServer.hpp"
#include "SHM.hpp"
#include <string.h>
#include <math.h>
#include <stdio.h>
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

using namespace DAQd;

AbstractDAQCard::AbstractDAQCard()
{
}

AbstractDAQCard::~AbstractDAQCard()
{
}

const uint64_t IDLE_WORD = 0xFFFFFFFFFFFFFFFFULL;
const uint64_t HEADER_WORD = 0xFFFFFFFFFFFFFFF5ULL;
const uint64_t TRAILER_WORD = 0xFFFFFFFFFFFFFFFAULL;

DAQFrameServer::DAQFrameServer(AbstractDAQCard *card, int nFEB, int *feTypeMap, int debugLevel)
  : FrameServer(nFEB, feTypeMap, debugLevel), DP(card)
{
	
	printf("allocated DP object = %p\n", DP);
	
	startWorker();
}


DAQFrameServer::~DAQFrameServer()
{
	stopWorker();
	printf("DAQFrameServer::~DAQFrameServer\n");
	delete DP;	
}

void DAQFrameServer::startAcquisition(int mode)
{
	DP->setAcquistionOnOff(false);
	DP->setAcquistionOnOff(true);
	FrameServer::startAcquisition(mode);
}

void DAQFrameServer::stopAcquisition()
{
	DP->setAcquistionOnOff(false);
 	FrameServer::stopAcquisition();
}

int DAQFrameServer::sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength)
{
	uint16_t sentSN = (unsigned(buffer[0]) << 8) + unsigned(buffer[1]);	

	boost::posix_time::ptime t1 = boost::posix_time::microsec_clock::local_time();
	DP->sendCommand(portID, slaveID, buffer,bufferSize, commandLength);
	

	boost::posix_time::ptime t2 = boost::posix_time::microsec_clock::local_time();

	int replyLength = 0;
	int nLoops = 0;
	char replyBuffer[12*4];
	do {
		boost::posix_time::ptime tl = boost::posix_time::microsec_clock::local_time();
		if((tl - t2).total_milliseconds() > CommandTimeout) break;

		int status = DP->recvReply(replyBuffer, 12*4);
		if (status < 0) {
			continue; // Timed out and did not receive a reply
		}
		
		if(status < 2) { // Received something weird
			fprintf(stderr, "Received very short reply: %d bytes\n", status);
			continue;
		}
		
		uint16_t recvSN = (unsigned(replyBuffer[0]) << 8) + replyBuffer[1];
		if(sentSN != recvSN) {
			fprintf(stderr, "Mismatched SN: sent %6hx, got %6hx\n", sentSN, recvSN);
			continue;
		}
	
		// Got what looks like a valid reply with the correct SN
		replyLength = status;
		memcpy(buffer, replyBuffer, replyLength);		
	} while(replyLength == 0);	
	
	boost::posix_time::ptime t3 = boost::posix_time::microsec_clock::local_time();
	
	if (replyLength == 0 ) {
		printf("Command reply timing: (t2 - t1) => %ld us, (t3 - t2) => %ld us, i = %d, status = %s\n", 
			(t2 - t1).total_microseconds(), 
			(t3 - t2).total_microseconds(),
			nLoops,
			replyLength > 0 ? "OK" : "FAIL"
		);
	}
	
	return replyLength;
}


static bool isFull(unsigned writePointer, unsigned readPointer)
{
	return (writePointer != readPointer) && ((writePointer % MaxDataFrameQueueSize) == (readPointer % MaxDataFrameQueueSize));
}

void *DAQFrameServer::doWork()
{	

	printf("DAQFrameServer::runWorker starting...\n");
	printf("DP object is %p\n", DP);
	DAQFrameServer *m = this;
	
	DataFrame *devNull = new DataFrame;
	
	
	long nFramesFound = 0;
	long nFramesPushed = 0;

	const int maxSkippedLoops = 32*1024;
	int skippedLoops = 0;
	bool lastFrameWasBad = true;
	unsigned long long frameCount = 0;

	while(!die){
		if(skippedLoops >= maxSkippedLoops) {
			//printf("skippedLoops = %d\n", skippedLoops);
			skippedLoops = 0;
			
		}
		
		uint64_t headerWords[2];
		int nWords;

		// If we are comming back from a bad frame, let's dump until we read an idle
		if(lastFrameWasBad) {
			nWords = DP->getWords(&headerWords[0], 1);
			//printf("DBG1 %016llx\n", headerWords[0]);
			if(nWords != 1) { skippedLoops = 1000001; continue; }			
			if(headerWords[0] != IDLE_WORD) { skippedLoops++; continue; }
		}
		lastFrameWasBad = false;

		// Read something, which may be a filler or a header
		nWords = DP->getWords(&headerWords[0], 1);
		if (nWords != 1) { skippedLoops = 1000002; continue; }		
		if(headerWords[0] == IDLE_WORD) { skippedLoops++; continue; }
/*		printf("DBG2 %016llx\n", headerWords[0]);		*/
		if(headerWords[0] != HEADER_WORD) { lastFrameWasBad = true; skippedLoops = 1000003; continue; }
		skippedLoops = 0;
		
		// Read the frame's first 2 words
		nWords = DP->getWords(&headerWords[0], 2);
		if (nWords != 2) { skippedLoops = 1000003; continue; }
		//printf("DBG3 %016llx\n", headerWords[0]);


		unsigned long long frameSource = (headerWords[0] >> 54) & 0x400;
		unsigned long long frameType = (headerWords[0] >> 51) & 0x7;
		unsigned long long frameSize = (headerWords[0] >> 36) & 0x7FFF;
		unsigned long long frameID = (headerWords[0] ) & 0xFFFFFFFFF;
		unsigned long long nEvents = headerWords[1] & 0xFFFF;
		bool frameLost = (headerWords[1] & 0x10000) != 0;

		if(frameSize > MaxDataFrameSize) {
			fprintf(stderr, "Excessive frame size: %u\n word (max is %u)", frameSize, MaxDataFrameSize);
			lastFrameWasBad = true; skippedLoops = 1000007; continue;
		}

		// Drop most of lost frames (lost = 1, nEvents = 0) to avoid wasting buffer space
		// Keep ~1% just to ensure software always has something
		bool dropLostFrame = (nEvents == 0) && frameLost &&  (frameCount % 128 != 0);
		frameCount += 1;

		DataFrame *dataFrame = devNull;
		if (m->acquisitionMode != 0 && !dropLostFrame) {
			// Get a free frame from the queue, if possible
			// If not, just carry on with the devNull frame
			pthread_mutex_lock(&m->lock);
			if(!isFull(m->dataFrameWritePointer, m->dataFrameReadPointer)) {
				dataFrame = &dataFrameSharedMemory[m->dataFrameWritePointer % MaxDataFrameQueueSize];
			}
			pthread_mutex_unlock(&m->lock);
		}
		if(die) break;
		
		
		dataFrame->data[0] = headerWords[0]; //one word was already read
		dataFrame->data[1] = headerWords[1]; //one word was already read
		nWords = DP->getWords(dataFrame->data+2,frameSize-2);
		if(nWords < 0) { lastFrameWasBad = true; skippedLoops = 1000004; continue; }
		
		// After a frame, we always have a tailerword word
		uint64_t trailerWord;
		nWords = DP->getWords(&trailerWord, 1);
		if(nWords < 0) { lastFrameWasBad = true; skippedLoops = 1000005; continue; }
// 		printf("DBG4 %016llx \n", trailerWord);
		if(trailerWord != TRAILER_WORD) { 
/*			for(unsigned i = 0; i < frameSize; i++) { 
				printf("%4d %016llx \n", i, dataFrame->data[i]);
			}
			printf("TAIL %016llx \n", trailerWord);
*/				
				lastFrameWasBad = true; skippedLoops = 1000006; continue; 
		}

		if (!m->parseDataFrame(dataFrame))
			continue;

		if(dataFrame != devNull) {
			pthread_mutex_lock(&m->lock);
			m->dataFrameWritePointer = (m->dataFrameWritePointer + 1)  % (2*MaxDataFrameQueueSize);
			pthread_mutex_unlock(&m->lock);
		}
	}	
	delete devNull;
	printf("DAQFrameServer::runWorker exiting...\n");
}

uint64_t DAQFrameServer::getPortUp()
{
	return DP->getPortUp();
}

uint64_t DAQFrameServer::getPortCounts(int port, int whichCount)
{
	return DP->getPortCounts(port, whichCount);
}

int AbstractDAQCard::setSorter(unsigned mode)
{
	return -1;
}

int AbstractDAQCard::setCoincidenceTrigger(CoincidenceTriggerConfig *config)
{
	return -1;
}

int DAQFrameServer::setSorter(unsigned mode)
{
	return DP->setSorter(mode);
}
int DAQFrameServer::setCoincidenceTrigger(CoincidenceTriggerConfig *config)
{
	int r = DP->setCoincidenceTrigger(config);
	return r;
}

int AbstractDAQCard::setGateEnable(unsigned mode)
{
	return -1;
}

int DAQFrameServer::setGateEnable(unsigned mode)
{
	return DP->setGateEnable(mode);
}