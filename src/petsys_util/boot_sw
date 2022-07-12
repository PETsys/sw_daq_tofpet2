#!/usr/bin/env python3

import sys
import argparse
import time
import math
from petsys import daqd, spi, boot

PROM_CHIP_ID = 0xFFEF
BOOT_OFFSET = 0x800000

def main(argv):
	parser = argparse.ArgumentParser(description='Boot FPGA into update image')
	parser.add_argument("--port", default=0, help="Unit port ID")
	parser.add_argument("--slave", default=0, help="Unit slaveID ID")
	parser.add_argument("--force", default=False, action="store_true", help="Force boot")

	args = parser.parse_args()



	conn = daqd.Connection()

	legacyFirmware = False
	protocol = conn.read_config_register(args.port, args.slave, 64, 0xFFF8)
	if (protocol & 0x7FFFFFFFFFFFFFFF) < 0x100:
		# Firmware running on the FPGA is generally too old for this software
		# but for PROM updating we support this as special case.
		print("Using legacy protocol.")
		conn = boot.LegacyWrapper(conn)
		legacyFirmware = True

	boot_status = conn.read_config_register(args.port, args.slave, 32, 0x02A0)
	if (boot_status & 0xFF00) != 0:
		if not args.force:
			sys.stderr.write("FPGA was booted into the update image before.\n")
			sys.stderr.write("Use --force to force booting into update image.\n")
			return 1
	

	if not boot.check_image(conn, args.port, args.slave, PROM_CHIP_ID, BOOT_OFFSET):
		sys.stderr.write("ERROR: PROM address 0x%08X does not contain an image\n", BOOT_OFFSET)
		return 1

	conn.write_config_register(args.port, args.slave, 32, 0x02C8, 0x800000)
	conn.write_config_register(args.port, args.slave, 1, 0x299, 0b1)

	print("Waiting for FPGA to reboot...")
	time.sleep(10)

	boot_en = conn.read_config_register(args.port, args.slave, 1, 0x299)
	if boot_en != 0:
		sys.stderr.write("ERROR: FPGA did not reboot.\n")
		return 3

	boot_status = conn.read_config_register(args.port, args.slave, 32, 0x02A0)

	return 0

if __name__ == '__main__':
	sys.exit(main(sys.argv))
