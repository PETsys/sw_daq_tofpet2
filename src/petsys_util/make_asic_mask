#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, config
from copy import deepcopy
import argparse
import os
import subprocess

parser = argparse.ArgumentParser(description='Create an ASIC mask (for enabling only a subset of connected ASICS) and describe how to export it as system environment variable')

parser.add_argument('--asics', nargs='*', type=int, default = None, help='List of Asics to be enabled (command-line)')
parser.add_argument('--asics_file', type=str, default = None, help='List of Asics to be enabled (one column text file)')

args = parser.parse_args()


if (args.asics != None and args.asics_file is None):
    asicList = args.asics
elif (args.asics is None and args.asics_file != None):
    fileAsicList = open(args.asics_file, "r")
    list = fileAsicList.readlines()
    asicList=[]
    for line in list:
        asic = line.split(" ")
        asicList.append(int(asic[0]))

else:
    print("Error: ASIC mask list must be defined, either in command line or by text file")
    exit(0)

currentAsicEnableMask = -1
isMaskSet = False

if "PETSYS_ASIC_MASK" in os.environ.keys():
    isMaskSet = True
    currentAsicEnableMask = int(os.environ["PETSYS_ASIC_MASK"], base=16)
    del os.environ['PETSYS_ASIC_MASK']

connection = daqd.Connection()
connection.initializeSystem()

activeAsics = []
for portID, slaveID, chipID in connection.getActiveAsics():
	activeAsics.append(chipID)

mask = 0	
for asic in asicList:
    if activeAsics.count(asic) == 0:
        print("Error: ASIC %d not detected in the system, cannot be enabled" % asic)
        exit(0)
    mask |= ( 1 << asic)

print("")

if not isMaskSet:
    print ("INFO: PETSYS_ASIC_MASK is currently not set.")
elif currentAsicEnableMask != mask:
    print ("PETSYS_ASIC_MASK is currently defined as %016X\n" % (currentAsicEnableMask))
else:
    print ("PETSYS_ASIC_MASK is already defined to the selected ASICs, no need for updating\n")
    exit(0)
 
exp = 'export PETSYS_ASIC_MASK=%016X' % mask

print("To create a new ASIC enabling mask for ASICs " + str(asicList) + ", type:\n")
print(exp,"\n")
