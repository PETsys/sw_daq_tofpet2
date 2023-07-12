#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, spi, bias # type: ignore 
import argparse
import struct
import sys
import re

def add_calibration_from_rom(connection, portID, slaveID, slotID, outputFile):
	chipID = 0x8000 + 0x100 * slotID + 0x0
	offset = 0

	data = spi.m95256_read(connection, portID, slaveID, chipID, 0x0000, 8)
	if data != b"PETSYS  ":
		if [x for x in spi.m95256_read(connection, portID, slaveID, chipID, 0x0000, 16)] in [bias.BIAS_32P_MAGIC, bias.BIAS_32P_AG_MAGIC, bias.BIAS_32P_LTC2439_MAGIC]:
			print(f'INFO: BIAS_32P detected @ (portID, slaveID, slotID) = ({portID},{slaveID},{slotID})')
			offset = 4
			promLayoutVersion = 0x01
		else:
			sys.stderr.write("ERROR: FEBD (portID %d, slaveID %d) slotID %2d PROM does not contain a valid header. \n" % (portID, slaveID, slotID))
			exit(1)
	
	if offset == 0:
		data = spi.m95256_read(connection, portID, slaveID, chipID, 0x0008, 8)
		promLayoutVersion, = struct.unpack("<Q", data)
	
	if promLayoutVersion == 0x01:
		measuredVoltageFullScale = 100.0
		
		data = spi.m95256_read(connection, portID, slaveID, chipID, 0x0010 + offset, 8)
		n_channels, = struct.unpack("<Q", data)

		data = spi.m95256_read(connection, portID, slaveID, chipID, 0x0018 + offset, 8)
		n_x_values, = struct.unpack("<Q", data)
		
		address = 0x0020
		x_values = [ 0 for i in range(n_x_values) ]
		v_meas = {}
		adc_meas = {}
		for i in range(n_x_values):
			data = spi.m95256_read(connection, portID, slaveID, chipID, 0x020 + 2*i + offset, 2)
			address += 2
			v, =  struct.unpack("<H", data)
			x_values[i] = v

		for j in range(n_channels):
			ch_v_meas = [ 0.0 for i in range(n_x_values) ]
			for i in range(n_x_values):
				data = spi.m95256_read(connection, portID, slaveID, chipID, address + offset, 4)
				address += 4
				v, =  struct.unpack("<I", data)
				v = v * measuredVoltageFullScale / (2**32)
				ch_v_meas[i] = v
			v_meas[(portID, slaveID, j)] = ch_v_meas
			
		for j in range(n_channels):
			ch_adc_meas = [ 0 for i in range(n_x_values) ]
			for i in range(n_x_values):
				data = spi.m95256_read(connection, portID, slaveID, chipID, address + offset, 4)
				address += 4
				v, =  struct.unpack("<I", data)
				ch_adc_meas[i] = v
			adc_meas[(portID, slaveID, j)] = ch_adc_meas
			
		
		for j in range(n_channels):
			for i in range(n_x_values):
				outputFile.write("%d\t%d\t%d\t%d\t%d\t%f\t%d\n" % (portID, slaveID, slotID, j, x_values[i], v_meas[(portID, slaveID, j)][i], adc_meas[(portID, slaveID, j)][i]))
		
	else:
		sys.stderr.write("ERROR: FEBD (portID %d, slaveID %d) slotID %2d has a PROM with an unknown layout 0x%016x\n" % (portID, slaveID, slotID, promLayoutVersion))
		exit(1)
		
	return None


def normalizeAndSplit(l):
	l = re.sub("\s*#.*", "", l)	# Remove comments
	l = re.sub('^\s*', '', l)	# Remove leading white space
	l = re.sub('\s*$', '', l)	# Remove trailing whitespace
	l = re.sub('\s+', '\t', l)	# Normalize whitespace to tabs
	l = re.sub('\r', '', l)		# Remove \r
	l = re.sub('\n', '', l)		# Remove \l
	l = l.split('\t')
	return l

def add_calibration_table_from_file(inputFileName, portID, slaveID, slotID, outputFile):
	f = open(inputFileName)
	c = {}
	x = []
	for l in f:
		l = normalizeAndSplit(l)
		if l == ['']: continue

		if x == []:
			x = [ int(v) for v in l]
		else:
			_, _, channelID = [ int(v) for v in l[0:3] ]
			y = [ float(v) for v in l[3:] ]
			assert len(x) == len(y)

			for i in range(len(x)):
				outputFile.write("%d\t%d\t%d\t%d\t%d\t%f\t%d\n" % (portID, slaveID, slotID, channelID, x[i], y[i], 0))
	f.close()
	return c



def main():
	parser = argparse.ArgumentParser(description='Make a simple SiPM bias voltage table')
	parser.add_argument("-o", type=str, required=True, help="Output file")
	parser.add_argument("--port", type=int, required=False, action="append", help="Port ID")
	parser.add_argument("--slave", type=int, required=False, action="append", help="Slave ID")
	parser.add_argument("--slotID", type=int, required=False, action="append", help="Slot index")
	parser.add_argument("--filename", type=str, required=False, action="append", default=[], help="File name")
	args = parser.parse_args()

	connection = daqd.Connection()
	outputFile = open(args.o, "w")
	
	febd_calibration_type = {}
	for portID, slaveID, slotID in connection.getActiveBiasSlots():
		febd_calibration_type[(portID, slaveID, slotID)] = "NONE"
		if bias.has_prom(connection, portID, slaveID, slotID) == False:
			continue

		print("FEB/D (%2d, %2d) slot %d has calibration PROM" % (portID, slaveID, slotID))
		add_calibration_from_rom(connection, portID, slaveID, slotID, outputFile)
		febd_calibration_type[(portID, slaveID, slotID)] = "ROM"
		


	for i in range(len(args.filename)):
		portID = args.port[i]
		slaveID = args.slave[i]
		slotID = args.slotID[i]
		if (portID, slaveID, slotID ) not in list(febd_calibration_type.keys()):
			sys.stderr.write("WARNING: Loading calibration table for FEB/D (%2d, %2d) slot %2d which is not present in the system.\n" % (portID, slaveID, slotID))
			
		elif febd_calibration_type[(portID, slaveID, slotID)] == "ROM":
			sys.stderr.write("ERROR: Calibration file specified for FEB/D (%2d, %2d) slot %2d but this FEB/D has a calibration ROM.\n" % (portID, slaveID, slotID))
			exit(1)
			
		print('Reading calibration table "%s" for FEB/D (%d, %d) slot %2d' % (args.filename[i], portID, slaveID, slotID))
		add_calibration_table_from_file(args.filename[i], portID, slaveID, slotID, outputFile)
		febd_calibration_type[(portID, slaveID, slotID)] = "FILE"
	
	for portID, slaveID, slotID in list(febd_calibration_type.keys()):
		if febd_calibration_type[(portID, slaveID, slotID)] == "NONE":
			sys.stderr.write("ERROR: FEB/D (%2d, %2d) slot %2d is present but no calibration file has been specified.\n" % (portID, slaveID, slotID))
			exit(1)
		
		
if __name__ == '__main__':
	main()
