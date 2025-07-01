#ifndef __PETSYS__ASYNC_WRITER_HPP__DEFINED__
#define __PETSYS__ASYNC_WRITER_HPP__DEFINED__

#include <libaio.h>
#include <string>
#include <cstdint>

namespace PETSYS {

class DataWriter {

	static const size_t BUFFER_SIZE = 1048576;     // 1MB buffers to better handle very large rates
	//static const size_t BUFFER_SIZE = 2097152;
	static const size_t N_BUFFERS = 16;

	// TODO: determine this programmatically
        static const size_t IO_BLOCK_SIZE = 4096*4;       // I/O block size to which O_DIRECT needs to be aligned

	struct Buffer {
		void* data;
		ssize_t used = 0;
		struct iocb cb;
    };

    Buffer buffers[N_BUFFERS];
    io_context_t ctx{};
	int fd;

public:
    DataWriter(const std::string& filename, bool acqStdMode);
    ~DataWriter();

	long long getCurrentPosition();
	long long getCurrentPositionFromFile();
	void appendData(void *buf, size_t count);

	void writeHeader(unsigned long long fileCreationDAQTime, double daqSynchronizationEpoch, long systemFrequency, char *mode, int triggerID);

private:
	void submittCurrentBuffer();
	void completeAllBuffers();

	int currentBufferIndex = 0;
	uint64_t global_offset = 0;
};

}

#endif // __PETSYS__ASYNC_WRITER_HPP__DEFINED__
