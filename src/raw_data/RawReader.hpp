#ifndef __PETSYS__RAW_READER_HPP__DEFINED__
#define __PETSYS__RAW_READER_HPP__DEFINED__

#include <EventSourceSink.hpp>
#include <Event.hpp>

#include <vector>

namespace PETSYS {
	class RawReader {
	public:
		~RawReader();
		static RawReader *openFile(const char *fnPrefix);

		bool isQDC();
		double getFrequency();
		int getNSteps();
		void getStepValue(int n, float &step1, float &step2);
		void processStep(int n, bool verbose, EventSink<RawHit> *pipeline);
		void processLastStep(bool verbose, EventSink<RawHit> *pipeline);

	private:
		RawReader();
		void processRange(unsigned long begin, unsigned long end, bool verbose, EventSink<RawHit> *pipeline);

		struct Step {
                        float step1;
                        float step2;
                        unsigned long long stepBegin;
                        unsigned long long stepEnd;
			long long stepFirstFrame;
			long long stepLastFrame;
                };
                std::vector<Step> steps;
		int dataFile;
		char *dataFileBuffer;
		char *dataFileBufferPtr;
		char *dataFileBufferEnd;
		int readFromDataFile(char *buf, int count);

		unsigned frequency;
		bool qdcMode;

		
	};
}

#endif // __PETSYS__RAW_READER_HPP__DEFINED__
