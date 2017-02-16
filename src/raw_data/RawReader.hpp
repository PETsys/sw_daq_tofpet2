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

		double getFrequency();
		int getNSteps();
		void getStepValue(int n, float &step1, float &step2);
		void processStep(int n, EventSink<RawHit> *pipeline);
		void processLastStep(EventSink<RawHit> *pipeline);

	private:
		RawReader();
		void processRange(unsigned long begin, unsigned long end, EventSink<RawHit> *pipeline);

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
		unsigned frequency;
		bool qdcMode;

		
	};
}

#endif // __PETSYS__RAW_READER_HPP__DEFINED__
