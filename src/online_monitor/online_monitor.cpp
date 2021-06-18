#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <set>
#include <map>
#include <shm_raw.hpp>
#include <boost/lexical_cast.hpp>
#include "Monitor.hpp"
#include "SingleValue.hpp"
#include "Histogram1D.hpp"
#include <Event.hpp>
#include <SystemConfig.hpp>
#include <OrderedEventHandler.hpp>
#include <CoarseSorter.hpp>
#include <ProcessHit.hpp>
#include <SimpleGrouper.hpp>
#include <CoincidenceGrouper.hpp>
#include <ThreadPool.hpp>
#include <boost/regex.hpp>
#include <string>
#include <iostream>

using namespace std;
using namespace PETSYS;
using namespace PETSYS::OnlineMonitor;


class MyEventStream : public EventStream {
public:
	MyEventStream(double f, int tID) : frequency(f), triggerID(tID) { } ;
	double getFrequency() { return frequency; };
	int getTriggerID() { return triggerID; };
private:
	double frequency;
	int triggerID;
};


/* 
 * Duplicate code from RawReader
 * Needs reforming, probably once we add support for QDC/ToT mixed mode acquisitions
 */

struct UndecodedHit {
		u_int64_t frameID;
		u_int64_t eventWord;
	};

class Decoder : public UnorderedEventHandler<UndecodedHit, RawHit> {
	public:
		Decoder(bool mod, EventSink<RawHit> *sink) : UnorderedEventHandler<UndecodedHit, RawHit>(sink), totMode(mod)
		{
		};
		
		void report()
		{
			UnorderedEventHandler<UndecodedHit,RawHit>::report();
		};
		
	protected:
		virtual EventBuffer<RawHit> * handleEvents (EventBuffer<UndecodedHit> *inBuffer)
		{
		unsigned N =  inBuffer->getSize();
		EventBuffer<RawHit> *outBuffer = new EventBuffer<RawHit>(N, inBuffer);

		UndecodedHit *pi = inBuffer->getPtr();
		UndecodedHit *pe = pi + N;
		RawHit *po = outBuffer->getPtr();
		for(; pi < pe; pi++, po++) {
			RawEventWord e = RawEventWord(pi->eventWord);
			po->channelID = e.getChannelID();
			po->qdcMode = !totMode;
			po->tacID = e.getTacID();
			po->frameID = pi->frameID;
			po->tcoarse = e.getTCoarse();
			po->tfine = e.getTFine();
			po->ecoarse = e.getECoarse();
			po->efine = e.getEFine();

			po->time = pi->frameID * 1024 + po->tcoarse;
			po->timeEnd = pi->frameID * 1024 + po->ecoarse;
			if((po->timeEnd - po->time) < -256) po->timeEnd += 1024;
			po->valid = true;
	}
	outBuffer->setUsed(N);
	return outBuffer;			
		}
	private:
		bool totMode;
	};

class Filler  : public OrderedEventHandler<Coincidence, Coincidence> {
private:
	Monitor *monitor;
	SingleValue *acquisition_t0 ;
	SingleValue *acquisition_elapsed;
	SingleValue *coincidence_counter;
	map<int, SingleValue *> channel_counter;
	map<int, Histogram1D *> channel_energy;

public:
	Filler(char * tocFileName, Monitor *m, EventSink<Coincidence> *sink) :
		OrderedEventHandler<Coincidence, Coincidence>(sink), 
		monitor(m),
		acquisition_t0(NULL),
		acquisition_elapsed(NULL),
		coincidence_counter(NULL),
		channel_counter(),
		channel_energy()
	{
		FILE *tocFile = fopen(tocFileName, "r");
		char l[256];
		while(fscanf(tocFile, "%[^\n]\n", l) == 1) {
			boost::cmatch what;
			if(boost::regex_match(l, what, boost::regex {"(\\d+)\\s+SINGLEVALUE\\s+acquisition_t0"})) {
				size_t offset = boost::lexical_cast<size_t>(what[1]);
				acquisition_t0 = new SingleValue("acquisition_t0", m, m->getPtr() + offset);
			}
			else if(boost::regex_match(l, what, boost::regex {"(\\d+)\\s+SINGLEVALUE\\s+acquisition_elapsed"})) {
				size_t offset = boost::lexical_cast<size_t>(what[1]);
				acquisition_elapsed = new SingleValue("acquisition_elapsed", m, m->getPtr() + offset);
			}
			else if(boost::regex_match(l, what, boost::regex {"(\\d+)\\s+SINGLEVALUE\\s+coincidence_counter"})) {
				size_t offset = boost::lexical_cast<size_t>(what[1]);
				coincidence_counter = new SingleValue("coincidence_counter", m, m->getPtr() + offset);
			}
			else if(boost::regex_match(l, what, boost::regex {"(\\d+)\\s+SINGLEVALUE\\s+channel_counter/(\\d+)"})) {
				size_t offset = boost::lexical_cast<size_t>(what[1]);
				int channelID = boost::lexical_cast<int>(what[2]);
				channel_counter[channelID] = new SingleValue("channel_counter", m, m->getPtr() + offset);
			}
			else if(boost::regex_match(l, what, boost::regex {"(\\d+)\\s+HISTOGRAM1D\\s+channel_energy/(\\d+)\\s+(\\d+)\\s+(\\S+)\\s+(\\S+)"})) {
				size_t offset = boost::lexical_cast<size_t>(what[1]);
				int channelID = boost::lexical_cast<int>(what[2]);
				int nBins = boost::lexical_cast<int>(what[3]);
				double lowerX = boost::lexical_cast<double>(what[4]);
				double upperX = boost::lexical_cast<double>(what[5]);
				channel_energy[channelID] = new Histogram1D(nBins, lowerX, upperX, "channel_energy", m, m->getPtr() + offset);
			}
			
			else if(boost::regex_match(l, what, boost::regex {"(\\d+)\\s+END"})) {
			}
			else {
				fprintf(stderr, "UNMATCHED: '%s'\n", l);
			}
		}		
	};
	
	EventBuffer<Coincidence> * handleEvents(EventBuffer<Coincidence> *buffer) {
		
		monitor->lock();
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			Coincidence &e = buffer->get(i);
			
			for(int j = 0; j < e.nPhotons; j++) {
				GammaPhoton *group = e.photons[j];
				
				for(int k = 0; k < group->nHits; k++) {
					Hit *hit = group->hits[k];
					
					int channel = hit->raw->channelID;
					
					auto hcounter = channel_counter.find(channel);
					if(hcounter != channel_counter.end()) hcounter->second->addToValue(1);
					
					auto henergy = channel_energy.find(channel);
					if(henergy != channel_energy.end()) henergy->second->fill(hit->energy, 1);
					
				}
			}
			
			
		}
 		if(acquisition_elapsed != NULL) acquisition_elapsed->setValue(buffer->getTMax());
 		if(coincidence_counter != NULL) coincidence_counter->addToValue(N);
		monitor->unlock();
		
		return buffer;
	};
	
	
	void pushT0(double t0) {
		if(acquisition_t0 != NULL) acquisition_t0->setValue(t0);
		
	}
};


struct BlockHeader  {
	float step1;
	float step2;	
	uint32_t wrPointer;
	uint32_t rdPointer;
	int32_t endOfStep;
};


int main(int argc, char *argv[])
{
	assert(argc == 8);
	long systemFrequency = boost::lexical_cast<long>(argv[1]);
	// "tot", "qdc
	char *mode = argv[2];
	char *configFileName = argv[3];
	char *shmObjectPath = argv[4];
	char *tocFileName = argv[5];
	int triggerID = boost::lexical_cast<int>(argv[6]);
	double acquisitionStartTime = boost::lexical_cast<double>(argv[7]);
	
	bool totMode = (strcmp(mode, "tot") == 0);
	
	// If data was taken in full ToT mode, do not attempt to load these files
	unsigned long long mask = SystemConfig::LOAD_ALL;	
	if(totMode){ 
		mask ^= (SystemConfig::LOAD_QDC_CALIBRATION | SystemConfig::LOAD_ENERGY_CALIBRATION);
	}
	SystemConfig *config = SystemConfig::fromFile(configFileName, mask);
	
	
	PETSYS::SHM_RAW *shm = new PETSYS::SHM_RAW(shmObjectPath);
	bool firstBlock = true;
	float step1;
	float step2;
	BlockHeader blockHeader;
	
	long long minFrameID = 0x7FFFFFFFFFFFFFFFLL, maxFrameID = 0, lastMaxFrameID = 0;
	
	long long lastFrameID = -1;
	long long stepFirstFrameID = -1;


	auto pool = new ThreadPool<UndecodedHit>();
	auto eventStream = new MyEventStream(systemFrequency, triggerID);
	auto monitor = new Monitor(false);
	
	auto pipeline = new Decoder(totMode, 
			new CoarseSorter(
			new ProcessHit(config, eventStream,
			new SimpleGrouper(config,
			new CoincidenceGrouper(config,
			new Filler(tocFileName, monitor, 
			new NullSink<Coincidence>()
		))))));
	
	monitor->resetAllObjects();
	pipeline->pushT0(acquisitionStartTime);
	
	EventBuffer<UndecodedHit> *outBuffer = NULL; 
	size_t seqN = 0;
	long long currentBufferFirstFrame = 0;	
	
	while(fread(&blockHeader, sizeof(blockHeader), 1, stdin) == 1) {

		step1 = blockHeader.step1;
		step2 = blockHeader.step2;
		
		unsigned bs = shm->getSizeInFrames();
		unsigned rdPointer = blockHeader.rdPointer % (2*bs);
		unsigned wrPointer = blockHeader.wrPointer % (2*bs);
		while(rdPointer != wrPointer) {
			unsigned index = rdPointer % bs;
			
			long long frameID = shm->getFrameID(index);
			if(stepFirstFrameID == -1) stepFirstFrameID = frameID;
			if(frameID <= lastFrameID) {
				fprintf(stderr, "WARNING!! Frame ID reversal: %12lld -> %12lld | %04u %04u %04u\n", 
					lastFrameID, frameID, 
					blockHeader.wrPointer, blockHeader.rdPointer, rdPointer
					);
				
			}

			lastFrameID = frameID;
			minFrameID = minFrameID < frameID ? minFrameID : frameID;
			maxFrameID = maxFrameID > frameID ? maxFrameID : frameID;

			// Get the pointer to the raw data frame
			PETSYS::RawDataFrame *dataFrame = shm->getRawDataFrame(index);
			// Increase the circular buffer pointer
			rdPointer = (rdPointer+1) % (2*bs);
			
			int frameSize = shm->getFrameSize(index);
			int N = shm->getNEvents(index);
			bool frameLost = shm->getFrameLost(index);
			
			if(frameLost)
				continue;
			
			// Blocksize
			// Best block size from profiling: 2048
			// but handle larger frames correctly
			size_t allocSize = max(N, 2048);
			if(outBuffer == NULL) {
				currentBufferFirstFrame = dataFrame->getFrameID();
				outBuffer = new EventBuffer<UndecodedHit>(allocSize, seqN, currentBufferFirstFrame * 1024);
				seqN += 1;
			}
			else if((outBuffer->getFree() < N) || ((frameID - currentBufferFirstFrame) > (1LL << 32))) {
				// Buffer is full or buffer is covering too much time
				pool->queueTask(outBuffer, pipeline);
				currentBufferFirstFrame = dataFrame->getFrameID();
				outBuffer = new EventBuffer<UndecodedHit>(allocSize, seqN, currentBufferFirstFrame * 1024);
				seqN += 1;
			}			
			
			UndecodedHit *p = outBuffer->getPtr() + outBuffer->getUsed();
			for(int i = 0; i < N; i++) {
				p[i].frameID = frameID - currentBufferFirstFrame;
				p[i].eventWord = dataFrame->data[2+i];
			}
			outBuffer->setUsed(outBuffer->getUsed() + N);
			outBuffer->setTMax((frameID + 1) * 1024);
	        
		}		
		
		if(outBuffer != NULL) {
			pool->queueTask(outBuffer, pipeline);
			outBuffer = NULL;
		}
		pool->completeQueue();
		
		if(blockHeader.endOfStep != 0) {
		}
		
		fwrite(&rdPointer, sizeof(uint32_t), 1, stdout);
		fflush(stdout);
	}

	
	delete pool;		
	
	
	return 0;
}

