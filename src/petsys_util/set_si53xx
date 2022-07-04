#!/usr/bin/env python3

import sys
from petsys import daqd, spi
import time
import argparse

CLOCK_FILTER_CHIP_ID = 0xFFFE

def main(argv):

	parser = argparse.ArgumentParser(description='Set FEM powe ON/OFF')
	parser.add_argument("-i", type=str, required=True, help="Si53xx configuration file")
	parser.add_argument("-s", type=int, required=True, help="Si53xx clock source")
	parser.add_argument("--port", default=0, help="Unit port ID")
	parser.add_argument("--slave", default=0, help="Unit slave ID")

	args = parser.parse_args()

	conn = daqd.Connection()
	conn.write_config_register(args.port, args.slave, 8, 0x02C0, 0b10000001 | (args.s << 1))

	loadFile(conn, args.port, args.slave, args.i)

	# Check if FPGA has locked on the Si53xx outpout
	t0 = time.time()

	while conn.read_config_register(args.port, args.slave, 1, 0x200) == 0:
		if time.time() - t0 > 5:
			sys.stdout.write("ERROR: ASIC reference clock did not lock\n")
			return 1

	

	return 0


def loadFile(conn, portID, slaveID, inputFileName):
	inputFile = open(inputFileName, "r")

	page  = None
	addr = None
	for line in inputFile:
		if line[0:2] != '0x': continue
		line = line.replace('\r', '')
		line = line.replace('\n', '')
		regNum, regValue = line.split(',')
		regNum = int(regNum, base=16)

		regValue = regValue
		regValue = int(regValue, base=16)

		lPage = regNum >> 8
		lAddr = regNum & 0xFF


		spi.si534x_command(conn, portID, slaveID, CLOCK_FILTER_CHIP_ID, [0b00000000, 0x01])
		spi.si534x_command(conn, portID, slaveID, CLOCK_FILTER_CHIP_ID, [0b01000000, lPage])
		spi.si534x_command(conn, portID, slaveID, CLOCK_FILTER_CHIP_ID, [0b00000000, lAddr ])

		# Write
		spi.si534x_command(conn, portID, slaveID, CLOCK_FILTER_CHIP_ID, [0b01000000, regValue ])
		# Read and increment address
		reply = spi.si534x_command(conn, portID, slaveID, CLOCK_FILTER_CHIP_ID, [0b10100000, 0x55 ])
		#print("%04x %02x %02x" % (regNum, regValue, reply[2]))

		if regNum == 0x0540:
			time.sleep(0.3)


if __name__ == '__main__':
	sys.exit(main(sys.argv))
