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
#include <boost/python.hpp>
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
#include <TFile.h>
#include <TTree.h>

using namespace std;
using namespace PETSYS;
using namespace PETSYS::OnlineMonitor;


static const unsigned MAX_NUMBER_CHANNELS = 4194304;

enum FILE_TYPE { FILE_TEXT, FILE_BINARY, FILE_ROOT };

enum EVENT_TYPE { SINGLE, GROUP, COINCIDENCE };

enum FrameType { FRAME_TYPE_UNKNOWN, FRAME_TYPE_SOME_DATA, FRAME_TYPE_ZERO_DATA, FRAME_TYPE_SOME_LOST, FRAME_TYPE_ALL_LOST };

static void normalizeLine(char *line) {
	std::string s = std::string(line);
	// Remove carriage return, from Windows written files
	s = boost::regex_replace(s, boost::regex("\r"), "");
	// Remove comments
	s = boost::regex_replace(s, boost::regex("\\s*#.*"), "");
	// Remove leading white space
	s = boost::regex_replace(s, boost::regex("^\\s+"), "");
	// Remove trailing whitespace
	s = boost::regex_replace(s, boost::regex("\\s+$"), "");
	// Normalize white space to tab
	s = boost::regex_replace(s, boost::regex("\\s+"), "\t");
	strcpy(line, s.c_str());	
}

class OnlineEventStream : public EventStream {
public:
	OnlineEventStream(double f, int tID) : frequency(f), triggerID(tID) { } ;
	double getFrequency() { return frequency; };
	int getTriggerID() { return triggerID; };
	bool isQDC(unsigned int gChannelID){ return qdcMode[gChannelID]; };
	void setMode(unsigned int gChannelID, bool mode){ qdcMode[gChannelID] = mode; };
private:
	double frequency;
	int triggerID;
	bool qdcMode[MAX_NUMBER_CHANNELS];
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
	Decoder(OnlineEventStream *stream, EventSink<RawHit> *sink) : UnorderedEventHandler<UndecodedHit, RawHit>(sink), stream(stream)
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
			po->qdcMode = stream->isQDC(po->channelID);
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
	OnlineEventStream *stream;
};


class DataFileWriter {
private:
	float step1;
	float step2;
	double frequency;
	FILE_TYPE fileType;
	EVENT_TYPE eventType;
	int eventFractionToWrite;
	long long eventCounter;
	int hitLimitToWrite;

	FILE *dataFile;
	FILE *indexFile;
	off_t stepBegin;
	
	TTree *hData;
	TTree *hIndex;
	TFile *hFile;
	// ROOT Tree fields
	float		brStep1;
	float		brStep2;
	long long 	brStepBegin;
	long long 	brStepEnd;


	unsigned short	brN;
	unsigned short	brJ;
	long long	brTime;
	long long	brTimeDelta;
	unsigned int	brChannelID;
	float		brToT;
	float		brEnergy;
	unsigned short	brTacID;
	int		brXi;
	int		brYi;
	float		brX;
	float 		brY;
	float 		brZ;
	float		brTQT;
	float		brTQE;
	
	unsigned short	br1N, 		br2N;
	unsigned short	br1J,		br2J;
	long long	br1Time,	br2Time;
	unsigned int	br1ChannelID,	br2ChannelID;
	float		br1ToT,		br2ToT;
	float		br1Energy, 	br2Energy;
	unsigned short	br1TacID,	br2TacID;
	int		br1Xi,		br2Xi;
	int		br1Yi,		br2Yi;
	float		br1X,		br2X;
	float 		br1Y,		br2Y;
	float 		br1Z,		br2Z;


	struct SingleEvent {
		long long time;
		float e;
		int id;  
	} __attribute__((__packed__));


	struct GroupEvent {
		uint8_t mh_n; 
		uint8_t mh_j;
		long long time;
		float e;
		int id;  
	} __attribute__((__packed__));

	struct CoincidenceEvent {
		uint8_t mh_n1; 
		uint8_t mh_j1;
		long long time1;
		float e1;
		int id1;
	
		uint8_t mh_n2; 
		uint8_t mh_j2;
		long long time2;
		float e2;
		int id2;
	} __attribute__((__packed__));

	
public:
	DataFileWriter(char *fName, double frequency, EVENT_TYPE eventType, FILE_TYPE fileType, int hitLimitToWrite, int eventFractionToWrite) {
		this->frequency = frequency;
		this->fileType = fileType;
		this->eventType = eventType;
		this->eventFractionToWrite = eventFractionToWrite;
		this->hitLimitToWrite = hitLimitToWrite;
		this->eventCounter = 0;
		step1 = 0;
		step2 = 0;
		stepBegin = 0;
		
		if (fileType == FILE_ROOT){
			hFile = new TFile(fName, "RECREATE");
			int bs = 512*1024;
			hData = new TTree("data", "Event List", 2);
			hData->Branch("step1", &brStep1, bs);
			hData->Branch("step2", &brStep2, bs);	
			if(eventType == SINGLE || eventType == GROUP){  
				
				hData->Branch("time", &brTime, bs);
				hData->Branch("channelID", &brChannelID, bs);
				hData->Branch("tot", &brToT, bs);
				hData->Branch("energy", &brEnergy, bs);
				hData->Branch("tacID", &brTacID, bs);
				hData->Branch("xi", &brXi, bs);
				hData->Branch("yi", &brYi, bs);
				hData->Branch("x", &brX, bs);
				hData->Branch("y", &brY, bs);
				hData->Branch("z", &brZ, bs);
				hData->Branch("tqT", &brTQT, bs);
				hData->Branch("tqE", &brTQE, bs);
			}
			if(eventType == GROUP){
				hData->Branch("timeDelta", &brTimeDelta, bs);
				hData->Branch("mh_n", &brN, bs);
				hData->Branch("mh_j", &brJ, bs);
			}
			if(eventType == COINCIDENCE){
				hData->Branch("mh_n1", &br1N, bs);
				hData->Branch("mh_j1", &br1J, bs);
				hData->Branch("tot1", &br1ToT, bs);
				hData->Branch("time1", &br1Time, bs);
				hData->Branch("channelID1", &br1ChannelID, bs);
				hData->Branch("energy1", &br1Energy, bs);
				hData->Branch("tacID1", &br1TacID, bs);
				hData->Branch("xi1", &br1Xi, bs);
				hData->Branch("yi1", &br1Yi, bs);
				hData->Branch("x1", &br1X, bs);
				hData->Branch("y1", &br1Y, bs);
				hData->Branch("z1", &br1Z, bs);
				hData->Branch("mh_n2", &br2N, bs);
				hData->Branch("mh_j2", &br2J, bs);
				hData->Branch("time2", &br2Time, bs);
				hData->Branch("channelID2", &br2ChannelID, bs);
				hData->Branch("tot2", &br2ToT, bs);
				hData->Branch("energy2", &br2Energy, bs);
				hData->Branch("tacID2", &br2TacID, bs);
				hData->Branch("xi2", &br2Xi, bs);
				hData->Branch("yi2", &br2Yi, bs);
				hData->Branch("x2", &br2X, bs);
				hData->Branch("y2", &br2Y, bs);
				hData->Branch("z2", &br2Z, bs);
			}	
			hIndex = new TTree("index", "Step Index", 2);
			hIndex->Branch("step1", &brStep1, bs);
			hIndex->Branch("step2", &brStep2, bs);
			hIndex->Branch("stepBegin", &brStepBegin, bs);
			hIndex->Branch("stepEnd", &brStepEnd, bs);
		}
		else if(fileType == FILE_BINARY) {
			char fName2[1024];
			sprintf(fName2, "%s.ldat", fName);
			dataFile = fopen(fName2, "w");
			sprintf(fName2, "%s.lidx", fName);
			indexFile = fopen(fName2, "w");
		}
		else {
			dataFile = fopen(fName, "w");
		}
	};
	
	~DataFileWriter() {
		if (fileType == FILE_ROOT){
			hFile->Write();
			hFile->Close();
		}
		else if(fileType == FILE_BINARY) {
			fclose(dataFile);
			fclose(indexFile);
		}
		else {
			fclose(dataFile);
		}
	}
	
	void setStep1(float step){
		step1 = step;
	}
	void setStep2(float step){
		step2 = step;
	}
	void closeStep() {
		if (fileType == FILE_ROOT){
			brStepBegin = stepBegin;
			brStepEnd = hData->GetEntries();
			brStep1 = step1;
			brStep2 = step2;
			hIndex->Fill();
			stepBegin = hData->GetEntries();
			hFile->Write();
		}
		else if(fileType == FILE_BINARY) {
			fprintf(indexFile, "%ld\t%ld\t%e\t%e\n", stepBegin, ftell(dataFile), step1, step2);
			stepBegin = ftell(dataFile);
		}
		else {
			// Do nothing
		}
	};
	
	void writeSingleEvents(EventBuffer<Hit> *buffer) {
		double Tps = 1E12/frequency;
		float Tns = Tps / 1000;
				
		long long tMin = buffer->getTMin() * (long long)Tps;
		
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			long long tmpCounter = eventCounter;
			eventCounter += 1;
			if((tmpCounter % 1024) >= eventFractionToWrite) continue;

			Hit &hit = buffer->get(i);
			if(!hit.valid) continue;

			float Eunit = hit.raw->qdcMode ? 1.0 : Tns;
			
			if(fileType == FILE_ROOT){
				brStep1 = step1;
				brStep2 = step2;
				brTime = ((long long)(hit.time * Tps)) + tMin;
				brChannelID = hit.raw->channelID;
				brToT = (hit.timeEnd - hit.time) * Tps;
				brEnergy = hit.energy * Eunit;
				brTacID = hit.raw->tacID;
				brTQT = hit.raw->time - hit.time;
				brTQE = (hit.raw->timeEnd - hit.timeEnd);
				brX = hit.x;
				brY = hit.y;
				brZ = hit.z;
				brXi = hit.xi;
				brYi = hit.yi;				
				hData->Fill();
			}
			else if(fileType == FILE_BINARY) {
				SingleEvent eo = {
					((long long)(hit.time * Tps)) + tMin,
					hit.energy * Eunit,
					(int)hit.raw->channelID
				};
				fwrite(&eo, sizeof(eo), 1, dataFile);
			}
			else {
				fprintf(dataFile, "%lld\t%f\t%d\n",
					((long long)(hit.time * Tps)) + tMin,
					hit.energy * Eunit,
					(int)hit.raw->channelID
					);
			     
			}
		}
		
	}

	void writeGroupEvents(EventBuffer<GammaPhoton> *buffer) {
		double Tps = 1E12/frequency;
		float Tns = Tps / 1000;
	
		long long tMin = buffer->getTMin() * (long long)Tps;
		
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			long long tmpCounter = eventCounter;
			eventCounter += 1;
			if((tmpCounter % 1024) >= eventFractionToWrite) continue;

			GammaPhoton &p = buffer->get(i);
			
			if(!p.valid) continue;
			Hit &h0 = *p.hits[0];
			int limit = (hitLimitToWrite < p.nHits) ? hitLimitToWrite : p.nHits;
			for(int m = 0; m < limit; m++) {
				Hit &h = *p.hits[m];
				float Eunit = h.raw->qdcMode ? 1.0 : Tns;
				if (fileType == FILE_ROOT){
					brStep1 = step1;
					brStep2 = step2;

					brN  = p.nHits;
					brJ = m;
					brTime = ((long long)(h.time * Tps)) + tMin;
					brTimeDelta = (long long)(h.time - h0.time) * Tps;
					brChannelID = h.raw->channelID;
					brToT = (h.timeEnd - h.time) * Tps;
					brEnergy = h.energy * Eunit;
					brTacID = h.raw->tacID;
					brX = h.x;
					brY = h.y;
					brZ = h.z;
					brXi = h.xi;
					brYi = h.yi;
					
					hData->Fill();
				}
				else if(fileType == FILE_BINARY) {
					GroupEvent eo = { 
						(uint8_t)p.nHits, (uint8_t)m,
						((long long)(h.time * Tps)) + tMin,
						h.energy * Eunit,
						(int)h.raw->channelID
					};
					fwrite(&eo, sizeof(eo), 1, dataFile);
				}
				else {
					fprintf(dataFile, "%d\t%d\t%lld\t%f\t%d\n",
						p.nHits, m,
						((long long)(h.time * Tps)) + tMin,
						h.energy * Eunit,
						h.raw->channelID
						);
				}
			}
		}
	}

	void writeCoincidenceEvents(EventBuffer<Coincidence> *buffer) {
		double Tps = 1E12/frequency;
		float Tns = Tps / 1000;	

		long long tMin = buffer->getTMin() * (long long)Tps;
		
		int N = buffer->getSize();
		for (int i = 0; i < N; i++) {
			long long tmpCounter = eventCounter;
			eventCounter += 1;
			if((tmpCounter % 1024) >= eventFractionToWrite) continue;

			Coincidence &e = buffer->get(i);
			if(!e.valid) continue;
			if(e.nPhotons != 2) continue;
			
			GammaPhoton &p1 = *e.photons[0];
			GammaPhoton &p2 = *e.photons[1];
			
			int limit1 = (hitLimitToWrite < p1.nHits) ? hitLimitToWrite : p1.nHits;
			int limit2 = (hitLimitToWrite < p2.nHits) ? hitLimitToWrite : p2.nHits;
			for(int m = 0; m < limit1; m++) for(int n = 0; n < limit2; n++) {
				if(m != 0 && n != 0) continue;
				Hit &h1 = *p1.hits[m];
				Hit &h2 = *p2.hits[n];
				
				float Eunit1 = h1.raw->qdcMode ? 1.0 : Tns;
				float Eunit2 = h2.raw->qdcMode ? 1.0 : Tns;

				if (fileType == FILE_ROOT){
					brStep1 = step1;
					brStep2 = step2;

					br1N  = p1.nHits;
					br1J = m;
					br1Time = ((long long)(h1.time * Tps)) + tMin;
					br1ChannelID = h1.raw->channelID;
					br1ToT = (h1.timeEnd - h1.time) * Tps;
					br1Energy = h1.energy * Eunit1;
					br1TacID = h1.raw->tacID;
					br1X = h1.x;
					br1Y = h1.y;
					br1Z = h1.z;
					br1Xi = h1.xi;
					br1Yi = h1.yi;
					
					br2N  = p2.nHits;
					br2J = n;
					br2Time = ((long long)(h2.time * Tps)) + tMin;
					br2ChannelID = h2.raw->channelID;
					br2ToT = (h2.timeEnd - h2.time) * Tps;
					br2Energy = h2.energy * Eunit2;
					br2TacID = h2.raw->tacID;
					br2X = h2.x;
					br2Y = h2.y;
					br2Z = h2.z;
					br2Xi = h2.xi;
					br2Yi = h2.yi;
					
					hData->Fill();
				}
				else if(fileType == FILE_BINARY) {
					CoincidenceEvent eo = { 
						(uint8_t)p1.nHits, (uint8_t)m,
						((long long)(h1.time * Tps)) + tMin,
						h1.energy * Eunit1,
						(int)h1.raw->channelID,
						
						
						(uint8_t)p2.nHits, (uint8_t)n,
						((long long)(h2.time * Tps)) + tMin,
						h2.energy * Eunit2,
						(int)h2.raw->channelID
						
					};
					fwrite(&eo, sizeof(eo), 1, dataFile);
				}
				else {
					fprintf(dataFile, "%d\t%d\t%lld\t%f\t%d\t%d\t%d\t%lld\t%f\t%d\n",
						p1.nHits, m,
						((long long)(h1.time * Tps)) + tMin,
						h1.energy * Eunit1,
						h1.raw->channelID,
						
						p2.nHits, n,
						((long long)(h2.time * Tps)) + tMin,
						h2.energy * Eunit2,
						h2.raw->channelID
					);
				}
			}
		}
		
	};
};

class WriteSinglesHelper : public OrderedEventHandler<Hit, Hit> {
private: 
	DataFileWriter *dataFileWriter;
public:
	WriteSinglesHelper(DataFileWriter *dataFileWriter, EventSink<Hit> *sink) :
		OrderedEventHandler<Hit, Hit>(sink),
		dataFileWriter(dataFileWriter)
	{
	};
	
	EventBuffer<Hit> * handleEvents(EventBuffer<Hit> *buffer) {
		dataFileWriter->writeSingleEvents(buffer);
		return buffer;
	};
	
	void pushT0(double t0) { };
	void report() { };
};

class WriteGroupsHelper : public OrderedEventHandler<GammaPhoton, GammaPhoton> {
private: 
	DataFileWriter *dataFileWriter;
public:
	WriteGroupsHelper(DataFileWriter *dataFileWriter, EventSink<GammaPhoton> *sink) :
		OrderedEventHandler<GammaPhoton, GammaPhoton>(sink),
		dataFileWriter(dataFileWriter)
	{
	};
	
	EventBuffer<GammaPhoton> * handleEvents(EventBuffer<GammaPhoton> *buffer) {
		dataFileWriter->writeGroupEvents(buffer);
		return buffer;
	};
	
	void pushT0(double t0) { };
	void report() { };
};


class WriteCoincidencesHelper : public OrderedEventHandler<Coincidence, Coincidence> {
private: 
	DataFileWriter *dataFileWriter;

public:
	WriteCoincidencesHelper(DataFileWriter *dataFileWriter,  EventSink<Coincidence> *sink) :
		OrderedEventHandler<Coincidence, Coincidence>(sink),
		dataFileWriter(dataFileWriter)
	{
	};
	
	EventBuffer<Coincidence> * handleEvents(EventBuffer<Coincidence> *buffer) {
		dataFileWriter->writeCoincidenceEvents(buffer);
		return buffer;
	};
};



struct BlockHeader  {
	float step1;
	float step2;	
	uint32_t wrPointer;
	uint32_t rdPointer;
	int32_t blockType;
};


int main(int argc, char *argv[])
{
	assert(argc == 12);
	long systemFrequency = boost::lexical_cast<long>(argv[1]);
	char *fileNamePrefix = argv[2];
	char *fType = argv[3];
	char *eType = argv[4];
	char *mode = argv[5];
	char *configFileName = argv[6];
	char *shmObjectPath = argv[7];
	int triggerID = boost::lexical_cast<int>(argv[8]);
	double acquisitionStartTime = boost::lexical_cast<double>(argv[9]);
	int eventFractionToWrite = round(1024*boost::lexical_cast<float>(argv[10])/ 100.0);
	int hitLimitToWrite = boost::lexical_cast<int>(argv[11]);

	EVENT_TYPE eventType; 
	if(strcmp(eType, "singles") == 0){
		eventType = SINGLE;
	}	
	else if(strcmp(eType, "groups") == 0){
		eventType = GROUP;
	}
	else eventType = COINCIDENCE;
	       
	
	FILE_TYPE fileType; 
	if(strcmp(fType, "binary") == 0){
		fileType = FILE_BINARY;
	}	
	else if(strcmp(fType, "root") == 0){
		fileType = FILE_ROOT;
	}
	else fileType = FILE_TEXT;



	bool totMode = (strcmp(mode, "tot") == 0);
       	
	// If data was taken in full ToT mode, do not attempt to load these files
	unsigned long long mask = SystemConfig::LOAD_ALL;	
	if(totMode){ 
		mask ^= (SystemConfig::LOAD_QDC_CALIBRATION | SystemConfig::LOAD_ENERGY_CALIBRATION);
	}
	SystemConfig *config = SystemConfig::fromFile(configFileName, mask);	
	
	PETSYS::SHM_RAW *shm = new PETSYS::SHM_RAW(shmObjectPath);
	bool firstBlock = true;

	BlockHeader blockHeader;
	
    long long stepEvents = 0;
	long long stepMaxFrame = 0;
	long long stepAllFrames = 0;
	long long stepLostFramesN = 0;
	long long stepLostFrames0 = 0;

	long long minFrameID = 0x7FFFFFFFFFFFFFFFLL, maxFrameID = 0, lastMaxFrameID = 0;
	
	long long lastFrameID = -1;
	long long stepFirstFrameID = -1;

	FrameType lastFrameType = FRAME_TYPE_UNKNOWN;

	auto pool = new ThreadPool<UndecodedHit>();
	auto eventStream = new OnlineEventStream(systemFrequency, triggerID);
	
	// If acquisition mode is mixed, read ".modf" file to assign channel energy mode 
	if(strcmp(mode, "mixed") == 0){
		char fName[1024];
		sprintf(fName, "%s.modf", fileNamePrefix);
		FILE *modeFile = fopen(fName, "r");
		if(modeFile == NULL) {
			fprintf(stderr, "Could not open '%s' for reading: %s\n", fName, strerror(errno));
			exit(1);
		}
		char line[PATH_MAX];
		while(fscanf(modeFile, "%[^\n]\n", line) == 1) {
			normalizeLine(line);
			if(strlen(line) == 0) continue;
			unsigned portID, slaveID, chipID,channelID;
			char m[128];		
			if(sscanf(line, "%d\t%u\t%u\t%u\t%s", &portID, &slaveID, &chipID, &channelID, m)!= 5) continue;
			unsigned long gChannelID = 0;
			gChannelID |= channelID;
			gChannelID |= (chipID << 6);
			gChannelID |= (slaveID << 12);
			gChannelID |= (portID << 17);
			eventStream->setMode(gChannelID, strcmp(m, "qdc") == 0);
		}
	}
	else{
		for(unsigned long gChannelID = 0; gChannelID < MAX_NUMBER_CHANNELS; gChannelID++)
			eventStream->setMode(gChannelID, strcmp(mode, "qdc") == 0);
	}

	char outputFileName[1024];
	
	DataFileWriter *dataFileWriter = new DataFileWriter(fileNamePrefix, eventStream->getFrequency(),  eventType, fileType, hitLimitToWrite, eventFractionToWrite);	

	Decoder *pipeline;
	if(eventType == SINGLE){
		pipeline = new Decoder(eventStream, 
			new CoarseSorter(
			new ProcessHit(config, eventStream,
			new WriteSinglesHelper(dataFileWriter, 
			new NullSink<Hit>()
			))));
	}
	else if(eventType == GROUP){
		pipeline = new Decoder(eventStream, 
			new CoarseSorter(
			new ProcessHit(config, eventStream,
			new SimpleGrouper(config,		
			new WriteGroupsHelper(dataFileWriter, 
			new NullSink<GammaPhoton>()
			)))));
	}
	else if(eventType == COINCIDENCE){
		pipeline = new Decoder(eventStream, 
			new CoarseSorter(
			new ProcessHit(config, eventStream,
			new SimpleGrouper(config,
			new CoincidenceGrouper(config,
			new WriteCoincidencesHelper(dataFileWriter, 
			new NullSink<Coincidence>()
			))))));
	}

	pipeline->pushT0(acquisitionStartTime);

	bool isReadyToAcquire = true; 
	fwrite(&isReadyToAcquire, sizeof(bool), 1, stdout);
	fflush(stdout);
	sleep(0.05);

	EventBuffer<UndecodedHit> *outBuffer = NULL; 
	size_t seqN = 0;
	long long currentBufferFirstFrame = 0;	

	
	while(fread(&blockHeader, sizeof(blockHeader), 1, stdin) == 1){

		dataFileWriter->setStep1(blockHeader.step1);
		dataFileWriter->setStep2(blockHeader.step2);

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
			else if((lastFrameID >= 0) && (frameID != (lastFrameID + 1))) {
				// We have skipped one or more frame ID, so 
				// we account them as lost...
				long long skippedFrames = (frameID - lastFrameID) - 1;
				stepAllFrames += skippedFrames;
				stepLostFrames0 += skippedFrames;				
				// .. and we set the lastFrameType
				lastFrameType = FRAME_TYPE_ALL_LOST;
			}

			lastFrameID = frameID;
			minFrameID = minFrameID < frameID ? minFrameID : frameID;
			maxFrameID = maxFrameID > frameID ? maxFrameID : frameID;

			// Get the pointer to the raw data frame
			PETSYS::RawDataFrame *dataFrame = shm->getRawDataFrame(index);
			// Increase the circular buffer pointer
			rdPointer = (rdPointer+1) % (2*bs);
			
			int frameSize = shm->getFrameSize(index);
			int nEvents = shm->getNEvents(index);
			bool frameLost = shm->getFrameLost(index);
			
			stepEvents += nEvents;
			stepMaxFrame = stepMaxFrame > nEvents ? stepMaxFrame : nEvents;

			if(frameLost){
				if(nEvents == 0)
					stepLostFrames0 += 1;
				else
					stepLostFramesN += 1;
			}			
			stepAllFrames += 1;
			FrameType frameType = FRAME_TYPE_UNKNOWN;
			if(frameLost) {
				frameType = (nEvents == 0) ? FRAME_TYPE_ZERO_DATA : FRAME_TYPE_SOME_DATA;
			}
			else {
				frameType = (nEvents == 0) ? FRAME_TYPE_ALL_LOST : FRAME_TYPE_SOME_LOST;
			}

			// Do not write sequences of normal empty frames, unless we're closing a step
			if(blockHeader.blockType == 1 && lastFrameType == FRAME_TYPE_ZERO_DATA && frameType == lastFrameType) {
				continue;
			}

			// Do not write sequences of all lost frames, unless we're closing a step
			if(blockHeader.blockType == 1 && lastFrameType == FRAME_TYPE_ALL_LOST && frameType == lastFrameType) {
				continue;
			}
			lastFrameType = frameType;

			// Blocksize
			// Best block size from profiling: 2048
			// but handle larger frames correctly
			//fprintf(stderr,"%d\n",nEvents);
			size_t allocSize = max(nEvents, 2048);
			if(outBuffer == NULL) {
				//fprintf(stderr,"Allocating first: %d %d %u %u %u %u\n",outBuffer->getFree(), nEvents,bs,rdPointer, wrPointer, index);
				currentBufferFirstFrame = dataFrame->getFrameID();
				outBuffer = new EventBuffer<UndecodedHit>(allocSize, seqN, currentBufferFirstFrame * 1024);
				seqN += 1;
			}
			else if((outBuffer->getFree() < nEvents) || ((frameID - currentBufferFirstFrame) > (1LL << 32))) {
				// Buffer is full or buffer is covering too much time
				//fprintf(stderr,"Allocating new: %d %d %u %u %u %u\n",outBuffer->getFree(), nEvents,bs,rdPointer, wrPointer, index);
				pool->queueTask(outBuffer, pipeline);
				currentBufferFirstFrame = dataFrame->getFrameID();
				outBuffer = new EventBuffer<UndecodedHit>(allocSize, seqN, currentBufferFirstFrame * 1024);
				seqN += 1;
			}
			//else{fprintf(stderr,"%d %d %u %u %u %u\n",outBuffer->getFree(), nEvents,bs,rdPointer, wrPointer, index);}
			
			UndecodedHit *p = outBuffer->getPtr() + outBuffer->getUsed();
			for(int i = 0; i < nEvents; i++) {
				p[i].frameID = frameID - currentBufferFirstFrame;
				p[i].eventWord = dataFrame->data[2+i];
			}
			outBuffer->setUsed(outBuffer->getUsed() + nEvents);
			outBuffer->setTMax((frameID + 1) * 1024);
	        
		}		
		
		if(outBuffer != NULL) {
			pool->queueTask(outBuffer, pipeline);
			outBuffer = NULL;
		}
		pool->completeQueue();
		//fprintf(stderr,"Completed queue\n");
		if(blockHeader.blockType == 2){
			
			fprintf(stderr, "onlineRaw:: Step had %lld frames with %lld events; %f events/frame avg, %lld event/frame max\n", 
				stepAllFrames, stepEvents, 
				float(stepEvents)/stepAllFrames,
				stepMaxFrame); fflush(stderr);
			fprintf(stderr, "onlineRaw:: some events were lost for %lld (%5.1f%%) frames; all events were lost for %lld (%5.1f%%) frames\n", 
				stepLostFramesN, 100.0 * stepLostFramesN / stepAllFrames,
				stepLostFrames0, 100.0 * stepLostFrames0 / stepAllFrames
				); 
			fflush(stderr);
			dataFileWriter->closeStep();

			stepAllFrames = 0;
			stepEvents = 0;
			stepMaxFrame = 0;
			stepLostFramesN = 0;
			stepLostFrames0 = 0;
			lastFrameID = -1;
			stepFirstFrameID = -1;
			lastFrameType = FRAME_TYPE_UNKNOWN;
		}

	
		fwrite(&rdPointer, sizeof(uint32_t), 1, stdout);
		long long dummy = 0;
		fwrite(&dummy, sizeof(long long), 1, stdout);
		fwrite(&dummy, sizeof(long long), 1, stdout);
		fwrite(&dummy, sizeof(long long), 1, stdout);
		fflush(stdout);
	}

	//pool->completeQueue();
	delete pool;		
	
	
	return 0;
}

