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
	
	bool requireTDC = systemConfig->useTDCCalibration();
	bool requireQDC = systemConfig->useQDCCalibration();
	
	for(int i = 0; i < N; i++) {
		RawHit &in = inBuffer->get(i);
		Hit &out = outBuffer->getWriteSlot();
		
		out.raw = &in;
		bool valid = in.valid;
		
		SystemConfig::ChannelConfig &cc = systemConfig->getChannelConfig(in.channelID);
		SystemConfig::TacConfig &ct = cc.tac_T[in.tacID];
		SystemConfig::TacConfig &ce = cc.tac_E[in.tacID];
		SystemConfig::QacConfig &cq = cc.qac_Q[in.tacID];
			
		float q_T = ( -ct.a1 + sqrtf((ct.a1 * ct.a1) - (4.0f * (ct.a0 - in.tfine) * ct.a2))) / (2.0f * ct.a2) ;
		
		out.time = in.time - q_T - ct.t0;
		valid &= (ct.a1 != 0) || !requireTDC;
		
		if(!qdcMode) {
			float q_E = ( -ce.a1 + sqrtf((ce.a1 * ce.a1) - (4.0f * (ce.a0 - in.efine) * ce.a2))) / (2.0f * ce.a2) ;
			out.timeEnd = in.timeEnd - q_E - ce.t0;
			out.energy = out.timeEnd - out.time;
			valid &= (ce.a1 != 0) || !requireTDC;
		}
		else {
			out.timeEnd = in.timeEnd;
			float ti = (out.timeEnd - out.time);
			float q0 = cq.p0 +
				cq.p1 * ti + 
				cq.p2 * ti * ti + 
				cq.p3 * ti * ti * ti + 
				cq.p4 * ti * ti * ti * ti;
				
			out.energy = in.efine - q0;
			valid &= (cq.p1 != 0) || !requireQDC;
		}
		
		out.region = cc.triggerRegion;
		out.x = cc.x;
		out.y = cc.y;
		out.z = cc.z;
		out.xi = cc.xi;
		out.yi = cc.yi;
		
		out.valid = valid;
		
		outBuffer->pushWriteSlot();
	}
	
	return outBuffer;
}

// TODO Add report
