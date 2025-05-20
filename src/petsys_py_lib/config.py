# kate: indent-mode: python; indent-pasted-text false; indent-width 8; replace-tabs: off;
# vim: tabstop=8 shiftwidth=8

import configparser
import os.path
import re
from sys import stderr
from . import tofpet2b, tofpet2c, fe_power
import bitarray
import math
from time import sleep, time
import os

LOAD_BIAS_CALIBRATION	= 0x00000001
LOAD_BIAS_SETTINGS 	= 0x00000002
LOAD_DISC_CALIBRATION	= 0x00000004
LOAD_DISC_SETTINGS	= 0x00000008
LOAD_MAP		= 0x00000010
LOAD_QDCMODE_MAP	= 0x00000020
LOAD_FIRMWARE_QDC_CALIBRATION = 0x00000040
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
	configParser = configparser.RawConfigParser()
	configParser.read(configFileName)
	
	if (loadMask & LOAD_BIAS_CALIBRATION) != 0:
		fn = configParser.get("main", "bias_calibration_table")
		fn = replace_variables(fn, cdir)
		if not os.path.isfile(fn):
			print(f"Error: File '{fn}' not present in working data folder. It is required in order to set correct bias voltage for SIPMs.")
			exit(1)
		t = readBiasCalibrationTable_tripplet_list(fn)
		config._Config__biasChannelCalibrationTable = t
		config._Config__loadMask |= LOAD_BIAS_CALIBRATION

	if (loadMask & LOAD_BIAS_SETTINGS) != 0:
		fn = configParser.get("main", "bias_settings_table")
		fn = replace_variables(fn, cdir)
		if not os.path.isfile(fn):
			print(f"Error: File '{fn}' not present in working data folder. It is required in order to set bias voltage of SIPMs.")
			exit(1)
		t = readSiPMBiasTable(fn)
		config._Config__biasChannelSettingsTable = t
		config._Config__loadMask |= LOAD_BIAS_SETTINGS

	if (loadMask & LOAD_DISC_CALIBRATION) != 0:
		fn = configParser.get("main", "disc_calibration_table")
		fn = replace_variables(fn, cdir)
		if not os.path.isfile(fn):
			print(f"Error: File '{fn}' not present in working data folder. It is required in order to set the discriminator thresholds for data acquisition.")
			exit(1)
		b, t = readDiscCalibrationsTable(fn)
		config._Config__asicChannelBaselineSettingsTable = b
		config._Config__asicChannelThresholdCalibrationTable = t
		config._Config__loadMask |= LOAD_DISC_CALIBRATION

	if (loadMask & LOAD_DISC_SETTINGS) != 0:
		fn = configParser.get("main", "disc_settings_table")
		fn = replace_variables(fn, cdir)
		if not os.path.isfile(fn):
			print(f"Error: File '{fn}' not present in working data folder. It is required in order to set the discriminator thresholds for data acquisition.")
			exit(1)
		t = readDiscSettingsTable(fn)
		config._Config__asicChannelThresholdSettingsTable = t
		config._Config__loadMask |= LOAD_DISC_SETTINGS

	if (loadMask & LOAD_QDCMODE_MAP) != 0:
		fn = configParser.get("main", "acquisition_mode_table")
		fn = replace_variables(fn, cdir)
		if not os.path.isfile(fn):
			print(f"Error: File '{fn}' not present in working data folder. It is required in order to acquire data in mixed mode.")
			exit(1)
		t = readQDCModeTable(fn)
		config._Config__asicChannelQDCModeTable = t
		config._Config__loadMask |= LOAD_QDCMODE_MAP


	# Load hw_trigger configuration IF hw_trigger section exists
	hw_trigger_config = { "type" : None }
	if (loadMask & LOAD_MAP) != 0:
		hw_trigger_config["group_min_energy"] = configParser.getfloat("hw_trigger", "group_min_energy")
		hw_trigger_config["group_max_energy"] = configParser.getfloat("hw_trigger", "group_max_energy")
		hw_trigger_config["group_min_multiplicity"] = configParser.getint("hw_trigger", "group_min_multiplicity")
		hw_trigger_config["group_max_multiplicity"] = configParser.getint("hw_trigger", "group_max_multiplicity")

		hw_trigger_config["febd_pre_window"] = configParser.getint("hw_trigger", "pre_window")
		hw_trigger_config["febd_post_window"] = configParser.getint("hw_trigger", "post_window")
		hw_trigger_config["coincidence_window"] = configParser.getint("hw_trigger", "coincidence_window")
		hw_trigger_config["pre_window"] = 0
		hw_trigger_config["post_window"] = 0
        #hw_trigger_config["febd_pre_window"] = configParser.getint("hw_trigger", "febd_pre_window")
		#hw_trigger_config["febd_post_window"] = configParser.getint("hw_trigger", "febd_post_window")
		
		hw_trigger_config["single_acceptance_period"] = configParser.getint("hw_trigger", "single_acceptance_period")
		hw_trigger_config["single_acceptance_length"] = configParser.getint("hw_trigger", "single_acceptance_length")

		fn = configParser.get("main", "trigger_map")
		fn = replace_variables(fn, cdir)
		hw_trigger_config["regions"] = readTriggerMap(fn)

	# Load hw_trigger calibration table IF thrshold settings so require
	if (loadMask & LOAD_FIRMWARE_QDC_CALIBRATION) != 0:
		fn = configParser.get("hw_trigger", "hwtrigger_empirical_calibration_table")
		fn = replace_variables(fn, cdir)
		t = []
		if not os.path.isfile(fn) and hwTriggerParamsAreDefault(hw_trigger_config):
			fn2 = configParser.get("main", "disc_settings_table")
			fn2 = replace_variables(fn2, cdir)
			t = makeSimpleEmpiricalCalibrationTable(fn2)			
		elif not os.path.isfile(fn):
			print(f"Error: Calibration file '{fn}' not present in working data folder. It is required in order to enable hw_trigger using non-default energy and multiplicity thresholds.")
			exit(1)
		else:
			t = readQDCEmpiricalCalibrationTable(fn)
		config._Config__asicTacQDCEmpiricalCalibrationTable = t
		config._Config__loadMask |= LOAD_FIRMWARE_QDC_CALIBRATION
	

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
		self.__asicTacQDCEmpiricalCalibrationTable = {}
		self.__asicParameterTable = {}
		self.__hw_trigger = None


	def loadToHardware(self, daqd, bias_enable=APPLY_BIAS_OFF, hw_trigger_enable=False, qdc_mode = "qdc"):
		#
		# Apply bias voltage settings
		# 
		hvdacHwConfig = daqd.get_hvdac_config()
		if bias_enable == APPLY_BIAS_PREBD or bias_enable == APPLY_BIAS_ON:
			assert (self.__loadMask & LOAD_BIAS_CALIBRATION) != 0
			assert (self.__loadMask & LOAD_BIAS_SETTINGS) != 0
			for portID, slaveID in daqd.getActiveFEBDs(): 
				fe_power.set_bias_power(daqd, portID, slaveID, 'on')
			for key in list(self.__biasChannelSettingsTable.keys()):
				offset, prebd, bd, over = self.getBiasChannelDefaultSettings(key)
				if bias_enable == APPLY_BIAS_PREBD:
					Vset = offset + prebd
				else:
					Vset = offset + bd + over
				
				dacSet = self.mapBiasChannelVoltageToDAC(key, Vset)
				hvdacHwConfig[key] = dacSet
			daqd.set_hvdac_config(hvdacHwConfig)
		elif bias_enable == APPLY_BIAS_OFF:
			for portID, slaveID in daqd.getActiveFEBDs(): 
				fe_power.set_bias_power(daqd, portID, slaveID, 'off')
		else:
			raise Exception('Unknown value for bias_enable')
				
		asicsConfig = daqd.getAsicsConfig()

		# Apply ASIC parameters from file
		for (gc, key), value in list(self.__asicParameterTable.items()):
			for ac in list(asicsConfig.values()):
				if gc == "global":
					ac.globalConfig.setValue(key, value)
				else:
					for cc in ac.channelConfig:
						cc.setValue(key, value)

		# Apply discriminator baseline calibrations
		if (self.__loadMask & LOAD_DISC_CALIBRATION) != 0:
			for portID, slaveID, chipID in list(asicsConfig.keys()):
				ac = asicsConfig[(portID, slaveID, chipID)]
				for channelID in range(64):
					baseline_t, baseline_e = self.getAsicChannelDefaultBaselineSettings((portID, slaveID, chipID, channelID))
					cc = ac.channelConfig[channelID]
					cc.setValue("baseline_t", baseline_t)
					cc.setValue("baseline_e", baseline_e)

		# Apply discriminator settings and energy acquisition mode 
		for portID, slaveID, chipID in list(asicsConfig.keys()):
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

				daqd.write_config_register(portID, slaveID, 32, 0x060C, self.__hw_trigger["single_acceptance_length"] << 16 | self.__hw_trigger["single_acceptance_period"] )

				hw_trigger_regions = self.__hw_trigger["regions"]
				nRegions = daqd.read_config_register(portID, slaveID, 16, 0x0600)
				bytes_per_region = int(math.ceil(nRegions / 8.0))
				bits_per_region = 8 * bytes_per_region
				for r1 in range(nRegions):
					region_mask = bitarray.bitarray([ 0 for n in range(bits_per_region) ], endian="little")
					for r2 in range(nRegions):
						if (r1,r2) in hw_trigger_regions or (r2,r1) in hw_trigger_regions:
							region_mask[r2] = 1

					region_data = region_mask.tobytes()

					daqd.write_mem_ctrl(portID, slaveID, 6, 8, r1 * bytes_per_region, region_data)
					
			enable = 0b1
			calibration_enable = 0b1
			accumulator_on = 0b1
			energy_threshold_enable = 0b1
			nHits_threshold_enable = 0b1
			febd_tgr_enable = 0b1
			setupWord = enable | (calibration_enable << 1) | (accumulator_on << 4) | (energy_threshold_enable << 8) | (nHits_threshold_enable << 9) | (febd_tgr_enable << 10)
        
			energy_sum_vector = 0b000001111100000 #Sum energies within -2 to 2 clocks
			hits_sum_vector =  0b000001111100000  #Find multiple events within -2 to 2 clocks
			referenceVectors = energy_sum_vector | ( hits_sum_vector << 16 )

			# FEB/D setup
			for portID, slaveID in daqd.getActiveFEBDs():
				daqd.write_config_register(portID, slaveID, 9, 0x0602, setupWord)
				daqd.write_config_register(portID, slaveID, 32, 0x0620, referenceVectors)
				daqd.write_config_register(portID, slaveID, 2, 0x061C, self.__hw_trigger["febd_pre_window"])
				daqd.write_config_register(portID, slaveID, 4, 0x061E, self.__hw_trigger["febd_post_window"])
				daqd.write_config_register(portID, slaveID, 16, 0x0604,  self.float_to_u7_5(self.__hw_trigger["group_min_energy"]))
				daqd.write_config_register(portID, slaveID, 16, 0x0614,  self.float_to_u7_5(self.__hw_trigger["group_max_energy"]))
				daqd.write_config_register(portID, slaveID, 16, 0x0618,  self.__hw_trigger["group_max_multiplicity"])
				daqd.write_config_register(portID, slaveID, 16, 0x061A,  self.__hw_trigger["group_min_multiplicity"])

			for portID, slaveID, chipID in list(asicsConfig.keys()):
				for channelID in range(64):
					for tacID in range(4):
						address = tacID & 0b11
						address |= (channelID & 0x3F) << 2
						address |= chipID  << 8

						try:
								c0, c1, c2, k0 = self.getAsicTacQDCEmpiricalCalibrationTable((portID, slaveID, chipID, channelID, tacID))
						except:
								c0, c1, c2, k0 = (0,0,0,0)
                                                                
						c0_fixedPoint = self.getFixedPointBinaryCalibrationValue(c0, 10, 6)
						c1_fixedPoint = self.getFixedPointBinaryCalibrationValue(c1, 10, -1)
						c2_fixedPoint = self.getFixedPointBinaryCalibrationValue(c2, 9, -8, True)
						k0_fixedPoint = self.getFixedPointBinaryCalibrationValue(k0, 6, 1)

						data = (k0_fixedPoint << 30) | (c2_fixedPoint << 20) | (c1_fixedPoint << 10) | c0_fixedPoint

						data_bitarray = bitarray.bitarray(map(int, bin(data)[2:]), endian = "little")
						num_zeros = 40 - len(data_bitarray)
						leading_zeros = bitarray.bitarray('0' * num_zeros)
						data2_bitarray = (leading_zeros + data_bitarray)
						data2 = data2_bitarray.tobytes()
						reversed_data = data2[::-1]
                                                
						daqd.write_mem_ctrl2(portID, slaveID, 7, 40, address, reversed_data)	
		return None
	
	def getFixedPointBinaryCalibrationValue(self, value, nBits, msb, isSigned = False):
		sign = 0
		if isSigned and (value < 0):
			sign = 1
		decBits = nBits-msb-1
		value = abs(value)

		intPart = int(value)
		decPart = int((value % 1) * 2**(decBits))
		fixedPointRep = (intPart << decBits) | decPart
		result = self.twos_complement( fixedPointRep) if sign == 1 else fixedPointRep
		
		return result

	def float_to_u7_5(self, f):
		f = max(0, min(f, 127.96875))
		integer_part = int(f)
		fractional_part = int((f - integer_part) * 32)
		u7_5 = (integer_part << 5) | fractional_part
		return u7_5
        
	def twos_complement(self, value):
		return (value ^ ((1 << 10) - 1)) + 1

	def getCalibratedBiasChannels(self):
		return list(self.__biasChannelCalibrationTable.keys())
	
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
		return list(self.__asicChannelBaselineSettingsTable.keys())
	
	def getAsicChannelDefaultBaselineSettings(self, key):
		return self.__asicChannelBaselineSettingsTable[key]
		
	def getAsicChannelDefaultThresholds(self, key):
		return self.__asicChannelThresholdSettingsTable[key]
	
	def getAsicChannelQDCMode(self, key):
		return self.__asicChannelQDCModeTable[key]

	def getAsicTacQDCEmpiricalCalibrationTable(self, key):
		return self.__asicTacQDCEmpiricalCalibrationTable[key]	
			
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
				print("Invalid ASIC parameter: '%s'" % key)
				exit(1)
			else:
				t[("global", k)] = toInt(value)
		elif key[0:8] == "channel.":
			k = key[8:]
			if k not in ck:
				print("Invalid ASIC parameter: '%s'" % key)
				exit(1)
			else:
				t[("channel", k)] = toInt(value)
		else:
			print("Invalid ASIC parameter: '%s'" % key)
			exit(1)
	return t


def normalizeAndSplit(l):
	l = re.sub(r"\s*#.*", "", l)	# Remove comments
	l = re.sub(r"^\s*", '', l)	# Remove leading white space
	l = re.sub(r"\s*$", '', l)	# Remove trailing whitespace
	l = re.sub(r"\s+", '\t', l)	# Normalize whitespace to tabs
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
		portID, slaveID, channelID, slotID = [ int(v) for v in l[0:4] ]
		key = (portID, slaveID, channelID, slotID)
		if key not in c:
			c[key] = []
		dac_set = int(l[4])
		v_meas = float(l[5])
		adc_meas = int(l[6])
		
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
			portID, slaveID, slotID, channelID = [ int(v) for v in l[0:4] ]
			y = [ float(v) for v in l[4:] ]
			assert len(x) == len(y)
			xyz = [ (x[i], y[i], 0) for i in range(len(x)) ]
			c[(portID, slaveID, slotID, channelID)] = xyz
	f.close()
	return c

def readSiPMBiasTable(fn):
	f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, slotID, channelID = [ int(v) for v in l[0:4] ]
		c[(portID, slaveID, slotID, channelID)] = [ float(v) for v in l[4:8] ]
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
			print("Error in '%s' line %d: mode must be 'qdc' or 'tot'\n" % (fn, ln))
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
		c = (l[2]).upper()
		if c not in ['M', 'C']:
			print("Error in '%s' line %d: type must be 'M' or 'C'\n" % (fn, ln))
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

def readQDCEmpiricalCalibrationTable(fn):
	f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, chipID, channelID, tacID = [ int(v) for v in l[0:5] ]
		c[(portID, slaveID, chipID, channelID, tacID)] = [ float(v) for v in l[5:9] ]
	f.close()
	return c

def makeSimpleEmpiricalCalibrationTable(fn):
	f = open(fn)
	c = {}
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue
		portID, slaveID, chipID, channelID= [ int(v) for v in l[0:4] ]
		for tacID in range(4):
			c[(portID, slaveID, chipID, channelID, tacID)] = [1,0,0,0.5]
	f.close()
	return c


def hwTriggerParamsAreDefault(hw_trigger_config):
	isDefault = True
	if hw_trigger_config["group_min_energy"] > 0 or hw_trigger_config["group_max_energy"] < 128:
		isDefault = False 
	if hw_trigger_config["group_min_multiplicity"] > 1 or hw_trigger_config["group_max_multiplicity"] < 1024: 
		isDefault = False
	return isDefault
