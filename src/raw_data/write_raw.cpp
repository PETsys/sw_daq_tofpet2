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
#include <map>
#include <shm_raw.hpp>
#include <boost/lexical_cast.hpp>
#include <pthread.h>
#include <unistd.h>
#include <iostream>
#include <pthread.h>
#include <cstring>

#include <libaio.h>


using namespace std;

struct CalibrationData{
	uint64_t eventWord;
	int freq;
};

class DataWriter {

    static const size_t BUFFER_SIZE = 1048576; // 1MB buffers to better handle very large rates
	static const int N_BUFFERS = 32;
	static const size_t WORD_SIZE = sizeof(uint64_t); 

	struct Buffer {
        void* data;
        int size = 0;
    };

    Buffer buffers[N_BUFFERS];
    io_context_t ctx{};
	int fd;

public:
    DataWriter(const std::string& filename, bool acqStdMode);
    ~DataWriter();
	void fillDataBuffer(PETSYS::RawDataFrame *dataFrame , int frameSize); 
	void splitFrameToBuffers(PETSYS::RawDataFrame *dataFrame, int frameSize);
    void writeCurrentDataBuffer(bool resetSize = false);
	void swapBuffers();
	size_t getBufferSize(){return BUFFER_SIZE;}
	void writeHeader(unsigned long long fileCreationDAQTime, double daqSynchronizationEpoch, long systemFrequency, char *mode, int triggerID);
	void writeCalibrationData(CalibrationData c);
	void setAcqModeToCalibration(){acqStdMode = false;}
	long long getCurrentPosition(){
		if(!acqStdMode){
			return (long long)(lseek(fd, 0, SEEK_CUR));
		}
		else return global_offset + buffers[currentBufferIndex].size;
	}

private:
    int currentBufferIndex = 0;
	uint64_t global_offset = 0;
	int nSubmissions = 0;
	bool acqStdMode = true;
};

DataWriter::DataWriter(const std::string& filename, bool acqStdMode) {
	if(acqStdMode && strcmp(filename.c_str(), "/dev/null") != 0){
		fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC| O_DIRECT, 0644);
	}
	else if(!acqStdMode){
		setAcqModeToCalibration();
		fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC , 0644);
	}
	else if (strcmp(filename.c_str(), "/dev/null") == 0){
		fd = open(filename.c_str(), O_WRONLY);
	}

    if (fd < 0) {
        fprintf(stderr,"ERROR: Failed to open file");
        exit(-1);
    }
    if (io_setup(4, &ctx) != 0){
        fprintf(stderr,"ERROR: Failed io_setup");
        exit(-1);
    }

	for (int i = 0; i < N_BUFFERS; i++) {
        int ret = posix_memalign(&buffers[i].data, BUFFER_SIZE, BUFFER_SIZE);
        if (ret != 0) {
            fprintf(stderr,"ERROR: Failed posix_memalign alocation");
			return;
        }
        memset(buffers[i].data, 0, BUFFER_SIZE);
    }
}

DataWriter::~DataWriter() {
    io_destroy(ctx);
    close(fd);
}

void DataWriter::fillDataBuffer(PETSYS::RawDataFrame *dataFrame , int frameSize) {
	Buffer& buf = buffers[currentBufferIndex];
	if(buf.size + frameSize * WORD_SIZE < BUFFER_SIZE){
		Buffer& buf = buffers[currentBufferIndex];
		memcpy((char*)buf.data + buf.size, (void *)(dataFrame->data), frameSize * sizeof(uint64_t));
		buf.size += frameSize * WORD_SIZE ;
	}
	else{
		splitFrameToBuffers(dataFrame, frameSize);
	}
}

void DataWriter::splitFrameToBuffers(PETSYS::RawDataFrame *dataFrame , int frameSize) {
	Buffer& currentBuffer = buffers[currentBufferIndex];
	Buffer& nextBuffer = buffers[(currentBufferIndex + 1) % N_BUFFERS];

	size_t bytesForCurrentBuffer = BUFFER_SIZE - currentBuffer.size;
	size_t bytesForNextBuffer = frameSize * WORD_SIZE - bytesForCurrentBuffer;

	//Copy the the available bytes into the current buffer  and write it to disk
	memcpy((char*)currentBuffer.data + currentBuffer.size, (void *)(dataFrame->data), bytesForCurrentBuffer);
	writeCurrentDataBuffer();

	if(nSubmissions == N_BUFFERS){
		struct io_event events[nSubmissions];
		int completed = io_getevents(ctx, nSubmissions, nSubmissions, events, NULL);
		assert(completed == nSubmissions);
		nSubmissions = 0;
	}



	//Copy the reminder of the data frame to the nex buffer and swap buffers for further writing
	void *startDataPtr = (char *)(dataFrame->data) + bytesForCurrentBuffer;
    memcpy(nextBuffer.data, startDataPtr, bytesForNextBuffer);
	nextBuffer.size = bytesForNextBuffer;

	swapBuffers();
}


void DataWriter::writeCurrentDataBuffer(bool resetBuffer){

    Buffer& currentBuffer = buffers[currentBufferIndex];

	struct iocb cb;
    struct iocb* cbs[1] = {&cb};

	io_prep_pwrite(&cb, this->fd, currentBuffer.data, BUFFER_SIZE, global_offset);
    io_submit(ctx, 1, cbs);

	nSubmissions++;
	
	//struct io_event events[1];
	//int completed = io_getevents(ctx, 1, 1, events, NULL);
	
	if(resetBuffer) return;

	global_offset += BUFFER_SIZE;
}


void DataWriter::swapBuffers() {
	Buffer& buf = buffers[currentBufferIndex];
	buf.size = 0;
    currentBufferIndex = (currentBufferIndex + 1) % N_BUFFERS;
}


void DataWriter::writeHeader(unsigned long long fileCreationDAQTime, double daqSynchronizationEpoch, long systemFrequency, char *mode, int triggerID) {
	bool qdcMode = (strcmp(mode, "qdc") == 0);
	uint64_t header[8];
	for(int i = 0; i < 8; i++)
		header[i] = 0;
	header[0] |= uint32_t(systemFrequency);
	header[0] |= (qdcMode ? 0x1UL : 0x0UL) << 32;
	memcpy(header+1, &daqSynchronizationEpoch, sizeof(double));
	if (triggerID != -1) { header[2] = 0x8000 + triggerID; }
	if (strcmp(mode, "mixed") == 0) { header[3] = 0x1UL; }
	header[4] = fileCreationDAQTime;

	Buffer& currentBuffer = buffers[currentBufferIndex];

	if(acqStdMode){
		memcpy(currentBuffer.data, (void *)&header, sizeof(uint64_t)*8);
		currentBuffer.size =  sizeof(uint64_t)*8;
	}
	else {
		write(this->fd, (void *)&header, sizeof(uint64_t)*8);
	}
}

void DataWriter::writeCalibrationData(CalibrationData c){
	write(this->fd, &c, sizeof(CalibrationData));
	global_offset += sizeof(CalibrationData);
}

class CalibrationPool {
public:
	CalibrationPool(PETSYS::SHM_RAW *shm);
	~CalibrationPool();

	void clear();
	void processBatch(unsigned start, unsigned end);
	void writeOut(DataWriter *writer);

private:
	unsigned n_cpu;
	PETSYS::SHM_RAW *shm;
	vector<map<uint64_t, unsigned>> calEventSet;

	struct worker_t {
			CalibrationPool *self;
			pthread_t thread;
			unsigned cpu_index;
			unsigned start;
			unsigned end;
	};
	static void * thread_routine(void *arg);
};

struct BlockHeader  {
	float step1;
	float step2;	
	uint32_t wrPointer;
	uint32_t rdPointer;
	int32_t blockType;
};

enum FrameType { FRAME_TYPE_UNKNOWN, FRAME_TYPE_SOME_DATA, FRAME_TYPE_ZERO_DATA, FRAME_TYPE_SOME_LOST, FRAME_TYPE_ALL_LOST };

int main(int argc, char *argv[])
{
	assert(argc == 9);
	char *shmObjectPath = argv[1];
	char *outputFilePrefix = argv[2];
	long systemFrequency = boost::lexical_cast<long>(argv[3]);
	bool qdcMode = (strcmp(argv[4], "qdc") == 0);
	double daqSynchronizationEpoch = boost::lexical_cast<double>(argv[5]);
	unsigned long long fileCreationDAQTime = boost::lexical_cast<unsigned long long>(argv[6]);
	bool acqStdMode = (argv[7][0] == 'N');
	int triggerID = boost::lexical_cast<int>(argv[8]);

	PETSYS::SHM_RAW *shm = new PETSYS::SHM_RAW(shmObjectPath);


	char fNameRaw[1024];
	char fNameIdx[1024];
	char fNameTmp[1024];


	if(strcmp(outputFilePrefix, "/dev/null") == 0) {
		sprintf(fNameRaw, "%s", outputFilePrefix);
		sprintf(fNameIdx, "%s", outputFilePrefix);
		sprintf(fNameTmp, "%s", outputFilePrefix);
	}
	else {
		sprintf(fNameRaw, "%s.rawf", outputFilePrefix);
		sprintf(fNameIdx, "%s.idxf", outputFilePrefix);
		sprintf(fNameTmp, "%s.tmpf", outputFilePrefix);
	}

	

	FILE * indexFile = fopen(fNameIdx, "wb");
	if(indexFile == NULL) {
		fprintf(stderr, "Could not open '%s' for writing: %s\n", fNameIdx, strerror(errno));
		return 1;
	}

	FILE * tempFile = fopen(fNameTmp, "wb");
	if(tempFile == NULL) {
		fprintf(stderr, "Could not open '%s' for writing: %s\n", fNameTmp, strerror(errno));
		return 1;
	}

	DataWriter writer(fNameRaw, acqStdMode);

	fprintf(stderr, "INFO: Writing data to '%s.rawf' and index to '%s.idxf'\n", outputFilePrefix, outputFilePrefix);

	writer.writeHeader(fileCreationDAQTime, daqSynchronizationEpoch, systemFrequency, argv[4], triggerID);

	CalibrationPool calibrationPool(shm);

	bool firstBlock = true;
	float step1;
	float step2;
	BlockHeader blockHeader;
	
	long long stepEvents = 0;
	long long stepMaxFrame = 0;
	long long stepAllFrames = 0;
	long long stepLostFramesN = 0;
	long long stepLostFrames0 = 0;
	
	long long minFrameID = 0x7FFFFFFFFFFFFFFFLL, maxFrameID = 0, lastMaxFrameID = 0;
	
	long long lastFrameID = -1;
	long long stepFirstFrameID = -1;
	
	long stepStartOffset;
	FrameType lastFrameType = FRAME_TYPE_UNKNOWN;

	int r;

	while(fread(&blockHeader, sizeof(blockHeader), 1, stdin) == 1) {

		unsigned bs = shm->getSizeInFrames();
		unsigned rdPointer = blockHeader.rdPointer % (2*bs);
		unsigned wrPointer = blockHeader.wrPointer % (2*bs);

		step1 = blockHeader.step1;
		step2 = blockHeader.step2;

		if(blockHeader.blockType == 0) {
			// First block in a step
			unsigned index = rdPointer % bs;

			stepAllFrames = 0;
			stepEvents = 0;
			stepMaxFrame = 0;
			stepLostFramesN = 0;
			stepLostFrames0 = 0;

			stepFirstFrameID = shm->getFrameID(index);
			lastFrameID = stepFirstFrameID - 1;
			lastFrameType = FRAME_TYPE_UNKNOWN;

			if(!acqStdMode) calibrationPool.clear();

			stepStartOffset = writer.getCurrentPosition();

			r = fprintf(tempFile, "%f\t%f\t%ld\t%ld\t", blockHeader.step1, blockHeader.step2, stepStartOffset, stepFirstFrameID);
			if(r < 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
			r = fflush(tempFile);
			if(r != 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }

		}
		

		if(!acqStdMode) calibrationPool.processBatch(rdPointer, wrPointer);

		while(rdPointer != wrPointer) {
			unsigned index = rdPointer % bs;
			
			long long frameID = shm->getFrameID(index);
			if(frameID <= lastFrameID) {
				fprintf(stderr, "WARNING!! Frame ID reversal: %12lld -> %12lld | %04u %04u %04u\n", 
					lastFrameID, frameID, 
					blockHeader.wrPointer, blockHeader.rdPointer, rdPointer
					);
				
			}
			else if (frameID != (lastFrameID + 1)) {
				// We have skipped one or more frame ID, so 
				// we account them as lost...
				long long skippedFrames = (frameID - lastFrameID) - 1;
				stepAllFrames += skippedFrames;
				stepLostFrames0 += skippedFrames;

				// ...and we write the first frameID of the lost batch to the datafile...
				if(acqStdMode) {
					PETSYS::RawDataFrame *lostFrameInfo = new PETSYS::RawDataFrame;
					lostFrameInfo->data[0] = (2ULL << 36) | (lastFrameID + 1);
					lostFrameInfo->data[1] = 1ULL << 16;
					writer.fillDataBuffer(lostFrameInfo, 2);
					delete lostFrameInfo;
				}
				
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
			
			// Accounting
			stepEvents += nEvents;
			stepMaxFrame = stepMaxFrame > nEvents ? stepMaxFrame : nEvents;
			if(frameLost) {
				if(nEvents == 0)
					stepLostFrames0 += 1;
				else
					stepLostFramesN += 1;
			}			
			stepAllFrames += 1;

			// Determine this frame type
			FrameType frameType = FRAME_TYPE_UNKNOWN;
			if(frameLost) {
				frameType = (nEvents == 0) ? FRAME_TYPE_ZERO_DATA : FRAME_TYPE_SOME_DATA;
			}
			else {
				frameType = (nEvents == 0) ? FRAME_TYPE_ALL_LOST : FRAME_TYPE_SOME_LOST;
			}

			// Do not write sequences of normal empty frames, unless we're closing a step
			if(blockHeader.blockType != 2 && lastFrameType == FRAME_TYPE_ZERO_DATA && frameType == lastFrameType) {
				continue;
			}

			// Do not write sequences of all lost frames, unless we're closing a step
			if(blockHeader.blockType != 2 && lastFrameType == FRAME_TYPE_ALL_LOST && frameType == lastFrameType) {
				continue;
			}
			lastFrameType = frameType;

			// Write out the data frame contents
			if(acqStdMode){
				writer.fillDataBuffer(dataFrame, frameSize);
			}
		}

		if(blockHeader.blockType == 2) {
			// If acquiring calibration data, at the end of each calibration step, write compressed data to disk
			if(!acqStdMode){
				calibrationPool.writeOut(&writer);
			}	
			else{
				writer.writeCurrentDataBuffer(true);
			}
			fprintf(stderr, "writeRaw:: Step had %lld frames with %lld events; %f events/frame avg, %lld event/frame max\n", 
					stepAllFrames, stepEvents, 
					float(stepEvents)/stepAllFrames,
					stepMaxFrame); fflush(stderr);
			fprintf(stderr, "writeRaw:: some events were lost for %lld (%5.1f%%) frames; all events were lost for %lld (%5.1f%%) frames\n", 
					stepLostFramesN, 100.0 * stepLostFramesN / stepAllFrames,
					stepLostFrames0, 100.0 * stepLostFrames0 / stepAllFrames
					); 
			fflush(stderr);
			
			if(r != 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }

			r = fprintf(indexFile, "%ld\t%lld\t%lld\t%lld\t%f\t%f\n", stepStartOffset, writer.getCurrentPosition(), stepFirstFrameID, lastFrameID, blockHeader.step1, blockHeader.step2);

			if(r < 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
			r = fflush(indexFile);
			if(r != 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }

			if(r < 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
			r = fflush(tempFile);
			if(r != 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }

		}

		fwrite(&rdPointer, sizeof(uint32_t), 1, stdout);
		fwrite(&stepAllFrames, sizeof(long long), 1, stdout);
		fwrite(&stepLostFrames0, sizeof(long long), 1, stdout);
		fwrite(&stepEvents, sizeof(long long), 1, stdout);
		fflush(stdout);
	}


	// Write a fake step to mark end of data
	r = fprintf(tempFile, "%f\t%f\t%llu\t%llu\n", 0.0, 0.0, ULLONG_MAX, ULLONG_MAX);
	if(r < 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }
	r = fflush(tempFile);
	if(r != 0) { fprintf(stderr, "ERROR writing to %s: %d %s\n", fNameRaw, errno, strerror(errno)); exit(1); }

	fclose(tempFile);
	unlink(fNameTmp);
	fclose(indexFile);

	return 0;
}

CalibrationPool::CalibrationPool(PETSYS::SHM_RAW *shm)
	: shm(shm)
{
	n_cpu = sysconf(_SC_NPROCESSORS_ONLN);
	calEventSet = vector<map<uint64_t, unsigned>>(n_cpu);
};

CalibrationPool::~CalibrationPool()
{
};

void CalibrationPool::clear()
{
	for (auto i = calEventSet.begin(); i != calEventSet.end(); i++)
		i->clear();

}

void CalibrationPool::processBatch(unsigned start, unsigned end)
{
	vector<worker_t> workers(n_cpu);
	for(auto i = 0; i < n_cpu; i++) {
		workers[i].self = this;
		workers[i].cpu_index = i;
		workers[i].start = start;
		workers[i].end = end;

		pthread_create(&workers[i].thread, NULL, thread_routine, &workers[i]);
	}

	for(auto i = workers.begin(); i != workers.end(); i++) {
		pthread_join(i->thread, NULL);
	}
};

void * CalibrationPool::thread_routine(void *arg)
{
	worker_t *worker = (worker_t *)arg;
	PETSYS::SHM_RAW *shm = worker->self->shm;
	unsigned bs = shm->getSizeInFrames();
	unsigned n_cpu = worker->self->n_cpu;
	unsigned cpu_index = worker->cpu_index;
	
	unsigned index2 = worker->start;
	while(index2 != worker->end) {
		unsigned index = index2 % bs;

		int frameSize = shm->getNEvents(index);
		for(int i = 0; i < frameSize; i++) {
			unsigned g = shm->getChannelID(index, i);
			unsigned channelID = g % 64;
			unsigned asicID = (g >> 6) % 64;
			unsigned slaveID = (g >> 12) % 32;
			unsigned portID = (g >> 17) % 32;
		
			unsigned hash = channelID ^ asicID ^ slaveID ^ portID;
			hash = hash % n_cpu;

			if(hash != cpu_index) continue;

			uint64_t event_word = shm->getFrameWord(index, i+2);
			worker->self->calEventSet[cpu_index][event_word]++;
		}

		index2 = (index2 + 1) % (2 * bs);
	}

	return NULL;
}

void CalibrationPool::writeOut(DataWriter *writer)
{
	for(auto i = calEventSet.begin(); i != calEventSet.end(); i++) {
		for(auto j = i->begin(); j != i->end(); j++) {
			CalibrationData c;
			c.eventWord = j->first;
			c.freq = j->second;
			writer->writeCalibrationData(c);
		}
		i->clear();
	}

}
