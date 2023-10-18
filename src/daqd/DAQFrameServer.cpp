// kate: mixedindent off; space-indent off; indent-pasted-text false; tab-width 8; indent-width 8; replace-tabs: off;
// vim: tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab

#include "DAQFrameServer.hpp"
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

using namespace PETSYS;

AbstractDAQCard::AbstractDAQCard()
{
}

AbstractDAQCard::~AbstractDAQCard()
{
}

const uint64_t IDLE_WORD = 0xFFFFFFFFFFFFFFFFULL;
const uint64_t HEADER_WORD = 0xFFFFFFFFFFFFFFF5ULL;
const uint64_t TRAILER_WORD = 0xFFFFFFFFFFFFFFFAULL;


DAQFrameServer *DAQFrameServer::createFrameServer(std::vector<AbstractDAQCard *> cards, unsigned daqCardPortBits, const char * shmName, int shmfd, RawDataFrame * shmPtr, int debugLevel)
{
	return new DAQFrameServer(cards, daqCardPortBits, shmName, shmfd, shmPtr, debugLevel);
}

DAQFrameServer::DAQFrameServer(std::vector<AbstractDAQCard *> cards, unsigned daqCardPortBits, const char * shmName, int shmfd, RawDataFrame * shmPtr, int debugLevel)
: FrameServer(shmName, shmfd, shmPtr, debugLevel), cards(cards), daqCardPortBits(daqCardPortBits)
{
	int nCards = cards.size();
	size_t bs = MaxRawDataFrameSize + 4;
	lastFrameWasBad = true;
}
	

DAQFrameServer::~DAQFrameServer()
{
	stopWorker();
}

void DAQFrameServer::startAcquisition(int mode)
{
	stopAcquisition();

	for(auto card = cards.begin(); card != cards.end(); card++) {
		(*card)->setAcquistionOnOff(true);
	}

	lastFrameWasBad = true;
	FrameServer::startAcquisition(mode);
}

void DAQFrameServer::stopAcquisition()
{
	FrameServer::stopAcquisition();
	for(auto card = cards.begin(); card != cards.end(); card++) {
		(*card)->setAcquistionOnOff(false);
	}
}

int DAQFrameServer::sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength)
{
	// Convert these int to uint64_t
	uint64_t sendPortID = portID;
	uint64_t sendSlaveID = slaveID;

	uint64_t cardID = sendPortID >> daqCardPortBits;
	sendPortID = sendPortID % (1 << daqCardPortBits);

	if(cardID >= cards.size()) {
		fprintf(stderr, "Error in DAQFrameServer::sendCommand(): packet addressed to non-existing card %lu.\n", cardID);
		return -1;

	}

	AbstractDAQCard *card = cards[cardID];


	uint16_t sentSN = (unsigned(buffer[0]) << 8) + unsigned(buffer[1]);
	
	const int MAX_PACKET_WORDS = 256;
	int packetLength = 2 + ceil(commandLength / 8.0);
	if(packetLength > MAX_PACKET_WORDS) {
		fprintf(stderr, "Error in DAQFrameServer::sendCommand(): packet length %d is too large.\n", packetLength);
		return -1;
	}
	
	uint64_t packetBuffer[MAX_PACKET_WORDS];
	for(int i = 0; i < 8; i++) packetBuffer[i] = 0x0FULL << (4*i);

	uint64_t header = 0;
	header = header + (8ULL << 36);
	header = header + (sendPortID << 59) + (sendSlaveID << 54);

	packetBuffer[0] = header;
	packetBuffer[1] = commandLength;
	memcpy((void*)(packetBuffer+2), buffer, commandLength);

	card->clearReplyQueue();
	boost::posix_time::ptime t1 = boost::posix_time::microsec_clock::local_time();
	card->sendCommand(packetBuffer, packetLength);
	

	boost::posix_time::ptime t2 = boost::posix_time::microsec_clock::local_time();

	int nLoops = 0;
	while(true) {
		nLoops += 1;
		
		boost::posix_time::ptime t3 = boost::posix_time::microsec_clock::local_time();
		if((t3 - t2).total_milliseconds() > CommandTimeout) {
			printf("WARNING: Command timeout: (t2 - t1) => %ld us, (t3 - t2) => %ld us, i = %d\n", 
				(t2 - t1).total_microseconds(), 
				(t3 - t2).total_microseconds(),
				nLoops
			);
			return 0;
		}

		int status = card->recvReply(packetBuffer, MAX_PACKET_WORDS);
		if (status < 0) {
			continue; // Timed out and did not receive a reply
		}
		
		uint64_t recvPortID = packetBuffer[0] >> 59;
		uint64_t recvSlaveID = (packetBuffer[0] >> 54) & 0x1F;

		// Put back the portID bits take by the cardID
		sendPortID = (cardID << daqCardPortBits) | sendPortID;
		recvPortID = (cardID << daqCardPortBits) | recvPortID;

		packetBuffer[0] &= 0x1FFFFFFFFFFFFFFULL;
		packetBuffer[0] |= (recvPortID << 59);

		
		if((sendPortID != recvPortID) || (sendSlaveID != recvSlaveID)) {
			fprintf(stderr, "WARNING: Mismatched address. Sent (%2lu, %2lu), received (%2lu, %2lu).\n",
				sendPortID, sendSlaveID,
				recvPortID, recvSlaveID
			);
			continue;
		}

		int replyLength = packetBuffer[1];
		if(replyLength < 2) { // Received something weird
			fprintf(stderr, "WARNING: Received very short reply from (%2lu, %2lu): %u bytes.\n",
				sendPortID, sendSlaveID,
				replyLength
			);
			continue;
		}
		
		if(replyLength > 8*(MAX_PACKET_WORDS-2)) {
			fprintf(stderr, "WARNING: Truncated packet from (%2lu, %2lu): expected %d bytes.\n",
				sendPortID, sendSlaveID,
				replyLength
			);
			continue;
		}

		if(replyLength > bufferSize) {
			fprintf(stderr, "WARNING: Packet too large from (%2lu, %2lu): %d bytes.\n",
				sendPortID, sendSlaveID,
				replyLength
			);
			continue;
		}
		

		memcpy(buffer, packetBuffer+2, replyLength);
		
		uint16_t recvSN = (unsigned(buffer[0]) << 8) + buffer[1];
		if(sentSN != recvSN) {
			fprintf(stderr, "WARNING: Mismatched SN  from (%2lu, %2lu): sent %04hx, got %04hx.\n",
				sendPortID, sendSlaveID,
				sentSN, recvSN
			);
			continue;
		}

		return replyLength;
	};
	
	return 0;
}


static bool isFull(unsigned writePointer, unsigned readPointer)
{
	return (writePointer != readPointer) && ((writePointer % MaxRawDataFrameQueueSize) == (readPointer % MaxRawDataFrameQueueSize));
}
void *DAQFrameServer::doWork()
{
	std::vector<AbstractDAQCard *> activeCards;
	std::vector<uint64_t> cardsPrefix;
	for (uint64_t i = 0; i < cards.size(); i++) {
		if(cards[i]->getPortUp() != 0x0) {
			activeCards.push_back(cards[i]);
			cardsPrefix.push_back(i << (59 + daqCardPortBits));
		}
	}

	int nCards = cards.size();
	int nActiveCards = activeCards.size();

	while(!die) {
		if(acquisitionMode == 0) continue;

		RawDataFrame *dst = NULL;
		pthread_mutex_lock(&lock);
		if(!isFull(dataFrameWritePointer, dataFrameReadPointer)) {
			dst = &shmPtr[dataFrameWritePointer % MaxRawDataFrameQueueSize];
		}
		pthread_mutex_unlock(&lock);

		if(nCards == 1) {
			uint64_t *tmp = cards[0]->getNextFrame();
			if(tmp == NULL) continue;
			if(dst == NULL) continue;

			uint64_t frameSize = (tmp[0] >> 36) & 0x7FFF;
			memcpy(dst, tmp, frameSize * sizeof(uint64_t));
		}
		else if (nActiveCards == 1) {
			uint64_t *tmp = activeCards[0]->getNextFrame();
			if(tmp == NULL) continue;
			if(dst == NULL) continue;

			dst->data[0] = tmp[0];
			dst->data[1] = tmp[1];
			uint64_t nEvents = tmp[1]  & 0xFFFF;

			uint64_t *src_ptr = tmp + 2;
			uint64_t *end_ptr = src_ptr + nEvents;
			uint64_t *dst_ptr = dst->data + 2;
			uint64_t prefix = cardsPrefix[0];
			for( ; src_ptr < end_ptr; src_ptr++, dst_ptr++)
				*dst_ptr = *src_ptr | prefix;

		}
		else if(nActiveCards == 2) {
			uint64_t *tmp0 = activeCards[0]->getNextFrame();
			uint64_t *tmp1 = activeCards[1]->getNextFrame();
			if(tmp0 == NULL) continue;
			if(tmp1 == NULL) continue;
			if(dst == NULL) continue;

			uint64_t frameID0;
			uint64_t frameID1;
			do {
				frameID0 = tmp0[0] & 0xFFFFFFFFFULL;
				frameID1 = tmp1[0] & 0xFFFFFFFFFULL;
				//fprintf(stderr, "D1 %016llx %016llx\n", frameID0, frameID1);

				if(frameID0 < frameID1) tmp0 = cards[0]->getNextFrame();
				if(frameID1 < frameID0) tmp1 = cards[1]->getNextFrame();

				while(!die && (tmp0 == NULL)) tmp0 = cards[0]->getNextFrame();
				while(!die && (tmp1 == NULL)) tmp1 = cards[1]->getNextFrame();
			} while(!die && (frameID0 != frameID1));

			bool frameLost = ((tmp0[1] | tmp1[1]) & 0x10000) != 0;
			uint64_t nEvents0 = tmp0[1]  & 0xFFFF;
			uint64_t nEvents1 = tmp1[1]  & 0xFFFF;
			//fprintf(stderr, "D2 %4llu %4llu\n", nEvents0, nEvents1);

			if((nEvents0 + nEvents1 + 2) > MaxRawDataFrameSize) {
				frameLost = true;
			}

			if(frameLost) {
				dst->data[0] = (2ULL << 36) | frameID0;
				dst->data[1] = 0x10000;
			}
			else {

				dst->data[0] = ((nEvents0 + nEvents1 + 2ULL) << 36) | frameID0;
				dst->data[1] = nEvents0 + nEvents1;

				uint64_t *src_ptr = tmp0 + 2;
				uint64_t *end_ptr = src_ptr + nEvents0;
				uint64_t *dst_ptr = dst->data + 2;
				uint64_t prefix = cardsPrefix[0];
				for( ; src_ptr < end_ptr; src_ptr++, dst_ptr++)
					*dst_ptr = *src_ptr | prefix;

				src_ptr = tmp1 + 2;
				end_ptr = src_ptr + nEvents1;
				prefix = cardsPrefix[1];
				for( ; src_ptr < end_ptr; src_ptr++, dst_ptr++)
					*dst_ptr = *src_ptr | prefix;
			}



		}
		
		// Do not store frames older than minimumFrameID
		if(dst->getFrameID() < minimumFrameID) continue;
		
		// Store data if we are in acquisition mode and
		// - Frame is not lost
		// - Frame is lost but frameID is a multiple of 128 (forward at least 1% so the process does not freeze)
		if(dst->getFrameLost() && (dst->getFrameID() % 128 != 0)) continue;

		// Update the shared memory pointers to signal a new frame has been written
		pthread_mutex_lock(&lock);
		dataFrameWritePointer = (dataFrameWritePointer + 1)  % (2*MaxRawDataFrameQueueSize);
		pthread_mutex_unlock(&lock);

	}

	return NULL;
}

uint64_t DAQFrameServer::getPortUp()
{
	uint64_t retval = 0;
// 	for(auto card = cards.begin(); card != cards.end(); card++) {
// 		retval = retval << (1 << daqCardPortBits);
// 		retval = retval | (*card)->getPortUp();
// 	}



	unsigned portsPerCard = 1 << daqCardPortBits;
	for(unsigned i = 0; i < cards.size(); i++) {
		uint64_t pup = cards[i]->getPortUp();
		retval |= pup << (i * portsPerCard);
	}

	return retval;
}


uint64_t DAQFrameServer::getDAQTemp()
{
	uint64_t retval = 0;

	for(unsigned i = 0; i < cards.size(); i++) {
		uint16_t temp = 0;
		temp = cards[i]->getDAQTemp();
		//printf("PFP KX7 #%d temp  = %.2f ºC.\n", i, ((float)temp/100));
		retval |= temp << (i * 16);
	}

	return retval;
}


uint64_t DAQFrameServer::getPortCounts(int port, int whichCount)
{
	unsigned portsPerCard = 1 << daqCardPortBits;
	int cardID = port >> daqCardPortBits;
	port = port % portsPerCard;

	if(cardID >= cards.size()) {
		fprintf(stderr, "Error in DAQFrameServer::getPortCounts(): trying to read non-existing card %d.\n", cardID);
		return -1;

	}

	AbstractDAQCard *card = cards[cardID];
	return card->getPortCounts(port, whichCount);
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
	return -1;
}
int DAQFrameServer::setCoincidenceTrigger(CoincidenceTriggerConfig *config)
{
	return -1;
}

int AbstractDAQCard::setGateEnable(unsigned mode)
{
	return -1;
}

int DAQFrameServer::setGateEnable(unsigned mode)
{
	return -1;
}
