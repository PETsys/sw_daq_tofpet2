# -*- coding: utf-8 -*-
import ConfigParser
import os.path
import re
import tofpet2

LOAD_AD5535_CALIBRATION	= 0x00000001
LOAD_SIPM_BIAS 		= 0x00000002
LOAD_DISC_CALIBRATION	= 0x00000004
LOAD_ALL		= 0xFFFFFFFF

APPLY_BIAS_OFF		= 0x0
APPLY_BIAS_PREBD	= 0x1
APPLY_BIAS_ON		= 0x2


class SiPMBiasConfigEntry:
	def __init__(self, Vprebd, Vbd, Vover):
		self.Vprebd = Vprebd
		self.Vbd = Vbd
		self.Vover = Vover

class DiscriminatorConfigEntry:
	def __init__(self, baseline_t, zero_t1, noise_t1, zero_t2, noise_t2, baseline_e, zero_e, noise_e):
		self.baseline_t = baseline_t
		self.zero_t1 = zero_t1
		self.noise_t1 = noise_t1
		self.zero_t2 = zero_t2
		self.noise_t2 = noise_t2
		self.baseline_e = baseline_e
		self.zero_e = zero_e
		self.noise_e = noise_e


def ConfigFromFile(configFileName, loadMask=LOAD_ALL):
	dn = os.path.dirname(configFileName)

	config = Config()
	configParser = ConfigParser.SafeConfigParser()
	configParser.read(configFileName)
	
	if loadMask & LOAD_AD5535_CALIBRATION != 0:
		fn = configParser.get("main", "ad5535_calibration_table")
		if not os.path.isabs(fn):
			fn = os.path.join(dn, fn)
		t = readAD5535CalibrationTable(fn)
		config._Config__ad5535CalibrationTable = t
		config._Config__loadMask |= LOAD_AD5535_CALIBRATION

	if loadMask & LOAD_SIPM_BIAS != 0:
		fn = configParser.get("main", "sipm_bias_table")
		if not os.path.isabs(fn):
			fn = os.path.join(dn, fn)
		t = readSiPMBiasTable(fn)
		config.sipmBiasTable = t
		config._Config__loadMask |= LOAD_SIPM_BIAS

	if loadMask & LOAD_DISC_CALIBRATION != 0:
		fn = configParser.get("main", "disc_calibration_table")
		if not os.path.isabs(fn):
			fn = os.path.join(dn, fn)
		t = readDiscCalibrationsTable(fn)
		config.discCalibrationTable = t
		config._Config__loadMask |= LOAD_DISC_CALIBRATION

	# We always load ASIC parameters from config "asic" section, if they exist
	t = parseAsicParameters(configParser)
	config._Config__asicParameterTable = t


	return config

class Config:
	def __init__(self):
		self.__loadMask = 0x00000000
		self.__ad5535CalibrationTable = {}
		self.sipmBiasTable = {}
		self.discCalibrationTable = {}
		self.__asicParameterTable = {}

	def loadToHardware(self, daqd, biasMode):
		#
		# Apply bias voltage settings
		# 
		ad5535HwConfig = daqd.getAD5535Config()
		# Set all bias channels ~1V (off, but avoid from from amplifier to SiPM)
		for key in ad5535HwConfig.keys():
			# Set AD5535 to ~1 V, to avoid flow back from amplifier through SiPM
				ad5535HwConfig[key] = int(round(1.0 * 2**14 / (50 * 2.048)))

		if biasMode == APPLY_BIAS_PREBD or biasMode == APPLY_BIAS_ON:
			assert self.__loadMask & LOAD_AD5535_CALIBRATION != 0
			assert self.__loadMask & LOAD_SIPM_BIAS != 0
			for key in self.sipmBiasTable.keys():
				entry = self.sipmBiasTable[key]
				if biasMode == APPLY_BIAS_PREBD:
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

		#
		# Apply discriminator calibrations
		if self.__loadMask & LOAD_DISC_CALIBRATION:
			for portID, slaveID, chipID in asicsConfig.keys():
				ac = asicsConfig[(portID, slaveID, chipID)]
				for channelID in range(64):
					entry = self.discCalibrationTable[(portID, slaveID, chipID, channelID)]
					cc = ac.channelConfig[channelID]
					cc.setValue("baseline_t", entry.baseline_t)
					cc.setValue("baseline_e", entry.baseline_e)
					cc.setValue("vth_t1", int(entry.zero_t1 - 6*entry.noise_t1))
					cc.setValue("vth_t2", int(entry.zero_t2 - 6*entry.noise_t2))
					cc.setValue("vth_e", int(entry.zero_e - 6*entry.noise_e))

		daqd.setAsicsConfig(asicsConfig)

		return None
	
	def __ad5535VoltageToDAC(self, key, y):
		# Linear interpolation on closest neighbours
		xy = self.__ad5535CalibrationTable[key]
		for i in range(1, len(xy)):
			x1, y1 = xy[i-1]
			x2, y2 = xy[i]
			if y2 >= y: break

		# y = m*x+b
		m = (y2 - y1)/(x2 - x1)
		b = y1 - m*x1
		x = (y-b)/m
		return int(round(x))

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
				print "1Invalid ASIC parameter: '%s'" % key
				exit(1)
			else:
				t[("global", k)] = int(value)
		elif key[0:8] == "channel.":
			k = key[8:]
			if k not in ck:
				print "2Invalid ASIC parameter: '%s'" % key
				exit(1)
			else:
				t[("channel", k)] = int(value)
		else:
			print "3Invalid ASIC parameter: '%s'" % key
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
		baseline_T = int(l[4])
		zero_T1, noise_T1, zero_T2, noise_T2 = [ float(v) for v in l[5:9] ]
		baseline_E = int(l[9])
		zero_E, noise_E = [ float(v) for v in l[10:12] ]
		c[(portID, slaveID, chipID, channelID)] = DiscriminatorConfigEntry(baseline_T, zero_T1, noise_T1, zero_T2, noise_T2, baseline_E, zero_E, noise_E)
	return c