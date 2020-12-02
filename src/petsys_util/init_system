#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, config
from copy import deepcopy
import argparse
import math
import time


def main():
	parser = argparse.ArgumentParser(description="Initialize system and print status.")
	
	args = parser.parse_args()

	connection = daqd.Connection()
	connection.initializeSystem()


if __name__ == "__main__":
	main()