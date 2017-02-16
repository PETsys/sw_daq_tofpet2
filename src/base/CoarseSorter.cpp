#include "CoarseSorter.hpp"
#include <vector>
#include <algorithm>
#include <functional>

using namespace std;
using namespace PETSYS;

CoarseSorter::CoarseSorter(EventSink<RawHit> *sink) :
	OverlappedEventHandler<RawHit, RawHit>(sink)
{
	nSingleRead = 0;
}

struct SortEntry {
	long long frameID;
        long long time;
        unsigned index;
};

static bool operator< (SortEntry lhs, SortEntry rhs) { return (lhs.frameID < rhs.frameID) ||  (lhs.time < rhs.time); }


EventBuffer<RawHit> * CoarseSorter::handleEvents (EventBuffer<RawHit> *inBuffer)
{
	unsigned N =  inBuffer->getSize();
	EventBuffer<RawHit> * outBuffer = new EventBuffer<RawHit>(N, inBuffer);
	u_int32_t lSingleRead = 0;

	vector<SortEntry> sortList;
	sortList.reserve(N);

	long long T = 1;
	
	for(unsigned i = 0; i < N; i++) {
		RawHit &p = inBuffer->get(i);
		long frameTime = 1024L * T;
		SortEntry entry = { (long long)(p.time / frameTime), (long long)(p.time / (4*T)), i };
		sortList.push_back(entry);
	}
	
	sort(sortList.begin(), sortList.end());
	
	for(unsigned j = 0; j < sortList.size(); j++) {
		unsigned i = sortList[j].index;
		RawHit &p = inBuffer->get(i);
		outBuffer->push(p);
		lSingleRead++;
	}
	atomicAdd(nSingleRead, lSingleRead);

	return outBuffer;
}

void CoarseSorter::report()
{
	u_int32_t nTotal = nSingleRead;
	fprintf(stderr, ">> CoarseSorter report\n");
	fprintf(stderr, " events passed\n");
	fprintf(stderr, "  %10u\n", nSingleRead);
	OverlappedEventHandler<RawHit, RawHit>::report();
}
