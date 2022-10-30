#ifndef __PETSYS__PROTOCOL_HPP__DEFINED__
#define __PETSYS__PROTOCOL_HPP__DEFINED__

#include <stdint.h>

namespace PETSYS {

static const uint16_t commandAcqOnOff = 0x01;
static const uint16_t commandGetDataFrameSharedMemoryName = 0x02;
static const uint16_t commandGetDataFrameWriteReadPointer = 0x03;
static const uint16_t commandSetDataFrameReadPointer = 0x04;
static const uint16_t commandToFrontEnd = 0x05;
static const uint16_t commandGetPortUp = 0x06;
static const uint16_t commandGetPortCounts = 0x07;
static const uint16_t commandQuit = 0x08;
static const uint16_t commandSetSorter = 0x09;
static const uint16_t commandSetTrigger = 0x10;
static const uint16_t commandSetIdleTimeCalculation = 0x11;
static const uint16_t commandSetGateEnable = 0x12;
static const uint16_t commandSetMinimumFrameID = 0x13;

}
#endif
