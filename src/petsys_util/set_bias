#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, fe_power # type: ignore 
import argparse

def main():
	parser = argparse.ArgumentParser(description='Set BIAS power ON/OFF')
	parser.add_argument("--power", type=str, required=True, choices=["off", "on"], help="Set BIAS power")
	args = parser.parse_args()

	connection = daqd.Connection()

	for portID, slaveID in connection.getActiveFEBDs(): 
		fe_power.set_bias_power(connection, portID, slaveID, args.power)

if __name__ == "__main__":
	main()
