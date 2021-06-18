#include "Monitor.hpp"
#include "SingleValue.hpp"
#include "Histogram1D.hpp"
#include <boost/python.hpp>

using namespace boost::python;

BOOST_PYTHON_MODULE(monitor) {
	
	class_<PETSYS::OnlineMonitor::SingleValue>("SingleValue", init<std::string, PETSYS::OnlineMonitor::Monitor *>())
		.def("getValue", &PETSYS::OnlineMonitor::SingleValue::getValue)
		.def("setValue", &PETSYS::OnlineMonitor::SingleValue::setValue)
	;
	
	class_<PETSYS::OnlineMonitor::Histogram1D>("Histogram1D", init<int, double, double, std::string, PETSYS::OnlineMonitor::Monitor *>())
		.def("fill", &PETSYS::OnlineMonitor::Histogram1D::fill)
		.def("getBin", &PETSYS::OnlineMonitor::Histogram1D::getBin)
		.def("getSum", &PETSYS::OnlineMonitor::Histogram1D::getSum)
	;
	
	
	class_<PETSYS::OnlineMonitor::Monitor>("Monitor", init<>())
		.def("lock", &PETSYS::OnlineMonitor::Monitor::lock)
		.def("unlock", &PETSYS::OnlineMonitor::Monitor::unlock)
		.def("addObject", &PETSYS::OnlineMonitor::Monitor::addObject)
		.def("materialize", &PETSYS::OnlineMonitor::Monitor::materialize)
		.def("writeTOC", &PETSYS::OnlineMonitor::Monitor::writeTOC)
		.def("resetAllObjects", &PETSYS::OnlineMonitor::Monitor::resetAllObjects)
	;
	
}
