# -*- coding: utf-8 -*-
import ConfigParser
import os.path
import re
from sys import stderr
from string import upper
import tofpet2b, tofpet2c

LOAD_BIAS_CALIBRATION	= 0x00000001
LOAD_BIAS_SETTINGS 	= 0x00000002
LOAD_DISC_CALIBRATION	= 0x00000004
LOAD_DISC_SETTINGS	= 0x00000008
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


	# Load hw_trigger configuration IF hw_trigger section exists
	hw_trigger_config = { "type" : None }
	if configParser.has_section("hw_trigger"):
	
		hw_trigger_config["type"] = configParser.get("hw_trigger", "type")
		if hw_trigger_config["type"] not in [ "builtin" ]:
			stderr.write("Error: type must be 'builtin'\n")
			exit(1)

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
		self.__asicParameterTable = {}
		self.__hw_trigger = None


	def loadToHardware(self, daqd, bias_enable=APPLY_BIAS_OFF, hw_trigger_enable=False):
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

		# Apply discriminator settings
		if (self.__loadMask & LOAD_DISC_SETTINGS) != 0:
			for portID, slaveID, chipID in asicsConfig.keys():
				ac = asicsConfig[(portID, slaveID, chipID)]
				for channelID in range(64):
					vth_t1, vth_t2, vth_e = self.getAsicChannelDefaultThresholds((portID, slaveID, chipID, channelID))
					cc = ac.channelConfig[channelID]
					
					vth_t1 = self.mapAsicChannelThresholdToDAC((portID, slaveID, chipID, channelID), "vth_t1", vth_t1)
					vth_t2 = self.mapAsicChannelThresholdToDAC((portID, slaveID, chipID, channelID), "vth_t2", vth_t2)
					vth_e = self.mapAsicChannelThresholdToDAC((portID, slaveID, chipID, channelID), "vth_e", vth_e)
					cc.setValue("vth_t1", vth_t1)
					cc.setValue("vth_t2", vth_t2)
					cc.setValue("vth_e", vth_e)


		daqd.setAsicsConfig(asicsConfig)


		# Disable builtin hardware trigger (if it exists)
		for portID, slaveID in daqd.getActiveFEBDs():
			daqd.write_config_register(portID, slaveID, 64, 0xD002, 0x0F000000)
			daqd.write_config_register(portID, slaveID, 64, 0x0501, 0)
			daqd.write_config_register(portID, slaveID, 64, 0xD000, 0)

		if hw_trigger_enable:
			if self.__hw_trigger["type"] == None:
				raise "Cannot enable HW trigger: hw_trigger section was not present in configuration file"

			if self.__hw_trigger["type"] == "builtin":
				portID, slaveID = (0, 0) # There should be only one FEB/D connected via Ethernet when we use the builtin trigger
				nRegions = 2**daqd.readFEBDConfig(portID, slaveID, 0, 21)

				value = 0
				value |= self.__hw_trigger["pre_window"]
				value |= self.__hw_trigger["post_window"] << 4
				value |= self.__hw_trigger["coincidence_window"] << 20
				daqd.write_config_register(portID, slaveID, 64, 0xD002, value)

				value = self.__hw_trigger["threshold"]
				daqd.write_config_register(portID, slaveID, 64, 0x0501, value)

				value = 0
				hw_trigger_regions = self.__hw_trigger["regions"]
				for r1 in range(nRegions):
					# Set set bit to 1
					value |= (1 << (r1*nRegions + r1))
					for r2 in range(nRegions):
						if (r1,r2) in hw_trigger_regions or (r2,r1) in hw_trigger_regions:
							# Enable coincidences between r1 and r2
							value |= (1 << (r1*nRegions + r2))
							value |= (1 << (r2*nRegions + r1))
				daqd.write_config_register(portID, slaveID, 64, 0xD000, value)
			

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
			stdout.write("Error in '%s' line %d: type must be 'M' or 'C'\n" % (fn, ln))
			exit(1)

		if c == 'C':
			triggerMap.add((r1, r2))
	f.close()
	return triggerMap
