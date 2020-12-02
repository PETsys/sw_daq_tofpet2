#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, config
from copy import deepcopy
import argparse

parser = argparse.ArgumentParser(description='Make a simple energy calibration table to linearise charge measurements obtained in QDC mode. The function created for each channel is the average obtained by PETsys Electronics to several channels and ASICs.')
parser.add_argument("--config", type=str, required=True, help="Configuration file")
parser.add_argument("-o", type=str, required=True, help="Output file")


args = parser.parse_args()
config = config.ConfigFromFile(args.config, loadMask=config.LOAD_DISC_CALIBRATION)

outputFile = open(args.o, "w")


p0 = 8.00000;
p1 = 1.04676;
p2 = 1.02734;
p3 = 0.31909;


outputFile.write("#portID\tslaveID\tchipID\tchannelID\tp0\tp1\tp2\tp3\n")
for portID, slaveID, chipID, channelID in config.getCalibratedDiscChannels():
        for tacID in range(4):
                outputFile.write("%d\t%d\t%d\t%d\t%d\t%f\t%f\t%f\t%f\n" % (portID, slaveID, chipID, channelID, tacID, p0, p1, p2, p3))

outputFile.close()
