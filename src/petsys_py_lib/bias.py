from . import spi
from math import ceil

def read_bias_slot_info(conn, portID, slaveID, slotID):
	bias_interface = conn.read_config_register(portID, slaveID, 16, 0x0030)
	bias_interface = (bias_interface >> (4*slotID)) & 0xF

	return (bias_interface, None)

def has_prom(conn, portID, slaveID, slotID):
	bias_slot_info = conn.getBiasSlotInfo(portID, slaveID, slotID)

	if bias_slot_info == (0xF, None):
		return False

	elif bias_slot_info == (0xE, None):
		return True

	else:
		return None




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
		# Impose minimum 1V bias voltage
		min_dac = int(ceil(2**16 * 1.0 / 75))
		value = max(value, min_dac)
	
		chipID = 0x8000 + 0x100 * slotID + 0x10
		spi.ltc2668_set_channel(conn, portID, slaveID, chipID, channelID, value)

	return None


def get_active_channels(conn):
	r = []
	for portID, slaveID, slotID in conn.getActiveBiasSlots():
		bias_slot_info = conn.getBiasSlotInfo(portID, slaveID, slotID)

		if bias_slot_info == (0xF, None):
			r += [ (portID, slaveID, slotID, k) for k in range(64) ]

		elif bias_slot_info == (0xE, None):
			r += [ (portID, slaveID, slotID, k) for k in range(16) ]

	return r





	

