#include "SystemConfig.hpp"
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <boost/regex.hpp>
#include <string.h>

extern "C" {
#include <iniparser.h>
}

using namespace PETSYS;

SystemConfig *SystemConfig::fromFile(const char *configFileName)
{
	SystemConfig *config = fromFile(configFileName, LOAD_ALL);
	return config;
}


SystemConfig *SystemConfig::fromFile(const char *configFileName, uint64_t mask)
{
	dictionary * configFile = iniparser_load(configFileName);

	SystemConfig *config = new SystemConfig();
	
	if(mask & LOAD_TDC_CALIBRATION != 0) {
		char *fn = iniparser_getstring(configFile, "main:tdc_calibration_table", NULL);
		if(fn == NULL) {
			fprintf(stderr, "ERROR: tdc_calibration_table not specified in '%s'\n", configFileName);
			exit(1);
		}
		loadTDCCalibration(config, fn);
	}
	
	iniparser_freedict(configFile);
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
		float t0, tB, m, p2;
		
		if(sscanf(line, "%d\t%u\t%u\t%u\t%u\t%c\t%f\t%f\t%f\t%f\n", 
			&portID, &slaveID, &chipID, &channelID, &tacID, &bStr,
			&t0, &tB, &m, &p2) != 10) continue;
		
		unsigned long gChannelID = 0;
		gChannelID |= channelID;
		gChannelID |= (chipID << 6);
		gChannelID |= (slaveID << 12);
		gChannelID |= (portID << 17);
		
		config->touchChannelConfig(gChannelID);
		ChannelConfig &channelConfig = config->getChannelConfig(gChannelID);
		
		TacConfig &tacConfig = (bStr == 'T') ? channelConfig.tac_T[tacID] : channelConfig.tac_E[tacID];
		
		tacConfig.t0 = t0;
		tacConfig.tB = tB;
		tacConfig.m = m;
		tacConfig.p2 = p2;
	}
}
