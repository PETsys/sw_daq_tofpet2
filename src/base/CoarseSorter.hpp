#ifndef __PETSYS_COARSE_SORTER_HPP__DEFINED__
#define __PETSYS_COARSE_SORTER_HPP__DEFINED__
#include <Event.hpp>
#include <UnorderedEventHandler.hpp>
#include <Instrumentation.hpp>
#include <vector>

namespace PETSYS {
	
	/*! Sorts RawHit events into chronological order by their (coarse) time tag.
	 * Events are correctly sorted in relation to their frame ID, while ithing a frame, 
	 * they are sorted with a tolerance of overalp/2.
	 * Overlap/2 precision is good enough for the remainding of the software processing chain.
	 * Having correct frame boundaries is convenient for modules which write out events grouped by frame.
	 */
	 
	class CoarseSorter : public UnorderedEventHandler<RawHit, RawHit> {
	public:
		CoarseSorter (EventSink<RawHit> *sink);
		void report();
	protected:
		virtual EventBuffer<RawHit> * handleEvents (EventBuffer<RawHit> *inBuffer);
	private:
		u_int64_t nSingleRead;
	};

}
#endif 
