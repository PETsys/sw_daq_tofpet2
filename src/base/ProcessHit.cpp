#include "ProcessHit.hpp"
#include <math.h>
using namespace PETSYS;

ProcessHit::ProcessHit(SystemConfig *systemConfig, bool qdcMode, EventSink<Hit> *sink) :
OverlappedEventHandler<RawHit, Hit>(sink), systemConfig(systemConfig), qdcMode(qdcMode)
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
		SystemConfig::TacConfig &ct = cc.tac_T[in.tacID];
		SystemConfig::TacConfig &ce = cc.tac_E[in.tacID];
		SystemConfig::QacConfig &cq = cc.qac_Q[in.tacID];
		
		float q_T = +( 2 * ct.p2 * ct.tB + sqrtf(4 * in.tfine * ct.p2 + ct.m*ct.m) - ct.m)/(2 * ct.p2);
		out.time = in.time - q_T;
		if(!qdcMode) {
			float q_E = +( 2 * ce.p2 * ce.tB + sqrtf(4 * in.efine * ce.p2 + ce.m*ce.m) - ce.m)/(2 * ce.p2);
			out.timeEnd = in.timeEnd - q_E;
			out.energy = out.timeEnd - out.time;
		}
		else {
			out.timeEnd = in.timeEnd;
			float ti = (out.timeEnd - in.time);
			float q0 = cq.p0 +
				cq.p1 * ti + 
				cq.p2 * ti * ti + 
				cq.p3 * ti * ti * ti + 
				cq.p4 * ti * ti * ti * ti;
				
			out.energy = in.efine - q0;
		}
		
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