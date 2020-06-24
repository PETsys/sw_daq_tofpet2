# -*- coding: utf-8 -*-
import ConfigParser
import os.path
import re
from sys import stderr
from string import upper
import tofpet2b, tofpet2c
import bitarray
import math

LOAD_BIAS_CALIBRATION	= 0x00000001
LOAD_BIAS_SETTINGS 	= 0x00000002
LOAD_DISC_CALIBRATION	= 0x00000004
LOAD_DISC_SETTINGS	= 0x00000008
LOAD_MAP		= 0x00000010
LOAD_QDCMODE_MAP	= 0x00000020
LOAD_ALL		= 0xFFFFFFFF

APPLY_BIAS_OFF		= 0x0
APPLY_BIAS_PREBD	= 0x1
APPLY_BIAS_ON		= 0x2

def replace_variables(entry, cdir):
	tmp = entry
	tmp = re.sub("%PWD%", ".", tmp, re.I);
	tmp = re.sub("%CDIR%", cdir, tmp, re.I);
	tmp = re.sub("%HOME%", os.getenv("HOME"), tmp, re.I);
	return tmp

def ConfigFromFile(configFileName, loadMask=LOAD_ALL):
	cdir = os.path.dirname(configFileName)

	config = Config()
	configParser = ConfigParser.RawConfigParser()
	configParser.read(configFileName)
	
	if (loadMask & LOAD_BIAS_CALIBRATION) != 0:
		fn = configParser.get("main", "bias_calibration_table")
		fn = replace_variables(fn, cdir)
		t = readBiasCalibrationTable_tripplet_list(fn)
		config._Config__biasChannelCalibrationTable = t
		config._Config__loadMask |= LOAD_BIAS_CALIBRATION

	if (loadMask & LOAD_BIAS_SETTINGS) != 0:
		fn = configParser.get("main", "bias_settings_table")
		fn = replace_variables(fn, cdir)
		t = readSiPMBiasTable(fn)
		config._Config__biasChannelSettingsTable = t
		config._Config__loadMask |= LOAD_BIAS_SETTINGS

	if (loadMask & LOAD_DISC_CALIBRATION) != 0:
		fn = configParser.get("main", "disc_calibration_table")
		fn = replace_variables(fn, cdir)
		b, t = readDiscCalibrationsTable(fn)
		config._Config__asicChannelBaselineSettingsTable = b
		config._Config__asicChannelThresholdCalibrationTable = t
		config._Config__loadMask |= LOAD_DISC_CALIBRATION

	if (loadMask & LOAD_DISC_SETTINGS) != 0:
		fn = configParser.get("main", "disc_settings_table")
		fn = replace_variables(fn, cdir)
		t = readDiscSettingsTable(fn)
		config._Config__asicChannelThresholdSettingsTable = t
		config._Config__loadMask |= LOAD_DISC_SETTINGS

        if (loadMask & LOAD_QDCMODE_MAP) != 0:
                fn = configParser.get("main", "acquisition_mode_table")
		fn = replace_variables(fn, cdir)
		t = readQDCModeTable(fn)
		config._Config__asicChannelQDCModeTable = t
		config._Config__loadMask |= LOAD_QDCMODE_MAP


	# Load hw_trigger configuration IF hw_trigger section exists
	hw_trigger_config = { "type" : None }
	if (loadMask & LOAD_MAP) != 0:
		hw_trigger_config["threshold"] = configParser.getint("hw_trigger", "threshold")
		hw_trigger_config["pre_window"] = configParser.getint("hw_trigger", "pre_window")
		hw_trigger_config["post_window"] = configParser.getint("hw_trigger", "post_window")
		hw_trigger_config["coincidence_window"] = configParser.getint("hw_trigger", "coincidence_window")
		hw_trigger_config["single_fraction"] = configParser.getint("hw_trigger", "single_fraction")

		fn = configParser.get("main", "trigger_map")
		fn = replace_variables(fn, cdir)
		hw_trigger_config["regions"] = readTriggerMap(fn)

	config._Config__hw_trigger = hw_trigger_config

	# We always load ASIC parameters from config "asic" section, if they exist
	t = parseAsicParameters(configParser)
	config._Config__asicParameterTable = t


	return config

class Config:
	def __init__(self):
		self.__loadMask = 0x00000000
		self.__biasChannelCalibrationTable = {}
		self.__biasChannelSettingsTable = {}
		self.__asicChannelBaselineSettingsTable = {}
		self.__asicChannelThresholdCalibrationTable = {}
		self.__asicChannelThresholdSettingsTable = {}
                self.__asicChannelQDCModeTable = {}
		self.__asicParameterTable = {}
		self.__hw_trigger = None


	def loadToHardware(self, daqd, bias_enable=APPLY_BIAS_OFF, hw_trigger_enable=False, qdc_mode = "qdc"):
		#
		# Apply bias voltage settings
		# 
		hvdacHwConfig = daqd.get_hvdac_config()
		# Set all bias channels ~1V (off, but avoid from from amplifier to SiPM)
		for key in hvdacHwConfig.keys():
			# Set HVDAC to ~1 V, to avoid flow back from amplifier through SiPM
				hvdacHwConfig[key] = int(round(1.0 * 2**14 / (50 * 2.048)))

		if bias_enable == APPLY_BIAS_PREBD or bias_enable == APPLY_BIAS_ON:
			assert (self.__loadMask & LOAD_BIAS_CALIBRATION) != 0
			assert (self.__loadMask & LOAD_BIAS_SETTINGS) != 0
			for key in self.__biasChannelSettingsTable.keys():
				offset, prebd, bd, over = self.getBiasChannelDefaultSettings(key)
				if bias_enable == APPLY_BIAS_PREBD:
					Vset = offset + prebd
				else:
					Vset = offset + bd + over
				
				dacSet = self.mapBiasChannelVoltageToDAC(key, Vset)
				hvdacHwConfig[key] = dacSet
				
		daqd.set_hvdac_config(hvdacHwConfig)

		
		asicsConfig = daqd.getAsicsConfig()

		# Apply ASIC parameters from file
		for (gc, key), value in self.__asicParameterTable.items():
			for ac in asicsConfig.values():
				if gc == "global":
					ac.globalConfig.setValue(key, value)
				else:
					for cc in ac.channelConfig:
						cc.setValue(key, value)

		# Apply discriminator baseline calibrations
		if (self.__loadMask & LOAD_DISC_CALIBRATION) != 0:
			for portID, slaveID, chipID in asicsConfig.keys():
				ac = asicsConfig[(portID, slaveID, chipID)]
				for channelID in range(64):
					baseline_t, baseline_e = self.getAsicChannelDefaultBaselineSettings((portID, slaveID, chipID, channelID))
					cc = ac.channelConfig[channelID]
					cc.setValue("baseline_t", baseline_t)
					cc.setValue("baseline_e", baseline_e)

		# Apply discriminator settings and energy acquisition mdoe 
                for portID, slaveID, chipID in asicsConfig.keys():
                        ac = asicsConfig[(portID, slaveID, chipID)]
                        for channelID in range(64):
                                cc = ac.channelConfig[channelID]
                                if (self.__loadMask & LOAD_DISC_SETTINGS) != 0:
                                        vth_t1, vth_t2, vth_e = self.getAsicChannelDefaultThresholds((portID, slaveID, chipID, channelID))
                                        vth_t1 = self.mapAsicChannelThresholdToDAC((portID, slaveID, chipID, channelID), "vth_t1", vth_t1)
                                        vth_t2 = self.mapAsicChannelThresholdToDAC((portID, slaveID, chipID, channelID), "vth_t2", vth_t2)
                                        vth_e = self.mapAsicChannelThresholdToDAC((portID, slaveID, chipID, channelID), "vth_e", vth_e)
                                        cc.setValue("vth_t1", vth_t1)
                                        cc.setValue("vth_t2", vth_t2)
                                        cc.setValue("vth_e", vth_e)

                                        cc.setValue("trigger_mode_1", 0b00)


				if qdc_mode == "tot":
					channel_qdc_mode = "tot"
				elif qdc_mode == "qdc":
					channel_qdc_mode = "qdc"
                                else:
                                        channel_qdc_mode =  self.getAsicChannelQDCMode((portID, slaveID, chipID, channelID))
                                                               
                                if channel_qdc_mode == "tot":
                                        cc.setValue("qdc_mode", 0)
                                        cc.setValue("intg_en", 0)
                                        cc.setValue("intg_signal_en", 0)
                                else:
                                        cc.setValue("qdc_mode", 1)
                                        cc.setValue("intg_en", 1)
                                        cc.setValue("intg_signal_en", 1)
                              
                                        
                                        
		daqd.setAsicsConfig(asicsConfig)


		daqd.disableCoincidenceTrigger()
		if hw_trigger_enable:
			if daqd.getTriggerUnit() is not None:
				
				# Trigger setup
				portID, slaveID = daqd.getTriggerUnit()
				daqd.write_config_register(portID, slaveID, 1, 0x0602, 0b1)
				daqd.write_config_register(portID, slaveID, 3, 0x0606, self.__hw_trigger["coincidence_window"])
				daqd.write_config_register(portID, slaveID, 2, 0x0608, self.__hw_trigger["pre_window"])
				daqd.write_config_register(portID, slaveID, 4, 0x060A, self.__hw_trigger["post_window"])
				daqd.write_config_register(portID, slaveID, 10, 0x060C, 0) # Single fraction
				
				hw_trigger_regions = self.__hw_trigger["regions"]
				nRegions = daqd.read_config_register(portID, slaveID, 16, 0x0600)
				bytes_per_region = int(math.ceil(nRegions / 8.0))
				bits_per_region = 8 * bytes_per_region
				for r1 in range(nRegions):
					region_mask = bitarray.bitarray([ 0 for n in range(bits_per_region) ], endian="little")
					for r2 in range(nRegions):
						if (r1,r2) in hw_trigger_regions or (r2,r1) in hw_trigger_regions:
							region_mask[r2] = 1

					region_data = [ ord(u) for u in region_mask.tobytes() ]
					daqd.write_mem_ctrl(portID, slaveID, 6, 8, r1 * bytes_per_region, region_data)
				
				# FEB/D setup
				for portID, slaveID in daqd.getActiveFEBDs():
					daqd.write_config_register(portID, slaveID, 10, 0x0604,  self.__hw_trigger["threshold"])
			
			# WARNING missing DAQ trigger setup

		
			

		return None
	
	def getCalibratedBiasChannels(self):
		return self.__biasChannelCalibrationTable.keys()
	
	def getBiasChannelDefaultSettings(self, key):
		return self.__biasChannelSettingsTable[key]
		
	def mapBiasChannelVoltageToDAC(self, key, voltage):
		# Linear interpolation on closest neighbours
		y = voltage
		xy_ = self.__biasChannelCalibrationTable[key]
		for i in range(1, len(xy_)):
			x1, y1, _ = xy_[i-1]
			x2, y2, _ = xy_[i]
			if y2 >= y: break

		# y = m*x+b
		m = (y2 - y1)/(x2 - x1)
		b = y1 - m*x1
		x = (y-b)/m
		return int(round(x))
		
	def getCalibratedDiscChannels(self):
		return self.__asicChannelBaselineSettingsTable.keys()
	
	def getAsicChannelDefaultBaselineSettings(self, key):
		return self.__asicChannelBaselineSettingsTable[key]
		
	def getAsicChannelDefaultThresholds(self, key):
		return self.__asicChannelThresholdSettingsTable[key]
        
	def getAsicChannelQDCMode(self, key):
                return self.__asicChannelQDCModeTable[key]
			
	def mapAsicChannelThresholdToDAC(self, key, vth_str, value):
		vth_t1, vth_t2, vth_e = self.__asicChannelThresholdCalibrationTable[key]
		tmp = { "vth_t1" : vth_t1, "vth_t2" : vth_t2, "vth_e" : vth_e }
		return int( tmp[vth_str] - value)
		

def toInt(s):
	s = s.upper()
	if s[0:2] == "0X":
		return int(s[2:], base=16)
	elif s[0:2] == "0B":
		return int(s[2:], base=2)
	else:
		return int(s)

def parseAsicParameters(configParser):
	if not configParser.has_section("asic_parameters"):
		return {}
	
	t = {}
	gk = set(tofpet2b.AsicGlobalConfig().getKeys() + tofpet2c.AsicGlobalConfig().getKeys())
	ck = set(tofpet2b.AsicChannelConfig().getKeys() + tofpet2c.AsicChannelConfig().getKeys())
	for key, value in configParser.items("asic_parameters"):
		if key[0:7] == "global.":
			k = key[7:]
			if k not in gk:
				print "Invalid ASIC parameter: '%s'" % key
				exit(1)
			else:
				t[("global", k)] = toInt(value)
		elif key[0:8] == "channel.":
			k = key[8:]
			if k not in ck:
				print "Invalid ASIC parameter: '%s'" % key
				exit(1)
			else:
				t[("channel", k)] = toInt(value)
		else:
			print "Invalid ASIC parameter: '%s'" % key
			exit(1)
	return t


def normalizeAndSplit(l):
	l = re.sub("\s*#.*", "", l)	# Remove comments
	l = re.sub('^\s*', '', l)	# Remove leading white space
	l = re.sub('\s*$', '', l)	# Remove trailing whitespace
	l = re.sub('\s+', '\t', l)	# Normalize whitespace to tabs
	l = re.sub('\r', '', l)		# Remove \r
	l = re.sub('\n', '', l)		# Remove \l
	l = l.split('\t')
	return l

def readBiasCalibrationTable_tripplet_list(fn):
	f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, channelID = [ int(v) for v in l[0:3] ]
		key = (portID, slaveID, channelID)
		if not c.has_key(key):
			c[key] = []
		dac_set = int(l[3])
		v_meas = float(l[4])
		adc_meas = int(l[5])
		
		c[key].append((dac_set, v_meas, adc_meas))
	return c
	f.close()

def readBiasCalibrationTable_table(fn):
	f = open(fn)
	c = {}
	x = []
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue

		if x == []:
			x = [ int(v) for v in l]
		else:
			portID, slaveID, channelID = [ int(v) for v in l[0:3] ]
			y = [ float(v) for v in l[3:] ]
			assert len(x) == len(y)
			xyz = [ (x[i], y[i], 0) for i in range(len(x)) ]
			c[(portID, slaveID, channelID)] = xyz
	f.close()
	return c

def readSiPMBiasTable(fn):
	f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, channelID = [ int(v) for v in l[0:3] ]
		c[(portID, slaveID, channelID)] = [ float(v) for v in l[3:7] ]
	f.close()
	return c

def readDiscCalibrationsTable(fn):
	f = open(fn)
	c_b = {}
	c_t = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, chipID, channelID = [ int(v) for v in l[0:4] ]
		c_b[(portID, slaveID, chipID, channelID)] = [ int(v) for v in l[4:6] ]
		c_t[(portID, slaveID, chipID, channelID)] = [ float(v) for v in l[6:9] ]
	f.close()
	return c_b, c_t

def readDiscSettingsTable(fn):
	f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, chipID, channelID = [ int(v) for v in l[0:4] ]
		c[(portID, slaveID, chipID, channelID)] = [ float(v) for v in l[4:7] ]
	f.close()
	return c

def readQDCModeTable(fn):
	f = open(fn)
	c = {}
        ln = 0
	for l in f:
                ln += 1
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, chipID, channelID = [ int(v) for v in l[0:4] ]
		c[(portID, slaveID, chipID, channelID)] =  l[4:5][0]
                if c[(portID, slaveID, chipID, channelID)] not in ['tot', 'qdc']:
			print "Error in '%s' line %d: mode must be 'qdc' or 'tot'\n" % (fn, ln)
			exit(1)
	f.close()
	return c


def readTriggerMap(fn):
	f = open(fn)
	triggerMap = set()
	ln = 0
	for l in f:
		ln += 1
		l = normalizeAndSplit(l)
		if l == ['']: continue
		r1 = int(l[0])
		r2 = int(l[1])
		c = upper(l[2])
		if c not in ['M', 'C']:
			print "Error in '%s' line %d: type must be 'M' or 'C'\n" % (fn, ln)
			exit(1)

		if c == 'C':
			triggerMap.add((r1, r2))
	f.close()
	return triggerMap


def readTopologyMap(fn):
        f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, chipID = [ int(v) for v in l[0:3] ]
                c[(portID, slaveID, chipID)] = [ v for v in l[3:4] ]
        
        f.close()
	return c
