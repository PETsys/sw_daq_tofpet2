#include "SystemConfig.hpp"
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <boost/regex.hpp>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <string>
#include <boost/algorithm/string/replace.hpp>

extern "C" {
#include <iniparser/iniparser.h>
}

using namespace PETSYS;

SystemConfig *SystemConfig::fromFile(const char *configFileName)
{
	SystemConfig *config = fromFile(configFileName, LOAD_ALL);
	return config;
}


static void replace_variables(char *fn, const char *entry, char *cdir)
{
	std::string tmp(entry);
	
	boost::ireplace_all(tmp, "%PWD%", ".");
	boost::ireplace_all(tmp, "%CDIR%", cdir);
	boost::ireplace_all(tmp, "%HOME%",  getenv("HOME"));
	strncpy(fn, tmp.c_str(), PATH_MAX);
	
}

SystemConfig *SystemConfig::fromFile(const char *configFileName, u_int64_t mask)
{
	char *path = new char[PATH_MAX];
	strcpy(path, configFileName);
	char *cdir = dirname(path);
	
	char *fn = new char[PATH_MAX];
	
	dictionary * configFile = iniparser_load(configFileName);
	SystemConfig *config = new SystemConfig();
	

	config->hasTDCCalibration = false;
	if((mask & LOAD_TDC_CALIBRATION) != 0) {
		const char *entry = iniparser_getstring(configFile, "main:tdc_calibration_table", NULL);
		if(entry == NULL) {
			fprintf(stderr, "ERROR: tdc_calibration_table not specified in section 'main' of '%s'\n", configFileName);
			exit(1);
		}
		replace_variables(fn, entry, cdir);
		loadTDCCalibration(config, fn);
		config->hasTDCCalibration = true;
	}
	
	config->hasQDCCalibration = false;
	config->hasEnergyCalibration = false;
	if ((mask & LOAD_QDC_CALIBRATION) != 0) {
		const char *entry = iniparser_getstring(configFile, "main:qdc_calibration_table", NULL);
		if(entry == NULL) {
			fprintf(stderr, "ERROR: qdc_calibration_table not specified in section 'main' of '%s'\n", configFileName);
			exit(1);
		}
		replace_variables(fn, entry, cdir);
		loadQDCCalibration(config, fn);
		config->hasQDCCalibration = true;

		entry = iniparser_getstring(configFile, "main:energy_calibration_table", NULL);
		if(entry != NULL) {
			replace_variables(fn, entry, cdir);
			loadEnergyCalibration(config, fn);
			config->hasEnergyCalibration = true;
		}	
	}
	


	config->hasXYZ = false;
	if((mask & LOAD_MAPPING) != 0) {
		const char *entry = iniparser_getstring(configFile, "main:channel_map", NULL);
		if(entry == NULL) {
			fprintf(stderr, "ERROR: channel_map not specified in section 'main' of '%s'\n", configFileName);
			exit(1);
		}
		replace_variables(fn, entry, cdir);
		loadChannelMap(config, fn);
		config->hasXYZ = true;
		
		entry = iniparser_getstring(configFile, "main:trigger_map", NULL);
		if(entry == NULL) {
			fprintf(stderr, "ERROR: trigger_map not specified in section 'main' of '%s'\n", configFileName);
			exit(1);
		}
		replace_variables(fn, entry, cdir);
		loadTriggerMap(config, fn);
	}
	
	config->hasTimeOffsetCalibration = false;
	if ((mask & LOAD_TIMEALIGN_CALIBRATION) != 0) {
		const char *entry = iniparser_getstring(configFile, "main:time_offset_calibration_table", NULL);
		if(entry != NULL) {
			replace_variables(fn, entry, cdir);
			loadTimeOffsetCalibration(config, fn);
			config->hasTimeOffsetCalibration = true;
		}        
		else
			fprintf(stderr, "WARNING: time_offset_calibration_table not specified in section 'main' of '%s': timestamps of different channels may present offsets\n", configFileName);
	}

	// Load trigger configuration
	 config->sw_trigger_group_max_hits = iniparser_getint(configFile, "sw_trigger:group_max_hits", 64);
	 config->sw_trigger_group_min_energy = iniparser_getdouble(configFile, "sw_trigger:group_min_energy", -1E6);
	 config->sw_trigger_group_max_energy = iniparser_getdouble(configFile, "sw_trigger:group_max_energy", +1E6);
	 config->sw_trigger_group_max_distance = iniparser_getdouble(configFile, "sw_trigger:group_max_distance", 100.0);
	 config->sw_trigger_group_time_window = iniparser_getdouble(configFile, "sw_trigger:group_time_window", 20.0);
	 config->sw_trigger_coincidence_time_window =  iniparser_getdouble(configFile, "sw_trigger:coincidence_time_window", 2.0);
	
	iniparser_freedict(configFile);
	delete [] fn;
	delete [] path;
	return config;
}

void SystemConfig::touchChannelConfig(unsigned channelID)
{
	unsigned indexH = channelID / 4096;
	unsigned indexL= channelID % 4096;
	ChannelConfig *ptr = channelConfig[indexH];
	if(ptr == NULL) {
		ptr  = new ChannelConfig[4096];
		for(unsigned n = 0; n < 4096; n++) {
			ptr[n] = nullChannelConfig;
		}
		channelConfig[indexH] = ptr;
	}
}

SystemConfig::SystemConfig()
{
	hasTDCCalibration = false;
	hasQDCCalibration = false;
	hasXYZ = false;
	
	channelConfig = new ChannelConfig *[PATH_MAX];
	for(unsigned n = 0; n < PATH_MAX; n++) {
		channelConfig[n] = NULL;
	}
	
	/*
	 * Initialize nullChannelConfig
	 */
	for(unsigned n = 0; n < 4; n++) {
		nullChannelConfig.tac_T[n] = { 0, 0, 0, 0};
		nullChannelConfig.tac_E[n] = { 0, 0, 0, 0};
		nullChannelConfig.qac_Q[n] = { 0, 0, 0, 0, 0 };
		nullChannelConfig.eCal[n] = { 0, 0, 0, 0};
		nullChannelConfig.x = 0.0;
		nullChannelConfig.y = 0.0;
		nullChannelConfig.z = 0.0;
		nullChannelConfig.xi = 0;
		nullChannelConfig.yi = 0;
		nullChannelConfig.triggerRegion = -1;
		
		nullChannelConfig.t0 = 0.0;
	}
	
	coincidenceTriggerMap = new bool[MAX_TRIGGER_REGIONS * MAX_TRIGGER_REGIONS];
	for(int i = 0; i < MAX_TRIGGER_REGIONS * MAX_TRIGGER_REGIONS; i++)
		coincidenceTriggerMap[i] = false;
	
	multihitTriggerMap = new bool[MAX_TRIGGER_REGIONS * MAX_TRIGGER_REGIONS];
	for(int i = 0; i < MAX_TRIGGER_REGIONS * MAX_TRIGGER_REGIONS; i++)
		multihitTriggerMap[i] = false;
}

SystemConfig::~SystemConfig()
{
	delete [] multihitTriggerMap;
	delete [] coincidenceTriggerMap;
	
	for(unsigned n = 0; n < PATH_MAX; n++) {
		if(channelConfig[n] != NULL) {
			delete [] channelConfig[n];
		}
	}
	delete channelConfig;
}


// Remove comments and extra whitespace
// Leaving only fiels separated by a single \t character
static void normalizeLine(char *line) {
	std::string s = std::string(line);
	// Remove carriage return, from Windows written files
	s = boost::regex_replace(s, boost::regex("\r"), "");
	// Remove comments
	s = boost::regex_replace(s, boost::regex("\\s*#.*"), "");
	// Remove leading white space
	s = boost::regex_replace(s, boost::regex("^\\s+"), "");
	// Remove trailing whitespace
	s = boost::regex_replace(s, boost::regex("\\s+$"), "");
	// Normalize white space to tab
	s = boost::regex_replace(s, boost::regex("\\s+"), "\t");
	strcpy(line, s.c_str());
	
}

static unsigned MAKE_GID(unsigned long portID, unsigned long slaveID, unsigned long chipID, unsigned long channelID)
{
	unsigned long gChannelID = 0;
	gChannelID |= channelID;
	gChannelID |= (chipID << 6);
	gChannelID |= (slaveID << 12);
	gChannelID |= (portID << 17);
	return gChannelID;
}



void SystemConfig::loadTDCCalibration(SystemConfig *config, const char *fn)
{
	FILE *f = fopen(fn, "r");
	if(f == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fn, strerror(errno));
		exit(1);
	}
	char line[PATH_MAX];
	while(fscanf(f, "%[^\n]\n", line) == 1) {
		normalizeLine(line);
		if(strlen(line) == 0) continue;
		
		unsigned portID, slaveID, chipID, channelID, tacID;
		char bStr;
		float t0, a0, a1, a2;
		
		if(sscanf(line, "%d\t%u\t%u\t%u\t%u\t%c\t%f\t%f\t%f\t%f\n", 
			&portID, &slaveID, &chipID, &channelID, &tacID, &bStr,
			&t0, &a0, &a1, &a2) != 10) continue;
		
		unsigned long gChannelID = MAKE_GID(portID, slaveID, chipID, channelID);
		
		config->touchChannelConfig(gChannelID);
		ChannelConfig &channelConfig = config->getChannelConfig(gChannelID);
		
		TacConfig &tacConfig = (bStr == 'T') ? channelConfig.tac_T[tacID] : channelConfig.tac_E[tacID];
		
		tacConfig.t0 = t0;
		tacConfig.a0 = a0;
		tacConfig.a1 = a1;
		tacConfig.a2 = a2;
	}
	fclose(f);
}


void SystemConfig::loadQDCCalibration(SystemConfig *config, const char *fn)
{
	FILE *f = fopen(fn, "r");
	if(f == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fn, strerror(errno));
		exit(1);
	}
	char line[PATH_MAX];
	while(fscanf(f, "%[^\n]\n", line) == 1) {
		normalizeLine(line);
		if(strlen(line) == 0) continue;
		
		unsigned portID, slaveID, chipID, channelID, tacID;
		float p0, p1, p2, p3, p4, p5, p6, p7, p8, p9;
		
		if(sscanf(line, "%d\t%u\t%u\t%u\t%u\t\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n",
			&portID, &slaveID, &chipID, &channelID, &tacID,
			  &p0, &p1, &p2, &p3, &p4, &p5, &p6, &p7, &p8, &p9) != 15) continue;
		
		unsigned long gChannelID = MAKE_GID(portID, slaveID, chipID, channelID);
		
		config->touchChannelConfig(gChannelID);
		ChannelConfig &channelConfig = config->getChannelConfig(gChannelID);
		
		QacConfig &qacConfig = channelConfig.qac_Q[tacID];
		
		qacConfig.p0 = p0;
		qacConfig.p1 = p1;
		qacConfig.p2 = p2;
		qacConfig.p3 = p3;
		qacConfig.p4 = p4;
		qacConfig.p5 = p5;
		qacConfig.p6 = p6;
		qacConfig.p7 = p7;
		qacConfig.p8 = p8;
		qacConfig.p9 = p9;
	}
	fclose(f);
}

void SystemConfig::loadEnergyCalibration(SystemConfig *config, const char *fn)
{
	FILE *f = fopen(fn, "r");
	if(f == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fn, strerror(errno));
		exit(1);
	}
	char line[1024];
	while(fscanf(f, "%[^\n]\n", line) == 1) {
		normalizeLine(line);
		if(strlen(line) == 0) continue;
		
		unsigned portID, slaveID, chipID, channelID, tacID;
		float p0, p1, p2, p3;
		
		if(sscanf(line, "%d\t%u\t%u\t%u\t%u\t\t%f\t%f\t%f\t%f\n",
			&portID, &slaveID, &chipID, &channelID, &tacID,
			&p0, &p1, &p2, &p3) != 9) continue;
		
		unsigned long gChannelID = MAKE_GID(portID, slaveID, chipID, channelID);
		
		config->touchChannelConfig(gChannelID);
		ChannelConfig &channelConfig = config->getChannelConfig(gChannelID);
		
		EnergyConfig &eCal = channelConfig.eCal[tacID];
		
		eCal.p0 = p0;
		eCal.p1 = p1;
		eCal.p2 = p2;
		eCal.p3 = p3;
	}
	fclose(f);
}

void SystemConfig::loadTimeOffsetCalibration(SystemConfig *config, const char *fn)
{
	FILE *f = fopen(fn, "r");
	if(f == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fn, strerror(errno));
		exit(1);
	}
	char line[PATH_MAX];
	while(fscanf(f, "%[^\n]\n", line) == 1) {
		normalizeLine(line);
		if(strlen(line) == 0) continue;
		
		unsigned portID, slaveID, chipID;
                int channelID;
		float t0;
		
		if(sscanf(line, "%u\t%u\t%u\t%d\t%f\n", 
			&portID, &slaveID, &chipID, &channelID,
			&t0) != 5) continue;
               
		unsigned long gChannelID = MAKE_GID(portID, slaveID, chipID, channelID);
		
		config->touchChannelConfig(gChannelID);
		ChannelConfig &channelConfig = config->getChannelConfig(gChannelID);
				
		channelConfig.t0 = t0;
               
        }
	fclose(f);
}


void SystemConfig::loadChannelMap(SystemConfig *config, const char *fn)
{
	FILE *f = fopen(fn, "r");
	if(f == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fn, strerror(errno));
		exit(1);
	}
	char line[PATH_MAX];
	while(fscanf(f, "%[^\n]\n", line) == 1) {
		normalizeLine(line);
		if(strlen(line) == 0) continue;
		
		unsigned portID, slaveID, chipID, channelID;
		int region, xi, yi;
		float x, y, z;
		
		if(sscanf(line, "%u\t%u\t%u\t%u\t%d\t%d\t%d\t%f\t%f\t%f\n",
			&portID, &slaveID, &chipID, &channelID,
			&region, &xi, &yi,
			&x, &y, &z) != 10) continue;
		
		unsigned long gChannelID = MAKE_GID(portID, slaveID, chipID, channelID);
		
		config->touchChannelConfig(gChannelID);
		ChannelConfig &channelConfig = config->getChannelConfig(gChannelID);
		channelConfig.triggerRegion = region;
		channelConfig.xi = xi;
		channelConfig.yi = yi;
		channelConfig.x = x;
		channelConfig.y = y;
		channelConfig.z = z;
		
	}
	fclose(f);
}

void SystemConfig::loadTriggerMap(SystemConfig *config, const char *fn)
{
	FILE *f = fopen(fn, "r");
	if(f == NULL) {
		fprintf(stderr, "Could not open '%s' for reading: %s\n", fn, strerror(errno));
		exit(1);
	}
	char line[PATH_MAX];
	int lineNumber = 0;
	while(fscanf(f, "%[^\n]\n", line) == 1) {
		lineNumber += 1;
		normalizeLine(line);
		if(strlen(line) == 0) continue;
		
		int r1, r2;
		char c;
		if(sscanf(line, "%d\t%d\t%c\n", &r1, &r2, &c) != 3) {
			fprintf(stderr, "Error on '%s' line %d: line should have 3 entries\n", fn, lineNumber);
			exit(1);
		}
		
		if((r1 < 0) || (r1 >= MAX_TRIGGER_REGIONS) || (r2 < 0) || (r2 >= MAX_TRIGGER_REGIONS)) {
			fprintf(stderr, "Error on '%s' line %d: trigger region number must be between 0 and %d", fn, lineNumber, MAX_TRIGGER_REGIONS);
			exit(1);
		}
		c = toupper(c);
		if((c != 'M') && (c != 'C')) {
			fprintf(stderr, "Error on '%s' line %d: trigger type must be M or C", fn, lineNumber);
			exit(1);
		}
		
		config->coincidenceTriggerMap[r1 * MAX_TRIGGER_REGIONS + r2] = (c == 'C');
		config->coincidenceTriggerMap[r2 * MAX_TRIGGER_REGIONS + r1] = (c == 'C');
		config->multihitTriggerMap[r1 * MAX_TRIGGER_REGIONS + r2] = (c == 'M');
		config->multihitTriggerMap[r2 * MAX_TRIGGER_REGIONS + r1] = (c == 'M');
	}
	fclose(f);
}
