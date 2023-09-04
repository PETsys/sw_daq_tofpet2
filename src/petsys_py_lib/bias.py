# kate: indent-mode: python; indent-pasted-text false; indent-width 8; replace-tabs: off;
# vim: tabstop=8 shiftwidth=8

from . import spi, fe_power
from math import ceil

BIAS_32P_MAGIC         = [ 0x27, 0x43, 0xd4, 0x9c, 0x28, 0x41, 0x47, 0xe2, 0xbb, 0xa2, 0xff, 0x81, 0xd0, 0x60, 0x12, 0xd3 ]
BIAS_32P_AG_MAGIC      = [ 0x36, 0x53, 0x77, 0x48, 0x72, 0x36, 0x55, 0x77, 0x68, 0x53, 0x57, 0x49, 0x6b, 0x76, 0x4a, 0x50 ]
BIAS_32P_LTC2439_MAGIC = [ 0x2b, 0x0c, 0x37, 0xc0, 0xa1, 0xa5, 0x49, 0xe3, 0xae, 0x22, 0xf3, 0x7c, 0x7b, 0xae, 0x45, 0x19 ]

def get_bias_interface(conn, portID, slaveID, slotID):
	return (conn.read_config_register(portID, slaveID, 16, 0x0030) >> (4*slotID)) & 0xF

def has_prom(conn, portID, slaveID, slotID):
	return (get_bias_interface(conn, portID, slaveID, slotID) != 0xF)

def read_bias_slot_info(conn, portID, slaveID, slotID, allowUnknown=False):
	bias_interface = get_bias_interface(conn, portID, slaveID, slotID)

	if bias_interface == 0xF:
		bias_name = "BIAS_64P"
	elif bias_interface == 0xE:
		bias_name = "BIAS_16P"
	elif bias_interface == 0xD:
		chipID = 0x8000 + 0x100 * slotID + 0x0
		d = spi.m95256_read(conn, portID, slaveID, chipID, 0x0, 16)
		if d == bytes(BIAS_32P_MAGIC):
			bias_name = "BIAS_32P"
		elif d == bytes(BIAS_32P_AG_MAGIC):
			bias_name = "BIAS_32P_AG"
		elif d == bytes(BIAS_32P_LTC2439_MAGIC):
			bias_name = "BIAS_32P_LTC2439"
		elif allowUnknown:
			bias_name = "UNKNOWN_BIAS"
		else:
			raise BadBiasMagic(f'{portID},{slaveID},{slotID},{bias_interface}')
	else:
		raise UnknownBiasType(f'{portID},{slaveID},{slotID},{bias_interface}')
	
	return bias_name

def get_str(conn, portID, slaveID, slotID): # !Should just receive a BIAS NAME, this is a formatter/error handler. Requires changes in GUI (get_str())
	#! FIND OTHER CALLS OF THIS FUCNTION
	PRETTY_NAMES = { 'BIAS_64P'         : 'BIAS-64P',
					 'BIAS_16P'         : 'BIAS-16P',
					 'BIAS_32P'         : 'BIAS-32P',
					 'BIAS_32P_AG'      : 'BIAS-32P AG7200',
					 'BIAS_32P_LTC2439' : 'BIAS-32P' }

	bias_slot_info = conn.getBiasSlotInfo(portID, slaveID, slotID)

	if bias_slot_info in PRETTY_NAMES:
		return PRETTY_NAMES[bias_slot_info]
	else:
		raise UnknownBiasType()
	
def set_ag7200_dcdc(conn, portID, slaveID, slotID, dacID, value):
	# Calculate DAC(VDC)
	value_truncated = max(39.5, min(57, value)) # VDC limits
	offset = 0
	v_dac = -( ( ( (value_truncated-2.495)/100000) - (2.495/5431) ) * 27000 - 2.495) + offset	# Convert target VDC to VDAC
	dac_setting = int((v_dac/5)*65536) 	# Convert VDAC to DAC setting

	# Set DAC
	chipID = 0x8000 + 0x100 * slotID + 0x14  
	spi.max5136_wrt_through(conn, portID, slaveID, chipID, dacID, dac_setting)

def set_channel(conn, portID, slaveID, slotID, channelID , value):
	bias_slot_info = conn.getBiasSlotInfo(portID, slaveID, slotID)

	if bias_slot_info == "BIAS_64P":
		if channelID > 32:
			chipID = 0x8000 + 0x100 * slotID + 0x11
		else:
			chipID = 0x8000 + 0x100 * slotID + 0x10
		channelID = channelID % 32

		# Impose minimum 1V bias voltage
		min_dac = int(ceil(2**14 * 1.0 / 200))
		value = max(value, min_dac)

		spi.ad5535_set_channel(conn, portID, slaveID, chipID, channelID, value)
	elif bias_slot_info == "BIAS_16P":
		# When bias_en is OFF we cannot communicate with the DAC and an exception will be thrown
		# In particular this happens when clearing the BIAS DACs before enabling bias

		# For the particular case of trying to set the DAC to zero do not bother
		# trying to communicate with the DAC if bias_enable is off
		if value == 0:
			if fe_power.get_bias_power_status(conn, portID, slaveID) == 0:
				return None

		# Impose minimum 1V bias voltage
		min_dac = int(ceil(2**16 * 1.0 / 75))
		value = max(value, min_dac)
	
		chipID = 0x8000 + 0x100 * slotID + 0x10
		spi.ltc2668_set_channel(conn, portID, slaveID, chipID, channelID, value)
	elif bias_slot_info in ["BIAS_32P", "BIAS_32P_AG", "BIAS_32P_LTC2439"]:
		# Impose minimum 1V bias voltage
		min_dac = int(ceil(2**16 * 1.0 / 60))
		value = max(value, min_dac)

		dacID = channelID // 16
		channelID = channelID % 16

		chipID = 0x8000 + 0x100 * slotID + 0x10 + dacID
		spi.ltc2668_set_channel(conn, portID, slaveID, chipID, channelID, value)
	else:
		raise UnknownBiasType()

	return None

def get_number_channels(bias_name):
	CH_PER_BIAS = { 'BIAS_64P'         : 64,
					'BIAS_16P'         : 16,
					'BIAS_32P'         : 32,
					'BIAS_32P_AG'      : 32,
					'BIAS_32P_LTC2439' : 32 }
	
	if bias_name in CH_PER_BIAS:
		return CH_PER_BIAS[bias_name]
	else:
		raise UnknownBiasType()


class BiasException(Exception): pass
class BadBiasMagic(BiasException): pass
class UnknownBiasType(BiasException): pass
