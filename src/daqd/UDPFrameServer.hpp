#ifndef __PETSYS__UDPFRAMESERVER_HPP__DEFINED__
#define __PETSYS__UDPFRAMESERVER_HPP__DEFINED__

#include "FrameServer.hpp"

#include "boost/tuple/tuple.hpp"
#include <boost/unordered_map.hpp> 
#include "boost/date_time/posix_time/posix_time.hpp"

namespace PETSYS {

class UDPFrameServer : public FrameServer
{
protected:
	UDPFrameServer(int udpSocket, const char * shmName, int shmfd, RawDataFrame * shmPtr, int debugLevel);
public:
	
	static UDPFrameServer * createFrameServer(const char * shmName, int shmfd, RawDataFrame * shmPtr, int debugLevel);
	virtual ~UDPFrameServer();	

	int sendCommand(int portID, int slaveID, char *buffer, int bufferSize, int commandLength);
	uint64_t getPortUp();
	virtual uint64_t getPortCounts(int port, int whichCount);
	
private:
	
	int udpSocket;

protected:
	void * doWork();
	
};

}
#endif
