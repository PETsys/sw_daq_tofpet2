#ifndef __DAQFRAMESERVER_HPP__DEFINED__
#define __DAQFRAMESERVER_HPP__DEFINED__

#include "FrameServer.hpp"

#include "boost/tuple/tuple.hpp"
#include <boost/unordered_map.hpp> 
#include "boost/date_time/posix_time/posix_time.hpp"

namespace DAQd {
	
class AbstractDAQCard {
public:
	AbstractDAQCard();
	virtual ~AbstractDAQCard();

	virtual int getWords(uint64_t *buffer, int count) = 0;
	virtual int sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength) = 0;
	virtual int recvReply(char *buffer, int bufferSize) = 0;
	virtual int setAcquistionOnOff(bool enable) = 0;
	virtual uint64_t getPortUp() = 0;
	virtual uint64_t getPortCounts(int channel, int whichCount) = 0;
	virtual int setSorter(unsigned mode);
	virtual int setCoincidenceTrigger(CoincidenceTriggerConfig *config);
	virtual int setGateEnable(unsigned mode);
};

class DAQFrameServer : public FrameServer
{
public:
	DAQFrameServer(AbstractDAQCard *card, int nFEB, int *feTypeMap, int debugLevel);
	virtual ~DAQFrameServer();	


	int sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength);
	
	virtual void startAcquisition(int mode);
	virtual void stopAcquisition();
	virtual uint64_t getPortUp();
	virtual uint64_t getPortCounts(int port, int whichCount);
	virtual int setSorter(unsigned mode);
	virtual int setCoincidenceTrigger(CoincidenceTriggerConfig *config);
	virtual int setGateEnable(unsigned mode);

private:
	AbstractDAQCard *DP;
	
protected:

	void * doWork();
	
};

}
#endif
