#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, config
from copy import deepcopy
import argparse

parser = argparse.ArgumentParser(description='Make a simple SiPM bias voltage table')
parser.add_argument("--config", type=str, required=True, help="Configuration file")
parser.add_argument("--vth_t1", type=int, required=True, help="Discriminator T1 (DAC above zero)")
parser.add_argument("--vth_t2", type=int, required=True, help="Discriminator T2 (DAC above zero)")
parser.add_argument("--vth_e", type=int, required=True, help="Discriminator E (DAC above zero)")
parser.add_argument("-o", type=str, required=True, help="Output file")


args = parser.parse_args()
config = config.ConfigFromFile(args.config, loadMask=config.LOAD_DISC_CALIBRATION)


outputFile = open(args.o, "w")

outputFile.write("#portID\tslaveID\tchipID\tchannelID\tvth_t1\tvth_t2\tvth_e\n")
for portID, slaveID, chipID, channelID in config.getCalibratedDiscChannels():
	outputFile.write("%d\t%d\t%d\t%d\t%d\t%d\t%d\n" % (portID, slaveID, chipID, channelID, args.vth_t1, args.vth_t2, args.vth_e))

outputFile.close()
