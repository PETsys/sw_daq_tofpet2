from . import spi

def read_febd_bias_info(conn, portID, slaveID):

	n_slots = conn.read_config_register(portID, slaveID, 8, 0x0030)
	bias_type_info = conn.read_config_register(portID, slaveID, 16, 0x0031)

	result = {}
	for n in range(n_slots):
		bias_type = (bias_type_info >> (4*n)) & 0xF
		if bias_type == 0xF:
			result[n] = (False, 64, "ad5535rev1", False)

		elif bias_type == 0xE:
			result[n] = (True, 16, "ltc2668rev1", True)

	return result




def set_channel(conn, portID, slaveID, channelID, value):
	bias_info = conn.getUnitInfo(portID, slaveID)["bias"]

	slot = channelID // 64
	channelID = channelID % 64
	_, _, bias_type, _ = bias_info[slot]

	if bias_type == "ad5535rev1":
		if channelID > 32:
			chipID = 0x11
		else:
			chipID = 0x10
		channelID = channelID % 32

		spi.ad5535_set_channel(conn, portID, slaveID, chipID, channelID, value)

	elif bias_type == "ltc2668rev1":
		chipID = 0x10
		spi.ltc2668_set_channel(portID, slaveID, chipID, channelID, value)

	return None


def get_active_channels(conn):
	r = []
	for p, s in conn.getActiveFEBDs():
		bias_info = conn.getUnitInfo(p, s)["bias"]
		for n, (has_prom, n_channels, bias_type, has_adc) in bias_info.items():
			r += [ (p, s, 64*n + k) for k in range(n_channels) ]

	return r

