#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, config
from copy import deepcopy
import argparse

def main():
	parser = argparse.ArgumentParser(description='Acquire data for TDC calibration')
	parser.add_argument("--config", type=str, required=True, help="Configuration file")
	parser.add_argument("-o", type=str, dest="fileNamePrefix", required=True, help="Data filename (prefix)")
	parser.add_argument("--time", type=float, required=True, help="Acquisition time (in seconds)")
	parser.add_argument("--mode", type=str, required=True, choices=["tot", "qdc", "mixed"], help="Acquisition mode (tot, qdc or mixed)")
	parser.add_argument("--enable-hw-trigger", dest="hwTrigger", action="store_true", help="Enable the hardware coincidence filter")
	parser.add_argument("--monitor-toc", dest="monitor_toc", type=str, required=True, help="Monitor TOC")
	parser.add_argument("--wait-on", dest="waitOn", type=str, help="Wait on named pipe before acquiring data")
	args = parser.parse_args()

	mask = config.LOAD_ALL
	if args.mode != "mixed":
		mask ^= config.LOAD_QDCMODE_MAP
	systemConfig = config.ConfigFromFile(args.config, loadMask=mask)

	conn = daqd.Connection()
	conn.initializeSystem()
	systemConfig.loadToHardware(conn, bias_enable=config.APPLY_BIAS_ON, hw_trigger_enable=args.hwTrigger, qdc_mode = args.mode)

	if args.waitOn:
		conn.waitOnNamedPipe(args.waitOn)
	conn.openRawAcquisitionWithMonitor(args.fileNamePrefix, monitor_config=args.config, monitor_toc=args.monitor_toc)
	conn.acquire(args.time, 0, 0);

	systemConfig.loadToHardware(conn, bias_enable=config.APPLY_BIAS_OFF)
	return 0

if __name__ == '__main__':
	exit(main())