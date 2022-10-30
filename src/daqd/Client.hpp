#ifndef __PETSYS__CLIENT_HPP__DEFINED__
#define __PETSYS__CLIENT_HPP__DEFINED__

#include "FrameServer.hpp"
#include <map>

namespace PETSYS {
	
class Client {
public:
	Client (int socket, FrameServer *frameServer);
	~Client();
	
	int handleRequest();
	
private:
	int socket;
	unsigned char socketBuffer[16*1024];
	FrameServer *frameServer;
	
	int doAcqOnOff();
	int doGetDataFrameSharedMemoryName();
	int doGetDataFrameWriteReadPointer();
	int doSetDataFrameReadPointer();
	int doCommandToFrontEnd(int commandLength);
	int doGetPortUp();
	int doGetPortCounts();
	int doSetSorter();
	int doSetTrigger();
	int doSetIdleTimeCalculation();
	int doSetGateEnable();
	int doSetMinimumFrameID();
};

}
#endif
