#include <string.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>

#include "shm_raw.hpp"

using namespace PETSYS;

SHM_RAW::SHM_RAW(std::string shmPath)
{
	shmfd = shm_open(shmPath.c_str(), 
			O_RDONLY, 
			S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	if (shmfd < 0) {
		fprintf(stderr, "Opening '%s' returned %d (errno = %d)\n", shmPath.c_str(), shmfd, errno );		
		exit(1);
	}
	shmSize = lseek(shmfd, 0, SEEK_END);
	assert(shmSize = MaxRawDataFrameQueueSize * sizeof(RawDataFrame));
	
	shm = (RawDataFrame *)mmap(NULL, 
				shmSize,
				PROT_READ, 
				MAP_SHARED, 
				shmfd,
				0);
}

SHM_RAW::~SHM_RAW()
{
	munmap(shm, shmSize);
	close(shmfd);
}

unsigned long long SHM_RAW::getSizeInBytes()
{
	assert (shmSize = MaxRawDataFrameQueueSize * sizeof(RawDataFrame));
	return shmSize;
}
