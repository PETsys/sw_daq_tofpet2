#include "ProcessHit.hpp"
#include <math.h>
using namespace PETSYS;

ProcessHit::ProcessHit(SystemConfig *systemConfig, bool qdcMode, EventSink<Hit> *sink) :
OverlappedEventHandler<RawHit, Hit>(sink), systemConfig(systemConfig), qdcMode(qdcMode)
{
	nReceived = 0;
	nReceivedInvalid = 0;
	nTDCCalibrationMissing = 0;
	nQDCCalibrationMissing = 0;
	nXYZMissing = 0;
	nSent = 0;
}

EventBuffer<Hit> * ProcessHit::handleEvents (EventBuffer<RawHit> *inBuffer)
{
	// TODO Add instrumentation
	unsigned N =  inBuffer->getSize();
	EventBuffer<Hit> * outBuffer = new EventBuffer<Hit>(N, inBuffer);
	
	bool useTDC = systemConfig->useTDCCalibration();
	bool useQDC = systemConfig->useQDCCalibration();
	bool useXYZ = systemConfig->useXYZ();
	
	uint32_t lReceived = 0;
	uint32_t lReceivedInvalid = 0;
	uint32_t lTDCCalibrationMissing = 0;
	uint32_t lQDCCalibrationMissing = 0;
	uint32_t lXYZMissing = 0;
	uint32_t lSent = 0;
	
	for(int i = 0; i < N; i++) {
		RawHit &in = inBuffer->get(i);
		Hit &out = outBuffer->getWriteSlot();
		out.raw = &in;
		
		uint8_t eventFlags = in.valid ? 0x0 : 0x1;
		
		SystemConfig::ChannelConfig &cc = systemConfig->getChannelConfig(in.channelID);
		SystemConfig::TacConfig &ct = cc.tac_T[in.tacID];
		SystemConfig::TacConfig &ce = cc.tac_E[in.tacID];
		SystemConfig::QacConfig &cq = cc.qac_Q[in.tacID];
		
		out.time = in.time;
		if(useTDC) {
			float q_T = ( -ct.a1 + sqrtf((ct.a1 * ct.a1) - (4.0f * (ct.a0 - in.tfine) * ct.a2))) / (2.0f * ct.a2) ;
			out.time = in.time - q_T - ct.t0;
			if(ct.a1 == 0) eventFlags |= 0x2;
		}
		
		if(!qdcMode) {
			out.timeEnd = in.timeEnd;
			if(useQDC) {
				float q_E = ( -ce.a1 + sqrtf((ce.a1 * ce.a1) - (4.0f * (ce.a0 - in.efine) * ce.a2))) / (2.0f * ce.a2) ;
				out.timeEnd = in.timeEnd - q_E - ce.t0;
				if(ce.a1 == 0) eventFlags |= 0x2;
			}
			out.energy = out.timeEnd - out.time;
		}
		else {
			out.timeEnd = in.timeEnd;
			out.energy = in.efine;
			
			if(useQDC) {
				float ti = (out.timeEnd - out.time);
				float q0 = cq.p0 +
					cq.p1 * ti + 
					cq.p2 * ti * ti + 
					cq.p3 * ti * ti * ti + 
					cq.p4 * ti * ti * ti * ti;
					
				out.energy = in.efine - q0;
				if(cq.p1 == 0) eventFlags |= 0x4;
			}
		}
		
		out.region = -1;
		out.x = out.y = out.z = 0.0;
		out.xi = out.yi = 0;
		if(useXYZ) {
			out.region = cc.triggerRegion;
			out.x = cc.x;
			out.y = cc.y;
			out.z = cc.z;
			out.xi = cc.xi;
			out.yi = cc.yi;
			if(cc.triggerRegion == -1) eventFlags |= 0x8;
		}
		
		lReceived += 1;
		if((eventFlags & 0x1) != 0) lReceivedInvalid += 1;
		if((eventFlags & 0x2) != 0) lTDCCalibrationMissing += 1;
		if((eventFlags & 0x4) != 0) lQDCCalibrationMissing += 1;
		if((eventFlags & 0x8) != 0) lXYZMissing += 1;
		
		if(eventFlags == 0) {
			out.valid = true;
			outBuffer->pushWriteSlot();
			lSent += 1;
		}
	}
	
	atomicAdd(nReceived, lReceived);
	atomicAdd(nReceivedInvalid, lReceivedInvalid);
	atomicAdd(nTDCCalibrationMissing, lTDCCalibrationMissing);
	atomicAdd(nQDCCalibrationMissing, lQDCCalibrationMissing);
	atomicAdd(nXYZMissing, lXYZMissing);
	atomicAdd(nSent, lSent);
	
	return outBuffer;
}

void ProcessHit::report()
{
	fprintf(stderr, ">> ProcessHit report\n");
	fprintf(stderr, " hits received\n");
	fprintf(stderr, "  %10u total\n", nReceived);
	fprintf(stderr, "  %10u (%4.1f%%) invalid\n", nReceivedInvalid, 100.0 * nReceivedInvalid / nReceived);
	fprintf(stderr, " hits dropped\n");
	fprintf(stderr, "  %10u (%4.1f%%) missing TDC calibration\n", nTDCCalibrationMissing, 100.0 * nTDCCalibrationMissing / nReceived);
	fprintf(stderr, "  %10u (%4.1f%%) missing QDC calibration\n", nQDCCalibrationMissing, 100.0 * nQDCCalibrationMissing / nReceived);
	fprintf(stderr, "  %10u (%4.1f%%) missing XYZ information\n", nXYZMissing, 100.0 * nXYZMissing / nReceived);
	fprintf(stderr, " hits passed\n");
	fprintf(stderr, "  %10u (%4.1f%%)\n", nSent, 100.0 * nSent / nReceived);
	
	OverlappedEventHandler<RawHit, Hit>::report();
}
