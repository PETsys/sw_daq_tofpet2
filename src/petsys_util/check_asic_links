#!/usr/bin/env python3
# -*- coding: utf-8 -*-
from petsys import daqd
from time import sleep
from math import sqrt
import sys

def main():
	connection = daqd.Connection()
	for portID, slaveID in sorted(connection.getActiveFEBDs()):
		asic_enable_vector = connection.read_config_register(portID, slaveID, 64, 0x0318)
		
		asic_deserializer_vector =  connection.read_config_register(portID, slaveID, 64, 0x0302)
		asic_decoder_vector = connection.read_config_register(portID, slaveID, 64, 0x0310)
		
		
		
		for n in range(64):
			if (asic_enable_vector & (1 << n)) != 0:
				deserializer_ok = (asic_deserializer_vector & (1 << n)) != 0
				decoder_ok = (asic_decoder_vector & (1 << n)) != 0
				if deserializer_ok and decoder_ok:
					print("ASIC (%2d, %2d, %2d) is enabled and TX is OK" % (portID, slaveID, n))
				else:
					print("ASIC (%2d, %2d, %2d) is enabled but TX is FAILED [%d %d]" % (portID, slaveID, n, deserializer_ok, decoder_ok))
	return 0


if __name__ == '__main__':
	sys.exit(main())