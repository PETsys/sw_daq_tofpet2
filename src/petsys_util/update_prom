#!/usr/bin/env python3

import sys
import argparse
import time
import math
from petsys import daqd, spi, boot

PROM_CHIP_ID = 0xFFEF

def main(argv):
	parser = argparse.ArgumentParser(description='Update FPGA PROM')
	parser.add_argument("--port", default=0, help="Unit port ID")
	parser.add_argument("--slave", default=0, help="Unit slaveID ID")
	parser.add_argument("--bin", type=str, required=True, help="BIN file name")
	parser.add_argument("--force", default=False, action="store_true", help="Force updating")
	parser.add_argument("--method", required=True, choices=["soft", "alternate", "unprotect"], help="Update method")
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


	print("Waiting for PROM device...")
	spi.generic_nand_flash_wait_write(conn, args.port, args.slave, PROM_CHIP_ID)
	prom_id = spi.generic_nand_flash_getid(conn, args.port, args.slave, PROM_CHIP_ID)
	prom_id = list(prom_id[1:4])
	if prom_id == [ 0xC2, 0x20, 0x18 ]:
		print("MX25L12835F PROM detected.")
		prom_bulk_erase = spi.mx25l12835f_bulk_erase
		prom_64k_erase = spi.mx25l12835f_64k_erase
		prom_write = spi.mx25l12835f_write

	elif prom_id == [ 0x20, 0xBA, 0x18 ]:
		print("N25Q128A PROM detected.")
		prom_bulk_erase = spi.n25q128a_bulk_erase
		prom_64k_erase = spi.n25q128a_64k_erase
		prom_write = spi.n25q128a_write
	else:
		sys.stderr.write("ERROR: Unknown PROM type: %s\n" % ("").join([ "%02X" % v for v in prom_id ]))
		return 1

	f = open(args.bin, "rb") # If file does not exist, python will exit here
	bitstream_data = f.read()


	# TODO: Remove this once DAQ card driver is updated
	spi.MAX_PROM_DATA_PACKET_SIZE = 256


	current_active_image_addr = boot.get_active_image_addr(conn, args.port, args.slave, PROM_CHIP_ID)

	if args.method == "unprotect":
		# Unprotect the PROM
		print("Unprotecting PROM.")
		set_golden_image_protection(conn, args.port, args.slave, PROM_CHIP_ID, False)

	elif args.method == "soft":
		if current_active_image_addr != None:
			sys.stderr.write("ERROR: PROM has a boot sector at 0x0, not a golden image.\n")
			return 1

		print("Protecting golden image area...")
		set_golden_image_protection(conn, args.port, args.slave, PROM_CHIP_ID, True)
		next_image_addr = 0x800000

		print("Erasing update image area...")
		prom_64k_erase(conn, args.port, args.slave, PROM_CHIP_ID, (next_image_addr >> 16), int(math.ceil(len(bitstream_data) / (1<<16))))

		print("Writing update image...")
		prom_write(conn, args.port, args.slave, PROM_CHIP_ID, next_image_addr, bitstream_data)

		print("Verifying update image...")
		d = spi.generic_nand_flash_read(conn, args.port, args.slave, PROM_CHIP_ID, next_image_addr, len(bitstream_data))

		if d != bitstream_data:
			sys.stderr.write("ERROR: Verification failed (1)! Previous image will remain in use.\n")
			return 1


		print("Booting FPGA into update image...")
		# Boot into new image
		conn.write_config_register(args.port, args.slave, 32, 0x02C8, next_image_addr) # This has no effect in legacy mode
		conn.write_config_register(args.port, args.slave, 1, 0x299, 0b1)

		# Wait a few seconds
		time.sleep(5)

		# Restart the connection to daqd
		conn = daqd.Connection()


		try:
			boot_enable = conn.read_config_register(args.port, args.slave, 1, 0x299)
			if boot_enable != 0:
				# Boot enable register is still at 1 which means the FPGA didn't actually boot
				sys.stderr.write("ERROR: FPGA boot failed (1)!  Power cycle to boot to golden image.\n")
				return 1

		except daqd.CommandErrorTimeout as e:
			sys.stderr.write("ERROR: FPGA boot failed (2)! Power cycle to boot to golden image.\n");
			return 1

		boot_status = conn.read_config_register(args.port, args.slave, 32, 0x02A0)
		if (boot_status & 0b11111010) != 0:
			sys.stderr.write("ERROR: FPGA boot failed (3)!  Power cycle to boot to golden image.\n");
			return 1

	else:
		# Unprotect the PROM in case some parts were write protected by earlier versions of this software
		set_golden_image_protection(conn, args.port, args.slave, PROM_CHIP_ID, False)

		# Area A is 0x010000
		# Area B is 0x810000.
		# Reserve 0x800000 for applications using golden/update images strategy
		if current_active_image_addr == None:
			# No valid boot sector was detected
			# But this PROM may have a valid image at 0x0
			next_image_addr = 0x810000
			print("Loading new image to image area B.")

		elif current_active_image_addr >= 0x800000:
			# Active image is B, load new image to A
			next_image_addr = 0x010000
			print("Loading new image to image area A.")

		else:
			# Multiple cases:
			# - Active image is A, load new image to B
			# - PROM has an image at 0x0, load new image to B to protect image at 0x0 until boot sector is written
			# - PROM has not been programmed
			next_image_addr = 0x810000
			print("Loading new image to image area B.")


		# Legacy firmware can only boot to 0x800000!
		# So we have to accept that for this update.
		if legacyFirmware:
			if next_image_addr < 0x800000:
				sys.stderr.write("Cannot load image into area A when running legacy firmware.\n")
				if not args.force:
					sys.stderr.write("Use --force to force loading into area. B\n")
					return 1
				else:
					next_image_addr = 0x800000
					print("Loading new image to image area B instead.")
			else:
				# Must use 0x800000 instead of 0x810000
				next_image_addr = 0x800000


		print("Erasing new image area...")
		prom_64k_erase(conn, args.port, args.slave, PROM_CHIP_ID, (next_image_addr >> 16), int(math.ceil(len(bitstream_data) / (1<<16))))

		print("Writing new image...")
		prom_write(conn, args.port, args.slave, PROM_CHIP_ID, next_image_addr, bitstream_data)

		print("Verifying new image...")
		d = spi.generic_nand_flash_read(conn, args.port, args.slave, PROM_CHIP_ID, next_image_addr, len(bitstream_data))

		if d != bitstream_data:
			sys.stderr.write("ERROR: Verification failed (1)! Previous image will remain in use.\n")
			return 1


		print("Booting FPGA into new image...")
		# Boot into new image
		conn.write_config_register(args.port, args.slave, 32, 0x02C8, next_image_addr) # This has no effect in legacy mode
		conn.write_config_register(args.port, args.slave, 1, 0x299, 0b1)

		# Wait a few seconds
		time.sleep(5)

		# Restart the connection to daqd
		conn = daqd.Connection()


		try:
			boot_enable = conn.read_config_register(args.port, args.slave, 1, 0x299)
			if boot_enable != 0:
				# Boot enable register is still at 1 which means the FPGA didn't actually boot
				sys.stderr.write("ERROR: FPGA boot failed (1)!  Power cycle to boot to previous image.\n")
				return 1

		except daqd.CommandErrorTimeout as e:
			sys.stderr.write("ERROR: FPGA boot failed (2)! Power cycle to boot to previous image.\n");
			return 1

		boot_status = conn.read_config_register(args.port, args.slave, 32, 0x02A0)
		if (boot_status & 0b11111010) != 0:
			sys.stderr.write("ERROR: FPGA boot failed (3)!  Power cycle to boot to previous image.\n");
			return 1


		# We should not be loading legacy firmware images
		protocol = conn.read_config_register(args.port, args.slave, 64, 0xFFF8)
		if (protocol & 0x7FFFFFFFFFFFFFFF) < 0x100:
			sys.stderr.write("ERROR: Loaded image contains legay firmware. Power cycle to boot to previous image.\n")
			return 1

		# Re-verify the first 64 KiB of the new image
		# Mainly to check that the new firmware can handle large accesses to the PROM
		d = spi.generic_nand_flash_read(conn, args.port, args.slave, PROM_CHIP_ID, next_image_addr, 64*1024)
		if d != bitstream_data[0:64*1024]:
			sys.stderr.write("ERROR: Verification failed (4)! Power cycle to boot to previous image.\n")
			return 1


		print("Updating boot sector...")
		boot_sector = boot.make_boot_sector(next_image_addr)
		prom_64k_erase(conn, args.port, args.slave, PROM_CHIP_ID, 0, 1)
		prom_write(conn, args.port, args.slave, PROM_CHIP_ID, 0x0, boot_sector)
		d = spi.generic_nand_flash_read(conn, args.port, args.slave, PROM_CHIP_ID, 0x0, len(boot_sector))
		if d != boot_sector:
			sys.stderr.write("ERROR: Verification failed (3)! Boot sector is corrupted!\n")
			return 1

	print("DONE")

	return 0





def set_golden_image_protection(conn, portID, slaveID, chipID, protect):
	# TODO: Consider making this function more generic and move it to the SPI library
	# All NAND flash we use for FPGAs use the same interface for locking

	spi.generic_nand_flash_wait_write(conn, portID, slaveID, chipID)

	# Ensure WEL is disabled
	spi.generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)

	spi.generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x06], 0)

	if protect:
		spi.generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x01, 0x60], 0)
	else:
		spi.generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x01, 0x00], 0)

	spi.generic_nand_flash_wait_write(conn, portID, slaveID, chipID)

	# Ensure WEL is disabled
	spi.generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)
	return None

if __name__ == '__main__':
	sys.exit(main(sys.argv))

