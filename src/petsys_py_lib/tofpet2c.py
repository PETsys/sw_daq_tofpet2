# -*- coding: utf-8 -*-
from bitarray import bitarray
from bitarray_utils import intToBin, binToInt, grayToBin, grayToInt

def nrange(start, end):
	r = [x for x in range(start, end) ]
	r.reverse()
	return r


GlobalConfigAfterReset = bitarray('1100011110010111111101011010110111001011011110001101000100110011101110110110011001011111001110111110111011111000111111111110010111101111111001111111011000010011111100001101000000010110')

## Contains parameters and methods related to the global operation of one ASIC. 
class AsicGlobalConfig(bitarray):
	## Constructor. 
	# Defines and sets all fields to default values. Most important fields are:
	# 
	def __init__(self, initial=None, endian="big"):
		super(AsicGlobalConfig, self).__init__()

		self.__fields = {
			"tx_nlinks"		: [ n for n in nrange(0, 2) ],
			"tx_ddr"		: [ n for n in nrange(2, 3) ],
			"tx_mode"		: [ n for n in nrange(3, 5) ],
			"debug_mode"		: [ n for n in nrange(5, 6) ],
			"veto_mode"		: [ n for n in nrange(6, 12) ],
			"tdc_clk_div"		: [ n for n in nrange(12, 13) ],
			"r_clk_en"		: [ n for n in nrange(13, 16) ],	# bits 16..17 are ignored
			"stop_ramp_en"		: [ n for n in nrange(18, 20) ],
			"counter_en"		: [ n for n in nrange(20, 21) ],
			"counter_period"	: [ n for n in nrange(21, 24) ],
			"tac_refresh_en"	: [ n for n in nrange(24, 25) ],
			"tac_refresh_period"	: [ n for n in nrange(25, 29) ],
			"data_clk_div"		: [ n for n in nrange(29, 31) ],
			"unused_1"		: [ n for n in range(31, 32) ],
			"fetp_enable"		: [ n for n in range(32, 33) ],
			"input_polarity"	: [ n for n in range(33, 34) ],
			"attenuator_ls"		: [ n for n in range(34, 40) ],
			"v_ref_diff_bias_ig"	: [ n for n in range(40, 46) ],
			"v_cal_ref_ig"		: [ n for n in range(46, 51) ], 
			"fe_postamp_t"		: [ n for n in range(51, 56) ],
			"fe_postamp_e"		: [ n for n in range(56, 61) ],
			"v_cal_tp_top"		: [ n for n in range(61, 66) ],
			"v_cal_diff_bias_ig"	: [ n for n in range(66, 71) ],
			"v_att_diff_bias_ig"	: [ n for n in range(71, 77) ],
			"v_integ_ref_ig"	: [ n for n in range(77, 83) ],
			"imirror_bias_top"	: [ n for n in range(83, 88) ],
			"tdc_comp_bias" 	: [ n for n in range(88, 93) ],
			"tdc_i_lsb"		: [ n for n in range(93, 98) ],
			"disc_lsb_t1"		: [ n for n in range(98, 104) ],
			"fe_ib2"		: [134] + [176] + [ n for n in range(104, 109) ],
			"vdifffoldcas"		: [ n for n in range(109, 115) ],
			"disc_vcas"		: [ n for n in range(115, 119) ],
			"disc_lsb_e"		: [ n for n in range(119, 125) ],
			"tdc_i_ref" 		: [ n for n in range(125, 130) ],
			"tdc_comp_vcas"		: [ n for n in range(130, 134) ],
			# see fe_ib2 for bit 134
			"main_global_dac"	: [ n for n in range(135, 140) ],
			"fe_ib1"		: [ n for n in range(140, 146) ],
			"disc_ib"		: [ n for n in range(146, 152) ],
			"disc_lsb_t2"		: [ n for n in range(152, 158) ],
			"tdc_tac_vcas_p"	: [ n for n in range(158, 163) ],
			"tdc_tac_vcas_n"	: [ n for n in range(163, 167) ],
			"adebug_out_mode"	: [ n for n in range(167, 169) ],
			"tdc_global_dac"	: [ n for n in range(169, 175) ],
			"adebug_buffer"		: [ 175 ],
			# ib2_msb 176
			#  1bits unused 177
			
			"disc_sf_bias"		: [ n for n in range(178, 184) ]
		}

		if initial is not None:
			# We have an initial value, let's just go with it!
			#self[0:169] = bitarray(initial)	;# TOFPET 2a
			self[0:184] = bitarray(initial)	;# TOFPET 2b

			return None


		#self[0:169] = bitarray('1000000000010110111001011101110001100000000010110101110110110011001011100000000001110111011111000111111111110010111101111111001111111011000010010110100001110010000110000') ; # TOFPET 2a

		self[0:184] = bitarray('1100011110010111111101011010110111001011011110001101000100110011101110110110011001011111001110111110111011111000111111111110010111101111111001111111011000010011000100001101000000010110'); # TOFPET 2c
		
		# WARNING This value is negated in the default(reset) config
		self.setValue("tdc_comp_bias", 0b00100)

		# WARNING These values were set by experimental adjustment
		# tdc global dac adjust due to mismatch
		self.setValue("tdc_global_dac", 63-44) # default: 63-11
		# main global dac adjustment due to mismatch
		self.setValue("main_global_dac", 31 - 20) # default: 31 - 17
		self.setValue("main_global_dac", 0b10111) ## WARNING
		
		# WARNING: Pushing these values to maximum seems to lead to better SPTR
		# but also push power consumption. Needs more study
		self.setValue("fe_ib2", 0b0100000)
		self.setValue("fe_ib2", 0b0000000) ## WARNING
		self.setValue("disc_sf_bias", 0)
		
		
		# WARNING These seem to be a reasonable compromise on having widest range for these discriminators
		# but was obtained with a small sampe
		self.setValue("disc_lsb_t2", 48)
		self.setValue("disc_lsb_e", 40)

		# Default FETP settings
		self.setValue("v_cal_tp_top", 1)
		self.setValue("v_cal_diff_bias_ig", 0)
		self.setValue("v_cal_ref_ig", 31);

		# Disable the counter and set it to a reasonably long period
		self.setValue("counter_en", 0b0)
		self.setValue("counter_period", 0b110)

		return None

	def __deepcopy__(self, memo):
		return AsicGlobalConfig(initial=self)
		

	## Set the value of a given parameter as an integer
	# @param key  String with the name of the parameter to be set
	# @param value  Integer corresponding to the value to be set	
	def setValue(self, key, value):
		b = intToBin(value, len(self.__fields[key]))
		self.setBits(key, b)

	## Set the value of a given parameter as a bitarray
	# @param key  String with the name of the parameter to be set
	# @param value  Bitarray corresponding to the value to be set		
	def setBits(self, key, value):
		index = self.__fields[key]
		assert len(value) == len(index)
		for a,b in enumerate(index):
			self[183 - b] = value[a]

	## Returns the value of a given parameter as a bitarray
	# @param key  String with the name of the parameter to be returned	
	def getBits(self, key):
		index = self.__fields[key]
		value = bitarray(len(index))
		for a,b in enumerate(index):
			value[a] = self[183 - b]
		return value

	## Returns the value of a given parameter as an integer
	# @param key  String with the name of the parameter to be returned	
	def getValue(self, key):
		return binToInt(self.getBits(key))

	## Prints the content of all parameters as a bitarray	
	def printAllBits(self):
		for key in self.__fields.keys():
			print key, " : ", self.getBits(key)

	## Prints the content of all parameters as integers
	def printAllValues(self):
		unsorted = [ (min(bitList), name) for name, bitList in self.__fields.items() ]
		unsorted.sort()
		for b, key in unsorted:
			bitList = self.__fields[key]
			l = bitList[0]
			r = bitList[-1]
			print "%30s : %3d : %20s : %d..%d" % (key, self.getValue(key), self.getBits(key), l, r)

	## Returns all the keys (variables) in this class
	def getKeys(self):
		return self.__fields.keys()

## Contains parameters and methods related to the operation of one channel of the ASIC. 
class AsicChannelConfig(bitarray):
	## Constructor
	# Defines and sets all fields to default values. Most important fields are:
	# 
	def __init__(self, initial=None, endian="big"):
		super(AsicChannelConfig, self).__init__()

		self.__fields = {
			"trigger_mode_1"	: [ n for n in nrange(0, 2) ],
			"debug_mode"		: [ n for n in nrange(2, 4) ],
			"sync_chain_length"	: [ n for n in nrange(4, 6) ],
			"dead_time"		: [ n for n in nrange(6, 12) ],
			"counter_mode"		: [ n for n in nrange(12, 16) ], 
			"tac_max_age"		: [ n for n in nrange(16, 21) ],
			"tac_min_age"		: [ n for n in nrange(21, 26) ],
			"trigger_mode_2_t"	: [ n for n in nrange(26, 28) ],
			"trigger_mode_2_e"	: [ n for n in nrange(28, 31) ],
			"trigger_mode_2_q"	: [ n for n in nrange(31, 33) ],
			"trigger_mode_2_b"	: [ n for n in nrange(33, 36) ],
			"branch_en_eq"		: [ n for n in nrange(36, 37) ],
			"branch_en_t"		: [ n for n in nrange(37, 38) ],
			"qdc_mode"		: [ n for n in nrange(38, 39) ],
			"trigger_b_latched"	: [ n for n in nrange(39, 40) ],
			"min_intg_time"		: [ n for n in nrange(40, 47) ],
			"max_intg_time"		: [ n for n in nrange(47, 54) ],
			"output_en"		: [ n for n in nrange(54, 56) ],
			"qtx2_en"		: [ n for n in nrange(56, 57) ],

			"baseline_t"		: [ n for n in nrange(57, 63) ],
			"vth_t1"		: [ n for n in nrange(63, 69) ],
			"vth_t2"		: [ n for n in nrange(69, 75) ],
			"vth_e"			: [ n for n in nrange(75, 81) ],
			"baseline_e"		: [ 82, 83, 81 ],
			"fe_delay"		: [84, 88, 87, 85, 86],#[ n for n in nrange(84, 89) ],
			"postamp_gain_t"	: [ n for n in range(89, 91) ],
			"postamp_gain_e"	: [ n for n in range(91, 93) ],
			"postamp_sh_e"		: [ n for n in nrange(93, 95) ],
			"intg_en"		: [ n for n in nrange(95, 96) ],
			"intg_signal_en"	: [ n for n in nrange(96, 97) ],
			"att"			: [ n for n in nrange(97, 100) ],
			"tdc_current_t"		: [ n for n in nrange(100, 104) ],
			"tdc_current_e"		: [ n for n in nrange(104, 108) ],
			"fe_tp_en"		: [ n for n in nrange(108, 110) ],
			"ch63_obuf_msb"		: [ n for n in nrange(110, 111) ],
		#		"tdc_delay"		: [ n for n in nrange(110, 114) ],
			"integ_source_sw"	: [ n for n in nrange(111,113)  ],
			"t1_hysteresis"		: [ n for n in nrange(115, 118) ],
			"t2_hysteresis"		: [ n for n in nrange(118, 121) ],
			"e_hysteresis"		: [ n for n in nrange(121, 124) ],
			"hysteresis_en_n"	: [ n for n in nrange(124, 125) ]

		}

		if initial is not None:
			# We have an initial value, let's just go with it!
			self[0:125] = bitarray(initial)
			return None

		# Specify default value
		self[0:125] = bitarray('10100100100000100000000001101100000000001111010011101111111000111101000000111000001110111101010100101010111110000000000000000')

		# Disable shaping by default
		self.setValue("postamp_sh_e", 0b00)
		
		# Default triggering
		self.setValue("fe_delay", 0b01011)	# Maximum T1 delay
		self.setValue("trigger_mode_2_t", 0b01)	# T1'delayed and T2
		self.setValue("trigger_mode_2_e", 0b010)# not E
		self.setValue("trigger_mode_2_q", 0b01)	# T2
		self.setValue("trigger_mode_2_b", 0b101)# T1 or T2 or E
		
		# Default integration windows: fixed ~300 ns
		self.setValue("min_intg_time", 34)
		self.setValue("max_intg_time", 34)

		# Avoid powers of 2
		self.setValue("tac_max_age", 30)
		
		# This setting gives better time resolution
		self.setValue("fe_delay", 14)

		# This setting has better linearity
		self.setValue("att", 1)

		return None

	def __deepcopy__(self, memo):
		return AsicChannelConfig(initial=self)

	## Set the value of a given parameter as an integer
	# @param key  String with the name of the parameter to be set
	# @param value  Integer corresponding to the value to be set		
	def setValue(self, key, value):
		b = intToBin(value, len(self.__fields[key]))
		self.setBits(key, b)

	## Set the value of a given parameter as a bitarray
	# @param key  String with the name of the parameter to be set
	# @param value  Bitarray corresponding to the value to be set		
	def setBits(self, key, value):
		index = self.__fields[key]
		assert len(value) == len(index)
		for a,b in enumerate(index):
			self[124 - b] = value[a]

	## Returns the value of a given parameter as a bitarray
	# @param key  String with the name of the parameter to be returned	
	def getBits(self, key):
		index = self.__fields[key]
		value = bitarray(len(index))
		for a,b in enumerate(index):
			value[a] = self[124 - b]
		return value

	## Returns the value of a given parameter as an integer
	# @param key  String with the name of the parameter to be returned	
	def getValue(self, key):
		return binToInt(self.getBits(key))

	# Prints the content of all parameters as a bitarray		
	def printAllBits(self):
		for key in self.__fields.keys():
			print key, " : ", self.getBits(key)
		
	## Prints the content of all parameters as integers
	def printAllValues(self):
		unsorted = [ (min(bitList), name) for name, bitList in self.__fields.items() ]
		unsorted.sort()
		for b, key in unsorted:
			bitList = self.__fields[key]
			l = bitList[0]
			r = bitList[-1]
			print "%30s : %3d : %20s : %d..%d" % (key, self.getValue(key), self.getBits(key), l, r)

	## Set the baseline value in units of ADC (63 to 0)
	def setBaseline(self, v):
		self.__baseline = v

	## Returns the baseline value for this channel
	def getBaseline(self):
		return self.__baseline

	## Returns all the keys (variables) in this class
	def getKeys(self):
		return self.__fields.keys()

## A class containing instances of AsicGlobalConfig and AsicChannelConfig
#, as well as 2 other bitarrays related to test pulse configuration. Is related to one given ASIC.
class AsicConfig:
	def __init__(self):
		self.channelConfig = [ AsicChannelConfig() for x in range(64) ]
		self.globalConfig = AsicGlobalConfig()
		return None



class ConfigurationError:
	pass

class ConfigurationErrorBadAck(ConfigurationError):
	def __init__(self, portID, slaveID, asicID, value):
		self.addr = (value, portID, slaveID, asicID)
		self.errType = value
	def __str__(self):
		return "Bad ACK (%d) when configuring ASIC at port %2d, slave %2d, asic %2d"  % self.addr

class ConfigurationErrorBadCRC(ConfigurationError):
	def __init__(self, portID, slaveID, asicID):
		self.addr = (portID, slaveID, asicID)
	def __str__(self):
		return "Received configuration datta with bad CRC from ASIC at port %2d, slave %2d, asic %2d" % self.addr

class ConfigurationErrorStuckHigh(ConfigurationError):
	def __init__(self, portID, slaveID, asicID):
		self.addr = (portID, slaveID, asicID)
	def __str__(self):
		return "MOSI stuck high from ASIC at port %2d, slave %2d, asic %2d" % self.addr

class ConfigurationErrorGeneric(ConfigurationError):
	def __init__(self, portID, slaveID, asicID, value):
		self.addr = (value, portID, slaveID, asicID)
	def __str__(self):
		return "Unexpected configuration error %02X from ASIC at port %2d, slave %2d, asic %2d" % self.addr

class ConfigurationErrorBadRead(ConfigurationError):
	def __init__(self, portID, slaveID, asicID, written, read):
		self.data = (portID, slaveID, asicID, written, read)
	def __str__(self):
		return "Configuration readback failed for ASIC at port %2d, slave %2d, asic %2d: wrote %s, read %s" % self.data

class ConfigurationErrorBadReply(ConfigurationError):
	def __init__(self, expected, actual):
		self.data = (expected, actual)
	def __str__(self):
		return "Bad reply for ASIC configuration command: expected %d bytes, got %d bytes" % self.data


