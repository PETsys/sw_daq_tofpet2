#include "ProcessHit.hpp"
#include <math.h>
using namespace PETSYS;

ProcessHit::ProcessHit(SystemConfig *systemConfig, EventSink<Hit> *sink) :
OverlappedEventHandler<RawHit, Hit>(sink), systemConfig(systemConfig)
{
}

EventBuffer<Hit> * ProcessHit::handleEvents (EventBuffer<RawHit> *inBuffer)
{
	// TODO Add instrumentation
	unsigned N =  inBuffer->getSize();
	EventBuffer<Hit> * outBuffer = new EventBuffer<Hit>(N, inBuffer);
	
	for(int i = 0; i < N; i++) {
		RawHit &in = inBuffer->get(i);
		Hit &out = outBuffer->getWriteSlot();
		
		out.raw = &in;
		
		SystemConfig::ChannelConfig &cc = systemConfig->getChannelConfig(in.channelID);
		SystemConfig::TacConfig &tc_T = cc.tac_T[in.tacID];
		SystemConfig::TacConfig &tc_E = cc.tac_E[in.tacID];
		
		float q_T = +( 2 * tc_T.p2 * tc_T.tB + sqrtf(4 * in.tfine * tc_T.p2 + tc_T.m*tc_T.m) - tc_T.m)/(2 * tc_T.p2);
		float q_E = +( 2 * tc_E.p2 * tc_E.tB + sqrtf(4 * in.efine * tc_E.p2 + tc_E.m*tc_E.m) - tc_E.m)/(2 * tc_E.p2);
		
		
		out.time = in.time - q_T;
		out.timeEnd = in.timeEnd - q_E;
		out.energy = out.timeEnd - out.time;
		
		// TODO Use channel map table
		out.region = in.channelID / 128;
		out.x = out.y = out.z = 0;
		out.xi = out.yi = 0;
		
		out.valid = true;
		
		outBuffer->pushWriteSlot();
	}
	
	return outBuffer;
}

// TODO Add report