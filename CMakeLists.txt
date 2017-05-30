cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(sw_daq_tofpet2)
find_package(PythonLibs)
find_package(Boost REQUIRED python regex)
include(CheckIncludeFiles)

include_directories(${Boost_INCLUDE_DIRS} ${PYTHON_INCLUDE_DIRS})
link_libraries("rt" "pthread")

include_directories("src/raw_data")
include_directories("src/base")

# Need to define LINUX for WINDRIVER
# even if just to check it's presence.
if(UNIX AND NOT APPLE)
set( CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -DLINUX" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLINUX" )
set( CMAKE_REQUIRED_DEFINITIONS -DLINUX )
endif()

check_include_files( "pfp_api.h" HAVE_PFP_KX7 )

set (DAQD_SOURCES "src/daqd/daqd.cpp" "src/daqd/Client.cpp" "src/daqd/FrameServer.cpp" "src/daqd/UDPFrameServer.cpp" "src/raw_data/shm_raw.cpp" )
if(HAVE_PFP_KX7)
	set( DAQD_SOURCES ${DAQD_SOURCES} "src/daqd/DAQFrameServer.cpp" "src/daqd/PFP_KX7.cpp" )
	set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__PFP_KX7__" )
endif()
add_executable("daqd" ${DAQD_SOURCES})
if(HAVE_PFP_KX7)
	target_link_libraries (daqd -lpfp_kx7_api -lwdapi1160 )
endif()
add_executable("write_raw" "src/raw_data/write_raw.cpp" "src/raw_data/shm_raw.cpp")

execute_process(COMMAND root-config --incdir OUTPUT_VARIABLE ROOT_INCDIR)
string(STRIP ${ROOT_INCDIR} ROOT_INCDIR)
execute_process(COMMAND root-config --libs OUTPUT_VARIABLE ROOT_LIBS)
string(STRIP ${ROOT_LIBS} ROOT_LIBS)
execute_process(COMMAND root-config --cflags OUTPUT_VARIABLE ROOT_CXX_FLAGS)
string(STRIP ${ROOT_CXX_FLAGS} ROOT_CXX_FLAGS)

link_libraries(${Boost_LIBRARIES} ${PYTHON_LIBRARIES} "-liniparser")
add_library(shm_raw_py MODULE "src/raw_data/shm_raw.cpp" "src/raw_data/shm_raw_py.cpp")
set_target_properties(shm_raw_py PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/petsys OUTPUT_NAME shm_raw PREFIX "")

configure_file("src/petsys_py_lib/__init__.py" "petsys/__init__.py" COPYONLY)
configure_file("src/petsys_py_lib/tofpet2.py" "petsys/tofpet2.py" COPYONLY)
configure_file("src/petsys_py_lib/daqd.py" "petsys/daqd.py" COPYONLY)
configure_file("src/petsys_py_lib/bitarray_utils.py" "petsys/bitarray_utils.py" COPYONLY)
configure_file("src/petsys_py_lib/config.py" "petsys/config.py" COPYONLY)
configure_file("src/petsys_util/setSI53xx.py" "setSI53xx.py" COPYONLY)
configure_file("src/petsys_util/SI5326_config.txt" "SI5326_config.txt" COPYONLY)
configure_file("src/petsys_util/acquire_tdc_calibration" "acquire_tdc_calibration" COPYONLY)
configure_file("src/petsys_util/acquire_threshold_calibration" "acquire_threshold_calibration" COPYONLY)
configure_file("src/petsys_util/process_threshold_calibration" "process_threshold_calibration" COPYONLY)
configure_file("src/petsys_util/acquire_qdc_calibration" "acquire_qdc_calibration" COPYONLY)
configure_file("src/petsys_util/acquire_sipm_data" "acquire_sipm_data" COPYONLY)
configure_file("src/petsys_util/make_simple_sipm_bias_table" "make_simple_sipm_bias_table" COPYONLY)
configure_file("src/petsys_util/make_simple_disc_table" "make_simple_disc_table" COPYONLY)
configure_file("src/petsys_util/read_temperature_sensors" "read_temperature_sensors" COPYONLY)
configure_file("src/petsys_util/acquire_fetp_scan" "acquire_fetp_scan" COPYONLY)
configure_file("src/petsys_util/psqc_fetp.C" "psqc_fetp.C" COPYONLY)

include_directories( ${ROOT_INCDIR} )
link_libraries(${ROOT_LIBS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ROOT_CXX_FLAGS}")

set ( COMMON_SOURCES 
	"src/base/ThreadPool.cpp"
	"src/base/SystemConfig.cpp"
	"src/raw_data/RawReader.cpp"
	"src/base/Instrumentation.cpp"
	"src/base/CoarseSorter.cpp"
	"src/base/ProcessHit.cpp"
	"src/base/SimpleGrouper.cpp"
	"src/base/CoincidenceGrouper.cpp"
)
add_executable("process_tdc_calibration" "src/petsys_util/process_tdc_calibration.cpp" ${COMMON_SOURCES} )
add_executable("process_qdc_calibration" "src/petsys_util/process_qdc_calibration.cpp" ${COMMON_SOURCES} )
add_executable("convert_raw_to_root" "src/petsys_util/convert_raw_to_root.cpp" ${COMMON_SOURCES} )
add_executable("convert_raw_to_coincidence" "src/petsys_util/convert_raw_to_coincidence.cpp" ${COMMON_SOURCES} )
add_executable("convert_raw_to_singles" "src/petsys_util/convert_raw_to_singles.cpp" ${COMMON_SOURCES} )
add_executable("print_raw" "src/petsys_util/print_raw.cpp" ${COMMON_SOURCES} )
