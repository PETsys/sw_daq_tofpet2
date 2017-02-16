#include <boost/python.hpp>
using namespace boost::python;

#include "shm_raw.hpp"
using namespace PETSYS;

BOOST_PYTHON_MODULE(shm_raw)
{
	class_<SHM_RAW>("SHM_RAW", init<std::string>())
		.def("getSizeInBytes", &SHM_RAW::getSizeInBytes)
		.def("getSizeInFrames", &SHM_RAW::getSizeInFrames)
		.def("getFrameSize", &SHM_RAW::getFrameSize)
		.def("getFrameWord", &SHM_RAW::getFrameWord)
		.def("getFrameID", &SHM_RAW::getFrameID)
		.def("getFrameLost", &SHM_RAW::getFrameLost)
		.def("getNEvents", &SHM_RAW::getNEvents)
		.def("getTCoarse", &SHM_RAW::getTCoarse)
		.def("getECoarse", &SHM_RAW::getECoarse)
		.def("getTFine", &SHM_RAW::getTFine)
		.def("getEFine", &SHM_RAW::getEFine)
		.def("getTacID", &SHM_RAW::getTacID)
		.def("getChannelID", &SHM_RAW::getChannelID)
	;
}
