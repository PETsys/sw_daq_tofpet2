# kate: indent-mode: python; indent-pasted-text false; indent-width 8; replace-tabs: off;
# vim: tabstop=8 shiftwidth=8

from . import spi

class LegacyWrapper:
	"""! Connection wrapper for firmware using old protocols
	"""
	def __init__(self, conn):
		self.__conn = conn

	def read_config_register(self, portID, slaveID, word_width, base_address):
		return self.__conn.read_config_register(portID, slaveID, word_width, base_address)

	def write_config_register(self, portID, slaveID, word_width, base_address, value):
		return self.__conn.write_config_register(portID, slaveID, word_width, base_address, value)

	#
	# Implement legacy firmware SPI master protocol
	# freq_sel, miso_edge and mosi_edge are ignored
	def spi_master_execute(self, portID, slaveID, chipID, cycle_length, sclk_en_on, sclk_en_off, cs_on, cs_off, mosi_on, mosi_off, miso_on, miso_off, mosi_data, freq_sel=1, miso_edge="rising", mosi_edge="rising"):

		if chipID == 0xFFEF:
			cfgFunctionID = 0x03
			chipID = 0xFF



		if len(mosi_data) == 0:
			mosi_data = [ 0x00 ]

		command = [ chipID,
			cycle_length & 0xFF, (cycle_length >> 8) & 0xFF,
			sclk_en_on & 0xFF, (sclk_en_on >> 8) & 0xFF,
			sclk_en_off & 0xFF, (sclk_en_off >> 8) & 0xFF,
			cs_on & 0xFF, (cs_on >> 8) & 0xFF,
			cs_off & 0xFF, (cs_off >> 8) & 0xFF,
			mosi_on & 0xFF, (mosi_on >> 8) & 0xFF,
			mosi_off & 0xFF, (mosi_off >> 8) & 0xFF,
			miso_on & 0xFF, (miso_on >> 8) & 0xFF,
			miso_off & 0xFF, (miso_off >> 8) & 0xFF
			] + mosi_data

		return self.__conn.sendCommand(portID, slaveID, cfgFunctionID, bytes(command))


def make_image_header():
	boot_sector = [
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0x000000BB,
		0x11220044,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xAA995566,
		0x20000000
	]

	s = []
	for word in boot_sector:
		s += [ (word >> 24) & 0xFF, (word >> 16) & 0xFF, (word >> 8) & 0xFF, word & 0xFF ]
	return bytes(s)


def check_image(conn, portID, slaveID, chipID, addr):
	# Check that there is something which looks like an image at addr
	# WARNING: This is not a reliable check, it only catches some situations.

	# Check the boot sector looks like the start of an image.
	boot_header = make_image_header()
	l = len(boot_header)
	d = spi.generic_nand_flash_read(conn, portID, slaveID, chipID, addr, l)
	if d != boot_header:
		return False


	# Check that it's not an alternating update boot sector
	boot_sector_marker = bytes([ 0xFF for k in range(8) ])
	d = spi.generic_nand_flash_read(conn, portID, slaveID, chipID, addr+248, 8)
	if d == boot_sector_marker:
		return False

	return True



def get_active_image_addr(conn, portID, slaveID, chipID):
	"""! Check if the PROM has an alternating update boot sector

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number

	@return Active image address if the boot sector is found, none otherwise
	"""
	d = spi.generic_nand_flash_read(conn,  portID, slaveID, chipID, 0x0, 256)

	boot_sector_addr = (d[22*4+0] << 24) | (d[22*4+1] << 16) | (d[22*4+2] << 8) | d[22*4+3];
	boot_sector_addr &= 0xFFFFFF


	if d == make_boot_sector(boot_sector_addr):
		# Boot sector matches the boot sector by this software
		return boot_sector_addr

	else:
		# Boot sector contains something else (eg, complete image written via JTAG)
		return None

def make_boot_sector(boot_addr):
	boot_sector = [
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0x000000BB,
		0x11220044,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0xAA995566,
		0x20000000,
		0x3003E001,
		0x0000000B,
		0x30008001,
		0x00000012,
		0x20000000,
		0x30022001,
		0x00000000,
		0x30020001,
		boot_addr,
		0x30008001,
		0x0000000F,
		0x20000000,
		0x30008001,
		0x00000007,
		0x20000000,
		0x20000000,
		0x30026001,
		0x00000000,
		0x30012001,
		0x02003FE5,
		0x3001C001,
		0x00000000,
		0x30018001,
		0x03647093,
		0x30008001,
		0x00000009,
		0x20000000,
		0x3000C001,
		0x00000001,
		0x3000A001,
		0x00000101,
		0x3000C001,
		0x00001000,
		0x30030001,
		0x00001000,
		0x20000000,
		0x20000000,
		0x20000000,
		0x20000000,
		0x20000000,
		0x20000000,
		0x20000000,
		0x20000000,
		0x30002001,
		0x00000000,
		0x30008001,
		0x00000001,
		0x20000000,

		# This should be illegal in a true image
		# but it's fine for a simple boot sector
		0xFFFFFFFF,
		0xFFFFFFFF
		#0x30004065,
		#0x00000000
	]


	s = []
	for word in boot_sector:
		s += [ (word >> 24) & 0xFF, (word >> 16) & 0xFF, (word >> 8) & 0xFF, word & 0xFF ]

	return bytes(s)
