#ifndef __PETSYS__RAW_READER_HPP__DEFINED__
#define __PETSYS__RAW_READER_HPP__DEFINED__

#include <EventSourceSink.hpp>
#include <Event.hpp>
#include <UnorderedEventHandler.hpp>
#include <event_decode.hpp>

#include <vector>

static const unsigned MAX_NUMBER_CHANNELS = 4194304;

namespace PETSYS {

	class RawReader : public EventStream {
	private:
		struct UndecodedHit {
			u_int64_t frameID;
			u_int64_t eventWord;
		};

		class Decoder : public UnorderedEventHandler<UndecodedHit, RawHit> {
	        public:
	                Decoder(RawReader *reader, EventSink<RawHit> *sink);
	                void report();
	        protected:
	                virtual EventBuffer<RawHit> * handleEvents (EventBuffer<UndecodedHit> *inBuffer);
		private:
			RawReader *reader;
		};


	public:
		~RawReader();
		static RawReader *openFile(const char *fnPrefix);
		bool isQDC(unsigned int gChannelID);
		bool isTOT();
		double getFrequency();
		int getTriggerID();
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
		bool qdcMode[MAX_NUMBER_CHANNELS];		
		int triggerID;
		
		
	};
}

#endif // __PETSYS__RAW_READER_HPP__DEFINED__
