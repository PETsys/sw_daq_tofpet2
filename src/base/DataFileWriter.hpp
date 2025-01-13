#ifndef __PETSYS__DATA_FILE_WRITER_HPP__DEFINED__
#define __PETSYS__DATA_FILE_WRITER_HPP__DEFINED__

#include <Event.hpp>
#include <EventBuffer.hpp>
#include <string.h>
#include <TFile.h>
#include <TNtuple.h>
#include <OrderedEventHandler.hpp>

namespace PETSYS {
	
enum FILE_TYPE { FILE_TEXT, FILE_BINARY, FILE_ROOT, FILE_NULL, FILE_TEXT_COMPACT, FILE_BINARY_COMPACT};

enum EVENT_TYPE { RAW, SINGLE, GROUP, COINCIDENCE};

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


class DataFileWriter{
private:
	std::string fName;
	FILE_TYPE fileType;
	EVENT_TYPE eventType;
	double fileEpoch;
	int eventFractionToWrite;
	long long eventCounter;
	double fileSplitTime;
	long long currentFilePartIndex;

	int hitLimitToWrite;

	float step1;
	float step2;

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
	
	long long	brFrameID;
	unsigned short	brTCoarse;
	unsigned short	brECoarse;
	unsigned short	brTFine;
	unsigned short	brEFine;

public:
	DataFileWriter(char *fName, double frequency, EVENT_TYPE eventType, FILE_TYPE fileType, double fileEpoch, int hitLimitToWrite, int eventFractionToWrite, float splitTime);
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
		return buffer;
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
		return buffer;
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
		return buffer;
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
