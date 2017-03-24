#include "SystemConfig.hpp"
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <boost/regex.hpp>
#include <string.h>
#include <libgen.h>

extern "C" {
#include <iniparser.h>
}

using namespace PETSYS;

SystemConfig *SystemConfig::fromFile(const char *configFileName)
{
	SystemConfig *config = fromFile(configFileName, LOAD_ALL);
	return config;
}


static void make_absolute(char *fn, char *entry, char *dn)
{
	if(entry[0] == '/') {
			strcpy(fn, entry);
	}
	else {
		sprintf(fn, "%s/%s", dn, entry);
	}
}

SystemConfig *SystemConfig::fromFile(const char *configFileName, uint64_t mask)
{
	char *path = new char[1024];
	strcpy(path, configFileName);
	char *dn = dirname(path);
	
	char *fn = new char[1024];
	
	dictionary * configFile = iniparser_load(configFileName);
	SystemConfig *config = new SystemConfig();
	
	config->hasTDCCalibration = false;
	if((mask & LOAD_TDC_CALIBRATION) != 0) {
		char *entry = iniparser_getstring(configFile, "main:tdc_calibration_table", NULL);
		if(entry == NULL) {
			fprintf(stderr, "ERROR: tdc_calibration_table not specified in section 'main' of '%s'\n", configFileName);
			exit(1);
		}
		make_absolute(fn, entry, dn);
		loadTDCCalibration(config, fn);
		config->hasTDCCalibration = true;
	}
	
	config->hasQDCCalibration = false;
	if ((mask & LOAD_QDC_CALIBRATION) != 0) {
		char *entry = iniparser_getstring(configFile, "main:qdc_calibration_table", NULL);
		if(entry == NULL) {
			fprintf(stderr, "ERROR: qdc_calibration_table not specified in section 'main' of '%s'\n", configFileName);
			exit(1);
		}
		make_absolute(fn, entry, dn);
		loadQDCCalibration(config, fn);
		config->hasQDCCalibration = true;
	}
	
	iniparser_freedict(configFile);
	delete [] fn;
	delete [] path;
	return config;
}


bool SystemConfig::useTDCCalibration()
{
	return hasTDCCalibration;
}

bool SystemConfig::useQDCCalibration()
{
	return hasQDCCalibration;
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
	channelConfig = new ChannelConfig *[1024];
	for(unsigned n = 0; n < 1024; n++) {
		channelConfig[n] = NULL;
	}
	
	/*
	 * Initialize nullChannelConfig
	 */
	for(unsigned n = 0; n < 4; n++) {
		nullChannelConfig.tac_T[n] = { 0, 0, 0, 0};
		nullChannelConfig.tac_E[n] = { 0, 0, 0, 0};
		nullChannelConfig.qac_Q[n] = { 0, 0, 0, 0, 0 };
		
		nullChannelConfig.x = 0.0;
		nullChannelConfig.y = 0.0;
		nullChannelConfig.z = 0.0;
		nullChannelConfig.xi = 0;
		nullChannelConfig.yi = 0;
		nullChannelConfig.triggerRegion = 0;
		
		nullChannelConfig.t0 = 0.0;
	}
}

SystemConfig::~SystemConfig()
{
	for(unsigned n = 0; n < 1024; n++) {
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
	char line[1024];
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
	char line[1024];
	while(fscanf(f, "%[^\n]\n", line) == 1) {
		normalizeLine(line);
		if(strlen(line) == 0) continue;
		
		unsigned portID, slaveID, chipID, channelID, tacID;
		float p0, p1, p2, p3, p4;
		
		if(sscanf(line, "%d\t%u\t%u\t%u\t%u\t\t%f\t%f\t%f\t%f\t%f\n",
			&portID, &slaveID, &chipID, &channelID, &tacID,
			&p0, &p1, &p2, &p3, &p4) != 10) continue;
		
		unsigned long gChannelID = MAKE_GID(portID, slaveID, chipID, channelID);
		
		config->touchChannelConfig(gChannelID);
		ChannelConfig &channelConfig = config->getChannelConfig(gChannelID);
		
		QacConfig &qacConfig = channelConfig.qac_Q[tacID];
		
		qacConfig.p0 = p0;
		qacConfig.p1 = p1;
		qacConfig.p2 = p2;
		qacConfig.p3 = p3;
		qacConfig.p4 = p4;
	}
	fclose(f);
}
