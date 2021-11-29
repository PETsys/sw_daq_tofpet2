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
	cfData = new uint64_t [bs * nCards];
	lastFrameWasBad = true;
}
	

DAQFrameServer::~DAQFrameServer()
{
	stopAcquisition();
	delete [] cfData;
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
	
	const int MAX_PACKET_WORDS = 8;
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

bool DAQFrameServer::getFrame(AbstractDAQCard *card, uint64_t *dst)
{
	while(!die) {
		int nWords;
		// If we are comming back from a bad frame, let's dump until we read an idle
		if(lastFrameWasBad) {
			if(!card->lookForWords(IDLE_WORD, true)) continue;
		}

		lastFrameWasBad = true;
		// Look for a non-filler
		if(!card->lookForWords(IDLE_WORD, false)) continue;

		// Read 3 words which should be a HEADER_WORD and the two first words of a frame
		nWords = card->getWords(dst, 3);
		if (nWords != 3) continue;
		if(dst[0] != HEADER_WORD) continue;

		uint64_t frameSource = (dst[1] >> 54) & 0x400;
		uint64_t frameType = (dst[1] >> 51) & 0x7;
		uint64_t frameSize = (dst[1] >> 36) & 0x7FFF;
		uint64_t frameID = (dst[1] ) & 0xFFFFFFFFF;
		uint64_t nEvents = dst[2] & 0xFFFF;
		bool frameLost = (dst[2] & 0x10000) != 0;


		if((frameType != 0x1) || (frameSource != 0)) {
			fprintf(stderr, "Bad frame header: %04lx %04lx\n", frameType, frameSource);
			continue;
		}

		if(frameSize > MaxRawDataFrameSize) {
			fprintf(stderr, "Excessive frame size: %lu\n word (max is %u)", frameSize, MaxRawDataFrameSize);
			continue;
		}

		if(frameSize != (nEvents+2)) {
			fprintf(stderr, "Inconsistent frame size: %lu\n words, %lu events\n", frameSize, nEvents);
			continue;
		}

		// Read the rest of the expected event words plus the TRAILER_WORD
		nWords = card->getWords(dst+3, nEvents + 2);
		if(nWords != (nEvents + 2)) continue;
		if(dst[frameSize+1] != TRAILER_WORD) continue;
		if(dst[frameSize+2] != IDLE_WORD) continue;

		// If we got here, we got a good data frame
		lastFrameWasBad = false;
		return true;

	}
	// If we got here, we didn't produce a good data frame
	return false;

}


void *DAQFrameServer::doWork()
{
	int nCards = cards.size();

	bool cfValid[nCards];

	// Initial fill with frame data
	size_t bs = MaxRawDataFrameSize + 4;
	for(int i = 0; i < nCards; i++) {
		cfValid[i] = getFrame(cards[i], cfData + bs * i);
	}
	if(die) return NULL;

	long long expectedFrameID = 1LL<<62;
	for(int i = 0; i < nCards; i++) {
		uint64_t *words = cfData + bs*i + 1;
                long long frameID = words[0] & 0xFFFFFFFFF;
		if(frameID < expectedFrameID) expectedFrameID = frameID;
	}

	// Read frames from various cards until they're all at the expected frameID or ahead
	for(int i = 0; i < nCards; i++) {
		uint64_t *words = cfData + bs*i + 1;
		long long frameID = words[0] & 0xFFFFFFFFF;
		while(frameID < expectedFrameID) {
			cfValid[i] = getFrame(cards[i], cfData + i);
			if(die) return NULL;
			frameID = words[0] & 0xFFFFFFFFF;
		}
        }

	while(!die) {
		// Fill out the cards' buffer for any consumed frames
		for(int i = 0; i < nCards; i++) {
			if(!cfValid[i]) {
				cfValid[i] = getFrame(cards[i], cfData + bs*i);
				if(die) return NULL;
			}
		}

		// WARNING The following is a failsafe which should never be needed
		// Consider removing this code
		// Read frames from various cards until they're all at the expected frameID or ahead
		for(int i = 0; i < nCards; i++) {
			uint64_t *words = cfData + bs*i + 1;
			long long frameID = words[0] & 0xFFFFFFFFF;

			while(frameID < expectedFrameID) {
				cfValid[i] = getFrame(cards[i], cfData + bs*i);
				if(die) return NULL;
				frameID = words[0] & 0xFFFFFFFFF;
			}

		}

		// Check if output frame will be a lost frame
		bool frameLost = false;
		for(int i = 0; i < nCards; i++) {
			uint64_t *words = cfData + bs*i + 1;
	                long long frameID = words[0] & 0xFFFFFFFFF;


			if(frameID != expectedFrameID) {
				frameLost = true;
				continue;
			}

			frameLost |= (words[1] & 0x10000) != 0;
		}

		RawDataFrame *dst = NULL;

		// Store data if we are in acquisition mode and
		// - Frame is not lost
		// - Frame is lost but frameID is a multiple of 128 (forward at least 1% so the process does not freeze)
		if((acquisitionMode != 0) && (!frameLost || ((expectedFrameID % 128) == 0))) {
			pthread_mutex_lock(&lock);
			if(!isFull(dataFrameWritePointer, dataFrameReadPointer)) {
				dst = &shmPtr[dataFrameWritePointer % MaxRawDataFrameQueueSize];
			}
			pthread_mutex_unlock(&lock);
		}

		if(dst != NULL) {
			uint64_t *dst_data = dst->data;
			if(frameLost) {
				// This frame is lost for some reason so just assemble an empty frame
				// with the correct frameID and the lostFrame flat set
				dst_data[0] = (2LL << 36) | expectedFrameID;
				dst_data[1] = 1 << 16;
			}
			else {
				uint64_t nEvents = 0;

				uint64_t *cfNextPtr[nCards];
				uint64_t *cfEndPtr[nCards];
				unsigned short cfNextTCoarse[nCards];

				for(int i = 0; i < nCards; i++) {
					uint64_t *words = cfData + bs*i + 1;
					cfNextPtr[i] = words + 2;
					cfEndPtr[i] = cfNextPtr[i] + (words[1] & 0xFFFF);

					cfNextTCoarse[i] = (cfNextPtr[i] != cfEndPtr[i]) ? ((*cfNextPtr[i]) >> 30 % 1024) : 0xFFFF;
				}

				while(true) {
					// Pick the source with the oldest event (lowest time tag)
					int selCard = -1;
					unsigned short selTCoarse = 0xFFFF;
					for(int i = 0; i < nCards; i++) {
						if(cfNextPtr[i] != cfEndPtr[i])
							if(cfNextTCoarse[i] <= selTCoarse) {
								selTCoarse = cfNextTCoarse[i];
								selCard = i;
							}
					}

					if(selCard == -1) {
						// Nothing was selected which means all sources were exausted
						break;
					}

					uint64_t cardID_shifted = (uint64_t(selCard) << (63 - daqCardPortBits));
					uint64_t eventWord = (*cfNextPtr[selCard]);
					dst_data[nEvents + 2] = cardID_shifted | eventWord;

					// Update output event count
					if(nEvents  < (MaxRawDataFrameSize-2)) nEvents += 1;

					// Update the source
					cfNextPtr[selCard] += 1;
					cfNextTCoarse[selCard] = (cfNextPtr[selCard] != cfEndPtr[selCard]) ? ((*cfNextPtr[selCard]) >> 30 % 1024) : 0xFFFF;
				}

				// Write the frame headers
				dst_data[0] = ((nEvents+2) << 36) | expectedFrameID;
				dst_data[1] = nEvents;
			}

			// Update the shared memory pointers to signal a new frame has been written
			pthread_mutex_lock(&lock);
			dataFrameWritePointer = (dataFrameWritePointer + 1)  % (2*MaxRawDataFrameQueueSize);
			pthread_mutex_unlock(&lock);
		}

		for(int i = 0; i < nCards; i++) {
			// Mark all sources with the correct frameID as invalid since they were consumed
			uint64_t *words = cfData + bs*i + 1;
			long long frameID = words[0] & 0xFFFFFFFFF;
			if(frameID == expectedFrameID) {
				cfValid[i] = false;
			}
		}

		expectedFrameID += 1;



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
