#!/usr/bin/env python3
#-*- coding: utf-8 -*-
#

from petsys import daqd, info
from time import sleep
import sys

daqd = daqd.Connection()

print("--- DAQ PORTS ---") 
for portID in daqd.getActivePorts():
	print("DAQ Port %2d: " % portID, "%12d transmitted %12d received (%d errors)" % daqd.getPortCounts(portID))

print("--- CONNECTED UNITSs ---")
for portID, slaveID in daqd.getActiveUnits():
	mtx, mrx, mrxBad, slaveOn, stx, srx, srxBad = daqd.getFEBDCount1(portID, slaveID)

	base_pcb, fw_variant, prom_variant = daqd.getUnitInfo(portID, slaveID)
	unit_version = "0x%016X" % daqd.read_config_register(portID, slaveID, 64, 0xFFF0)

	if info.is_febd((base_pcb, fw_variant, prom_variant)):
		# This is a FEB/D
		print("Unit (%2d, %2d): FEB/D type:%04X/%016X ver:%s\n" % (portID, slaveID, base_pcb, fw_variant, unit_version), end=' ')


	elif info.is_trigger((base_pcb, fw_variant, prom_variant)):
		print("Unit (%2d, %2d): CLK&TGR type:%04X/%016X ver:%s\n" % (portID, slaveID, base_pcb, fw_variant, unit_version), end=' ')
	else:
		sys.stderr.write("ERROR: UNIT (%2d, %2d) is of unknown type!\n" % (portID, slaveID))
		exit(1)

	print("  MASTER link %12d transmitted %12d received (%d errors)" % (mtx, mrx, mrxBad))
	if slaveOn:
		print("  SLAVE  link %12d transmitted %12d received (%d errors)" % (stx, srx, srxBad))
	else:
		print("  SLAVE  link off")
