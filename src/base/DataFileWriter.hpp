#ifndef __PETSYS__DATA_FILE_WRITER_HPP__DEFINED__
#define __PETSYS__DATA_FILE_WRITER_HPP__DEFINED__

#include <Event.hpp>
#include <EventBuffer.hpp>
#include <string.h>
#include <TFile.h>
#include <TNtuple.h>
#include <OrderedEventHandler.hpp>
#include"AsyncWriter.hpp"

namespace PETSYS {

static const size_t MONITOR_STAGING_SIZE = 8192/2;	
//static const uint64_t MONITOR_PRESCALE_MASK = 0x3F;
static const uint64_t MONITOR_BUFFER_MASK = 0xFF;
	
enum FILE_TYPE {FILE_TEXT, FILE_BINARY, FILE_ROOT, FILE_NULL, FILE_TEXT_COMPACT, FILE_BINARY_COMPACT};

enum EVENT_TYPE {RAW, SINGLE, GROUP, COINCIDENCE};

enum WRITE_TARGET {TARGET_FILE, TARGET_SHM, TARGET_BOTH};

struct Event {
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

struct CoincidenceGroupHeader {
	uint8_t nHits1; 
	uint8_t nHits2;
} __attribute__((__packed__));

typedef uint8_t GroupHeader;


struct DataShm {
    uint32_t nChannels;
    uint32_t nEnergyBins;
    uint16_t nTypes;
    uint16_t _reserved1;
    uint32_t _reserved2;
    double   energyLow;
    double   energyHigh;
	double   elapsedTime;
	uint64_t hitsSeen[4];     
    uint64_t hitsSampled[4];   
    uint32_t counts[];      
};


struct DataWriterConfig {
    //Params for writting data to file
	std::string fName;
    bool useAsyncWriting;
    double frequency = 200E6;
    EVENT_TYPE eventType = COINCIDENCE;
    FILE_TYPE fileType = FILE_TEXT;
	int hitLimitToWrite = 1;
	double userTimeRef = 0.0;
    int eventFractionToWrite = 1024;
    float splitTime = 0;
    WRITE_TARGET writeTarget = TARGET_FILE;
	bool isDataTransmissionCheck = false;

    //Params for writting data to shm for monitoring
	uint64_t monitorBufferMask = MONITOR_BUFFER_MASK;
	uint32_t nChannels = 131072;
    uint32_t nEnergyBins = 1500;
    double energyLow = 0.0;
    double energyHigh = 500;
};

struct MonitorStaging {
    uint64_t keys[MONITOR_STAGING_SIZE];
    size_t used;
};


class DataFileWriter{
private:
	
	WRITE_TARGET writeTarget;

	std::string fName;
	FILE_TYPE fileType;
	EVENT_TYPE eventType;
	double userTimeRef;
	int eventFractionToWrite;
	long long eventCounter;
	double fileSplitTime;
	long long currentFilePartIndex;
	double frequency;
	int hitLimitToWrite;

	float step1;
	float step2;

	bool useAsyncWriting;
	DataWriter *dataWriter;
	FILE *dataFile;
	FILE *indexFile;
	off_t stepBegin;
	
	TTree *hData;
	TTree *hIndex;
	TFile *hFile;

	double Tps;
	float Tns;

	// ROOT Tree fields
	float		brStep1;
	float		brStep2;
	long long 	brStepBegin;
	long long 	brStepEnd;
	
	unsigned short	brN;
	unsigned short	brJ;
	
	long long	brTimeDelta;
	long long	brTime;
	unsigned int	brChannelID;
	float		brToT;
	float		brEnergy;
	float		brTotalEnergy;
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
	float		br1TotalEnergy, br2TotalEnergy;
	unsigned short	br1TacID,	br2TacID;
	int		br1Xi,		br2Xi;
	int		br1Yi,		br2Yi;
	float		br1X,		br2X;
	float 		br1Y,		br2Y;
	float 		br1Z,		br2Z;
	
	long long	brFrameID;
	unsigned short	brTCoarse;
	unsigned short	brECoarse;
	unsigned short	brTFine;
	unsigned short	brEFine;

	//for online monitoring
	std::string shmName;

    uint32_t monitorChannels;
    uint32_t monitorEnergyBins;
    uint16_t monitorTypes;
    double   monitorEnergyLow;
    double   monitorInvBinWidth;

	uint64_t monitorBufferMask;

	std::atomic<uint64_t> monitorHitsSeen[4];
	std::atomic<uint64_t> monitorHitsSampled[4];

	u_int64_t nHitsReceived =0;

    DataShm* shm = nullptr;
	
public:
	DataFileWriter(const DataWriterConfig& cfg);
	~DataFileWriter(); 
	
	void openFile(); 
	void closeFile();
	void setStepValues(float step1, float step2);
	void checkFilePartForSplit(long long filePartIndex);
	
	void closeStep();
	void renameFile();
	
	void writeRawEvents(EventBuffer<RawHit> *buffer, double t0);
	void writeSingleEvents(EventBuffer<Hit> *buffer, double t0);
	void writeGroupEvents(EventBuffer<GammaPhoton> *buffer, double t0);
	void writeCoincidenceEvents(EventBuffer<Coincidence> *buffer, double t0);

	void openShm(uint32_t nChannels, uint32_t nEnergyBins, uint16_t nTypes, double energyLow, double energyHigh, const char* shmName = "/online_data");

    bool fillMonitoringData(uint16_t type, uint32_t ch, double energy);
	void addMonitorCounters(uint16_t type, uint64_t nSeen, uint64_t nFilled);
	
	void fillElapsedTime(double time);
	
	void resetMonitoringData();
	bool processForMonitoring();

	EVENT_TYPE getEventType(){
		return this->eventType;
	}
};


class WriteRawHelper : public OrderedEventHandler<RawHit, RawHit> {
private: 
	DataFileWriter *dataFileWriter;
public:
	WriteRawHelper(DataFileWriter *dataFileWriter, EventSink<RawHit> *sink) :
		OrderedEventHandler<RawHit, RawHit>(sink),
		dataFileWriter(dataFileWriter)
	{
	};
	
	EventBuffer<RawHit> * handleEvents(EventBuffer<RawHit> *buffer) {
		dataFileWriter->writeRawEvents(buffer, getT0());
		if(dataFileWriter->getEventType() == RAW){
			buffer->setUsed(0);
		}
		return buffer;
	};

	void report() {
		if (dataFileWriter->getEventType() != RAW) {
			this->sink->report();
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
		dataFileWriter->writeSingleEvents(buffer, getT0());
		if (dataFileWriter->getEventType() == SINGLE) {
			buffer->setUsed(0);
		}

		return buffer;
	};

	void report() {
		if (dataFileWriter->getEventType() == GROUP || dataFileWriter->getEventType() == COINCIDENCE) {
			this->sink->report();
		}
	};
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
		dataFileWriter->writeGroupEvents(buffer, getT0());
		if (dataFileWriter->getEventType() == GROUP) {
			buffer->setUsed(0);
		}
		return buffer;
	};
	void report() {
		if (dataFileWriter->getEventType() == COINCIDENCE) {
			this->sink->report();
		}
	};

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

		dataFileWriter->writeCoincidenceEvents(buffer, getT0());
		return buffer;
	};
};        
}


#endif // __PETSYS__DATA_FILE_WRITER_HPP__DEFINED__
