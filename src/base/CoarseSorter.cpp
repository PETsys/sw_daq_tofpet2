#include "CoarseSorter.hpp"
#include <vector>
#include <algorithm>
#include <functional>

using namespace std;
using namespace PETSYS;

CoarseSorter::CoarseSorter(EventSink<RawHit> *sink) :
	UnorderedEventHandler<RawHit, RawHit>(sink)
{
	nSingleRead = 0;
}

struct SortEntry {
        long long time;
        RawHit *p;
};

static bool operator< (SortEntry lhs, SortEntry rhs) { return lhs.time < rhs.time; }


EventBuffer<RawHit> * CoarseSorter::handleEvents (EventBuffer<RawHit> *inBuffer)
{
	unsigned N =  inBuffer->getSize();
	EventBuffer<RawHit> * outBuffer = new EventBuffer<RawHit>(N, inBuffer);
	u_int64_t lSingleRead = 0;
	
	vector<SortEntry> sortList;
	sortList.reserve(N);

	auto pi = inBuffer->getPtr();
	auto pe = pi + N;

	for(; pi < pe; pi++) {
		SortEntry entry = {
			.time = pi->time,
			.p = pi
		};
		sortList.push_back(entry);
	}
	
	sort(sortList.begin(), sortList.end());
	
	auto po = outBuffer->getPtr();
	for(auto iter = sortList.begin(); iter != sortList.end(); iter++) {
		auto p = (*iter).p;
		*po = *p;
		po++;
		lSingleRead++;
	}

	
	atomicAdd(nSingleRead, lSingleRead);

	outBuffer->setUsed(lSingleRead);
	return outBuffer;
}

void CoarseSorter::report()
{
	u_int64_t nTotal = nSingleRead;
	fprintf(stderr, ">> CoarseSorter report\n");
	fprintf(stderr, " events passed\n");
	fprintf(stderr, "  %10lu\n", nSingleRead);
	UnorderedEventHandler<RawHit, RawHit>::report();
}
