#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd
from time import sleep
from sys import argv

daqd = daqd.Connection()
f = open(argv[1])

print("Configuring SI53xx clock filter")

for line in f:
	if line[0] == '#': continue
	line = line.rstrip('\r\n')
	regNum, regValue = line.split(', ')
	regNum = int(regNum)

	regValue = '0x' + regValue[:-1]
	regValue = int(regValue, base=16)

	print("Register %02x set to %02x" % (regNum, regValue))
	daqd.setSI53xxRegister(regNum, regValue)

print("Done")
