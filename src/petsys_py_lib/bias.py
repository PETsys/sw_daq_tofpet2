# kate: indent-mode: python; indent-pasted-text false; indent-width 8; replace-tabs: off;
# vim: tabstop=8 shiftwidth=8

from . import spi
from math import ceil

BIAS_32P_MAGIC = [ 0x27, 0x43, 0xd4, 0x9c, 0x28, 0x41, 0x47, 0xe2, 0xbb, 0xa2, 0xff, 0x81, 0xd0, 0x60, 0x12, 0xd3 ]
BIAS_32P_CODE = int('11QA', base=36)

def read_bias_slot_info(conn, portID, slaveID, slotID, allowUnknown=False):
	bias_interface = conn.read_config_register(portID, slaveID, 16, 0x0030)
	bias_interface = (bias_interface >> (4*slotID)) & 0xF

	if bias_interface == 0xF:
		return (bias_interface, None)
	elif bias_interface == 0xE:
		return (bias_interface, None)
	else:
		chipID = 0x8000 + 0x100 * slotID + 0x0
		d = spi.m95256_read(conn, portID, slaveID, chipID, 0x0, 16)
		if d == bytes(BIAS_32P_MAGIC):
			return (bias_interface, BIAS_32P_CODE)
		elif allowUnknown:
			return (bias_interface, None)
		else:
			raise BadBiasMagic()

def has_prom(conn, portID, slaveID, slotID):
	bias_interface, prom_version = conn.getBiasSlotInfo(portID, slaveID, slotID)

	if bias_interface == 0xF:
		return False
	else:
		return True

def set_channel(conn, portID, slaveID, slotID, channelID , value):
	bias_slot_info = conn.getBiasSlotInfo(portID, slaveID, slotID)

	if bias_slot_info == (0xF, None):
		if channelID > 32:
			chipID = 0x8000 + 0x100 * slotID + 0x11
		else:
			chipID = 0x8000 + 0x100 * slotID + 0x10
		channelID = channelID % 32

		# Impose minimum 1V bias voltage
		min_dac = int(ceil(2**14 * 1.0 / 200))
		value = max(value, min_dac)

		spi.ad5535_set_channel(conn, portID, slaveID, chipID, channelID, value)

	elif bias_slot_info == (0xE, None):
		# BIAS-16P
		#
		# When bias_en is OFF we cannot communicate with the DAC and an exception will be thrown
		# In particular this happens when clearing the BIAS DACs before enabling bias

		# For the particular case of trying to set the DAC to zero do not bother
		# trying to communicate with the DAC if bias_enable is off
		if value == 0:
			power_feedback = conn.read_config_register(portID, slaveID, 8, 0x021C)
			if power_feedback & 0b10 == 0:
				return None

		# Impose minimum 1V bias voltage
		min_dac = int(ceil(2**16 * 1.0 / 75))
		value = max(value, min_dac)
	
		chipID = 0x8000 + 0x100 * slotID + 0x10
		spi.ltc2668_set_channel(conn, portID, slaveID, chipID, channelID, value)


	elif bias_slot_info == (0xD, BIAS_32P_CODE):
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


def get_active_channels(conn):
	r = []
	for portID, slaveID, slotID in conn.getActiveBiasSlots():
		bias_slot_info = conn.getBiasSlotInfo(portID, slaveID, slotID)

		if bias_slot_info == (0xF, None):
			r += [ (portID, slaveID, slotID, k) for k in range(64) ]

		elif bias_slot_info == (0xE, None):
			r += [ (portID, slaveID, slotID, k) for k in range(16) ]

		elif bias_slot_info == (0xD, BIAS_32P_CODE):
			r += [ (portID, slaveID, slotID, k) for k in range(32) ]

		else:
			raise UnknownBiasType()

	return r


class BiasException(Exception): pass
class BadBiasMagic(BiasException): pass
class UnknownBiasType(BiasException): pass
