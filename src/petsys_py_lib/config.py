# -*- coding: utf-8 -*-
import ConfigParser
import os.path
import re
from sys import stderr
from string import upper
import tofpet2

LOAD_AD5535_CALIBRATION	= 0x00000001
LOAD_SIPM_BIAS 		= 0x00000002
LOAD_DISC_CALIBRATION	= 0x00000004
LOAD_DISC_SETTINGS	= 0x00000008
LOAD_ALL		= 0xFFFFFFFF

APPLY_BIAS_OFF		= 0x0
APPLY_BIAS_PREBD	= 0x1
APPLY_BIAS_ON		= 0x2


class SiPMBiasConfigEntry:
	def __init__(self, Vprebd, Vbd, Vover):
		self.Vprebd = Vprebd
		self.Vbd = Vbd
		self.Vover = Vover

class DiscriminatorCalibrationEntry:
	def __init__(self, line):
		self.baseline_t = int(line[0])
		self.zero_t1 = float(line[1])
		self.noise_t1 = float(line[2])
		self.dark_width_t1 = float(line[3])
		self.rate_10k_t1 = float(line[4])
		self.rate_20k_t1 = float(line[5])
		self.rate_50k_t1 = float(line[6])
		self.rate_100k_t1 = float(line[7])

		self.zero_t2 = float(line[8])
		self.noise_t2 = float(line[9])
		self.rate_10k_t2 = float(line[10])
		self.rate_20k_t2 = float(line[11])
		self.rate_50k_t2 = float(line[12])
		self.rate_100k_t2 = float(line[13])

		self.baseline_e = int(line[14])
		self.zero_e = float(line[15])
		self.noise_e = float(line[16])
		self.rate_10k_e = float(line[17])
		self.rate_20k_e = float(line[18])
		self.rate_50k_e = float(line[19])
		self.rate_100k_e = float(line[20])

class DiscriminatorConfigEntry:
	def __init__(self, vth_t1, vth_t2, vth_e):
		self.vth_t1 = vth_t1
		self.vth_t2 = vth_t2
		self.vth_e = vth_e


def ConfigFromFile(configFileName, loadMask=LOAD_ALL):
	dn = os.path.dirname(configFileName)

	config = Config()
	configParser = ConfigParser.SafeConfigParser()
	configParser.read(configFileName)
	
	if (loadMask & LOAD_AD5535_CALIBRATION) != 0:
		fn = configParser.get("main", "ad5535_calibration_table")
		if not os.path.isabs(fn):
			fn = os.path.join(dn, fn)
		t = readAD5535CalibrationTable(fn)
		config.ad5535CalibrationTable = t
		config._Config__loadMask |= LOAD_AD5535_CALIBRATION

	if (loadMask & LOAD_SIPM_BIAS) != 0:
		fn = configParser.get("main", "sipm_bias_table")
		if not os.path.isabs(fn):
			fn = os.path.join(dn, fn)
		t = readSiPMBiasTable(fn)
		config.sipmBiasTable = t
		config._Config__loadMask |= LOAD_SIPM_BIAS

	if (loadMask & LOAD_DISC_CALIBRATION) != 0:
		fn = configParser.get("main", "disc_calibration_table")
		if not os.path.isabs(fn):
			fn = os.path.join(dn, fn)
		t = readDiscCalibrationsTable(fn)
		config.discCalibrationTable = t
		config._Config__loadMask |= LOAD_DISC_CALIBRATION

	if (loadMask & LOAD_DISC_SETTINGS) != 0:
		fn = configParser.get("main", "disc_settings_table")
		if not os.path.isabs(fn):
			fn = os.path.join(dn, fn)
		t = readDiscSettingsTable(fn)
		# We just read a file with settings relative to baseline
		# Not we need to calculate the threshold DAC value
		for key in config.discCalibrationTable.keys():
			a = config.discCalibrationTable[key]
			b = t[key]
			t[key] = DiscriminatorConfigEntry(a.zero_t1 - b.vth_t1, a.zero_t2 - b.vth_t2, a.zero_e - b.vth_e)

		config.discConfigTable = t
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
		if not os.path.isabs(fn):
			fn = os.path.join(dn, fn)
		hw_trigger_config["regions"] = readTriggerMap(fn)

	config._Config__hw_trigger = hw_trigger_config

	# We always load ASIC parameters from config "asic" section, if they exist
	t = parseAsicParameters(configParser)
	config._Config__asicParameterTable = t


	return config

class Config:
	def __init__(self):
		self.__loadMask = 0x00000000
		self.ad5535CalibrationTable = {}
		self.sipmBiasTable = {}
		self.discCalibrationTable = {}
		self.discConfigTable = {}
		self.__asicParameterTable = {}
		self.__hw_trigger = None

	def loadToHardware(self, daqd, bias_enable=APPLY_BIAS_OFF, hw_trigger_enable=False):
		#
		# Apply bias voltage settings
		# 
		ad5535HwConfig = daqd.getAD5535Config()
		# Set all bias channels ~1V (off, but avoid from from amplifier to SiPM)
		for key in ad5535HwConfig.keys():
			# Set AD5535 to ~1 V, to avoid flow back from amplifier through SiPM
				ad5535HwConfig[key] = int(round(1.0 * 2**14 / (50 * 2.048)))

		if bias_enable == APPLY_BIAS_PREBD or bias_enable == APPLY_BIAS_ON:
			assert (self.__loadMask & LOAD_AD5535_CALIBRATION) != 0
			assert (self.__loadMask & LOAD_SIPM_BIAS) != 0
			for key in self.sipmBiasTable.keys():
				entry = self.sipmBiasTable[key]
				if bias_enable == APPLY_BIAS_PREBD:
					Vset = entry.Vprebd
				else:
					Vset = entry.Vbd + entry.Vover
				
				ad5535HwConfig[key] = self.__ad5535VoltageToDAC(key, Vset)
				
		daqd.setAD5535Config(ad5535HwConfig)

		
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
					a = self.discCalibrationTable[(portID, slaveID, chipID, channelID)]
					cc = ac.channelConfig[channelID]
					cc.setValue("baseline_t", a.baseline_t)
					cc.setValue("baseline_e", a.baseline_e)

		# Apply discriminator settings
		if (self.__loadMask & LOAD_DISC_SETTINGS) != 0:
			for portID, slaveID, chipID in asicsConfig.keys():
				ac = asicsConfig[(portID, slaveID, chipID)]
				for channelID in range(64):
					b = self.discConfigTable[(portID, slaveID, chipID, channelID)]
					cc = ac.channelConfig[channelID]
					cc.setValue("vth_t1", int(b.vth_t1))
					cc.setValue("vth_t2", int(b.vth_t2))
					cc.setValue("vth_e", int(b.vth_e))


		daqd.setAsicsConfig(asicsConfig)


		# Disable builtin hardware trigger (if it exists)
		for portID, slaveID in daqd.getActiveFEBDs():
			daqd.writeFEBDConfig(portID, slaveID, 0, 16, 0x0F000000)
			daqd.writeFEBDConfig(portID, slaveID, 0, 15, 0)
			daqd.writeFEBDConfig(portID, slaveID, 0, 17, 0)

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
				daqd.writeFEBDConfig(portID, slaveID, 0, 16,value)

				value = self.__hw_trigger["threshold"]
				daqd.writeFEBDConfig(portID, slaveID, 0, 15, value)

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
				daqd.writeFEBDConfig(portID, slaveID, 0, 17, value)
			

		return None
	
	def __ad5535VoltageToDAC(self, key, y):
		# Linear interpolation on closest neighbours
		xy = self.ad5535CalibrationTable[key]
		for i in range(1, len(xy)):
			x1, y1 = xy[i-1]
			x2, y2 = xy[i]
			if y2 >= y: break

		# y = m*x+b
		m = (y2 - y1)/(x2 - x1)
		b = y1 - m*x1
		x = (y-b)/m
		return int(round(x))

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
	gk = tofpet2.AsicGlobalConfig().getKeys()
	ck = tofpet2.AsicChannelConfig().getKeys()
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

def readAD5535CalibrationTable(fn):
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
			xy = [ (x[i], y[i]) for i in range(len(x)) ]
			c[(portID, slaveID, channelID)] = xy
	return c

def readSiPMBiasTable(fn):
	f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue

		portID, slaveID, channelID = [ int(v) for v in l[0:3] ]
		Vprebd, Vbd, Vover = [ float(v) for v in l[3:6] ]
		c[(portID, slaveID, channelID)] = SiPMBiasConfigEntry(Vprebd, Vbd, Vover)
	return c

def readDiscCalibrationsTable(fn):
	f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue

		portID, slaveID, chipID, channelID = [ int(v) for v in l[0:4] ]
		c[(portID, slaveID, chipID, channelID)] = DiscriminatorCalibrationEntry(l[4:])
	return c

def readDiscSettingsTable(fn):
	f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, chipID, channelID = [ int(v) for v in l[0:4] ]
		vth_t1, vth_t2, vth_e = [ float(v) for v in l[4:7] ]
		c[(portID, slaveID, chipID, channelID)] = DiscriminatorConfigEntry(vth_t1, vth_t2, vth_e)
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
	return triggerMap
