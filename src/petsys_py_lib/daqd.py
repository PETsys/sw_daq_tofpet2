# -*- coding: utf-8 -*-

## @package daqd
# Handles interaction with the system via daqd

import shm_raw
import tofpet2b
import tofpet2c
import socket
from random import randrange
import struct
from time import sleep, time
from bitarray import bitarray
import bitarray_utils
import math
import subprocess
from sys import stdout
from copy import deepcopy

MAX_PORTS = 32
MAX_SLAVES = 32
MAX_CHIPS = 64

# Handles interaction with the system via daqd
class Connection:
        ## Constructor
	def __init__(self):
		socketPath = "/tmp/d.sock"
		self.__systemFrequency = 200E6

		# Open socket to daqd
		self.__socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.__socket.connect(socketPath)

		# Generate a random number for first command serial number
		self.__lastSN = randrange(0, 2**15-1)

		# Open raw data frame shared memory
		shmName, s0, p1, s1 = self.__getSharedMemoryInfo()
		self.__shmName = shmName
		self.__shm = shm_raw.SHM_RAW(self.__shmName)

		self.__activePorts = []
		self.__activeFEBDs = {}
		self.__activeAsics = {}

		self.__asicConfigCache = None
		self.__asicConfigCache_TAC_Refresh = None
		self.__hvdac_config_cache = {}

		self.__helperPipe = None
		
		self.__temperatureSensorList = {}
		self.__triggerUnit = None

	def __getSharedMemoryInfo(self):
		template = "@HH"
		n = struct.calcsize(template)
		data = struct.pack(template, 0x02, n)
		self.__socket.send(data);

		template = "@HQQQ"
		n = struct.calcsize(template)
		data = self.__socket.recv(n);
		length, s0, p1, s1 = struct.unpack(template, data)
		name = self.__socket.recv(length - n);
		return (name, s0, p1, s1)

	## Returns the system reference clock frequency
	def getSystemFrequency(self):
		return self.__systemFrequency
	
	## Returns an array with the active ports
	def getActivePorts(self):
		if self.__activePorts == []:
			self.__activePorts = self.__getActivePorts()
		return self.__activePorts

	def __getActivePorts(self):
		template = "@HH"
		n = struct.calcsize(template)
		data = struct.pack(template, 0x06, n)
		self.__socket.send(data);

		template = "@HQ"
		n = struct.calcsize(template)
		data = self.__socket.recv(n);
		length, mask = struct.unpack(template, data)
		reply = [ n for n in range(12*16) if (mask & (1<<n)) != 0 ]
		return reply

	def getActiveUnits(self):
		unit_list = set(self.getActiveFEBDs())
		trigger_unit = self.getTriggerUnit()
		if trigger_unit is not None:
			unit_list.add(trigger_unit)
		return sorted(unit_list)

	def getTriggerUnit(self):
		if self.__triggerUnit is None:
			self.__activeFEBDs, self.__triggerUnit = self.__scanUnits_ll()

		return self.__triggerUnit
		
	## Returns an array of (portID, slaveID) for the active FEB/Ds (PAB) 
	def getActiveFEBDs(self):
		return sorted(self.__getActiveFEBDs().keys())
	
	def __getActiveFEBDs(self):
		if self.__activeFEBDs == {}:
			self.__activeFEBDs, self.__triggerUnit = self.__scanUnits_ll()
			
		return self.__activeFEBDs

	def __scanUnits_ll(self):
		r = {}
		u = None
		for portID in self.getActivePorts():
			slaveID = 0

			if self.read_config_register(portID, slaveID, 1, 0x0000) == 1:
				bias_type = self.read_config_register(portID, slaveID, 16, 0x0012)
				r[(portID, slaveID)] = (bias_type, )
			
			if self.read_config_register(portID, slaveID, 1, 0x0001) == 1:
				u = (portID, slaveID)
		
			while self.read_config_register(portID, slaveID, 1, 0x0400) != 0b0:
				slaveID += 1
				if self.read_config_register(portID, slaveID, 1, 0x0000) == 1:
					bias_type = self.read_config_register(portID, slaveID, 16, 0x0012)
					r[(portID, slaveID)] = (bias_type, )
				
				if self.read_config_register(portID, slaveID, 1, 0x0001) == 1:
					u = (portID, slaveID)

		return r, u

	def getActiveAsics(self):
		return sorted(self.__activeAsics.keys())

	def getAsicSubtype(self, portID, slaveID, chipID):
		return self.__activeAsics[(portID, slaveID, chipID)]
	
	def getActiveAsicsChannels(self):
		return [ (p, s, a, c) for c in range(64) for (p, s, a) in self.getActiveAsics() ]
	
	def getActiveBiasChannels(self):
		r = []
		for p, s in self.getActiveFEBDs():
			bias_type = self.getBiasType(p, s)
			if bias_type == 1:
				bias_channels = 64
			else:
				bias_channels = 16
			r += [ (p, s, c) for c in range(bias_channels) ]
		return r

	## Disables test pulse 
	def setTestPulseNone(self):
		self.__setSorterMode(True)
		for portID, slaveID in self.getActiveUnits():
			self.write_config_register(portID, slaveID, 64, 0x20B, 0x0)
		return None

	def setTestPulsePLL(self, length, interval, finePhase, invert=False):
		self.set_test_pulse_febds(length, interval, finePhase, invert)

        ## Sets the properties of the internal FPGA pulse generator
        # @param length Sets the length of the test pulse, from 1 to 1023 clock cycles. 0 disables the test pulse.
        # @param interval Sets the interval between test pulses in clock cycles.
        # @param finePhase Defines the delay of the test pulse in clock cycles.
        # @param invert Sets the polarity of the test pulse: active low when ``True'' and active high when ``False''
	def set_test_pulse_febds(self, length, interval, finePhase, invert=False):
		self.__set_test_pulse(self.getActiveFEBDs(), length, interval, finePhase, invert)
		
	def set_test_pulse_tgr(self, length, interval, finePhase, invert=False):
		self.__set_test_pulse([ self.getTriggerUnit() ], length, interval, finePhase, invert)
	
	def __set_test_pulse(self, targets, length, interval, finePhase, invert=False):
		# Check that the pulse interval does not cause problem with the ASIC TAC refresh period
		# First, make sure we have a cache of settings
		if self.__asicConfigCache_TAC_Refresh is None:
			self.getAsicsConfig()
			
		for tacRefreshPeriod_1, tacRefreshPeriod_2 in self.__asicConfigCache_TAC_Refresh:
			tacRefreshPeriod = 64 * (tacRefreshPeriod_1 + 1) * (tacRefreshPeriod_2 + 1)
			if interval % tacRefreshPeriod == 0:
				print "WARNING: Test pulse period %d is a multiple of TAC refresh period %d (%d %d) in some ASICs." % (interval, tacRefreshPeriod, tacRefreshPeriod_1, tacRefreshPeriod_2)
		
		finePhase = int(round(finePhase * 6*56))	# WARNING: This should be firmware dependent..
	
		if interval < (length + 1):
			raise "Interval (%d) must be greater than length (%d) + 1" % (interval, length)

		interval = interval - (length + 1)

		value = 0x1 << 63
		value |= (length & 0x3FF)
		value |= (interval & 0x1FFFFF) << 10
		value |= (finePhase & 0xFFFFFF) << 31
		if invert: value |= 1 << 61

		for portID, slaveID in targets:
			self.write_config_register(portID, slaveID, 64, 0x20B, value)
		return None

	def __setSorterMode(self, mode):
		pass

	def disableEventGate(self):
		self.__daqdGateMode(0)
		for portID, slaveID in self.getActiveUnits():
			self.write_config_register(portID, slaveID, 1, 0x0202, 0b0);
			
	## Enabled external gate function
	# @param delay Delay of the external gate signal, in clock periods
	def enableEventGate(self, delay):
		self.__daqdGateMode(1)
		for portID, slaveID in self.getActiveUnits():
			self.write_config_register(portID, slaveID, 1, 0x0202, 0b1);
			self.write_config_register(portID, slaveID, 10, 0x0294, delay);
			
	def __daqdGateMode(self, mode):
		template1 = "@HHI"
		n = struct.calcsize(template1)
		data = struct.pack(template1, 0x12, n, mode);
		self.__socket.send(data)

		template = "@I"
		n = struct.calcsize(template)
		data = self.__socket.recv(n);
		return None			
			
	def disableCoincidenceTrigger(self):
		if self.getTriggerUnit() is not None:
			portID, slaveID = self.getTriggerUnit()
			self.write_config_register(portID, slaveID, 1, 0x0602, 0b0)

	def disableAuxIO(self):
		for portID, slaveID in self.getActiveFEBDs():
			self.write_config_register(portID, slaveID, 64, 0x0214, 0x0)

	def setAuxIO(self, which, mode):
		which = which.upper()
		ioList = [ "LEMO_J3_J4", "LEMO_J5_J6", "LEMO_J7_J8", "LEMO_J9_J10", "LEMO_J12_J11", "LEMO_J14_J13", "LEMO_J15" ]
		if which not in ioList:
			raise UnknownAuxIO(which)

		n = ioList.index(which) * 8
		mode = mode & 0xFF
		m = (0xFF << n) ^ 0xFFFFFFFFFFFFFFFF
		for portID, slaveID in self.getActiveFEBDs():
			current = self.read_config_register(portID, slaveID, 64, 0x0214)
			current = (current & m) | (mode << n)
			self.write_config_register(portID, slaveID, 64, 0x0214, current)




	## Sends the entire configuration (needs to be assigned to the abstract Connection.config data structure) to the ASIC and starts to write data to the shared memory block
        # @param maxTries The maximum number of attempts to read a valid dataframe after uploading configuration 
	def initializeSystem(self, maxTries = 5):
		# Stop the acquisition, if the system was still acquiring
		self.__setAcquisitionMode(0)

		activePorts = self.getActivePorts()
		print "INFO: active units on ports: ", (", ").join([str(x) for x in self.getActivePorts()])

		# Disable everything
		self.setTestPulseNone()
		self.disableEventGate()
		self.disableCoincidenceTrigger()
		self.disableAuxIO()
		self.__setAllBiasToZero()

		# Check FEB/D board status
		for portID, slaveID in self.getActiveFEBDs():
			pllLocked = self.read_config_register(portID, slaveID, 1, 0x200)
			if pllLocked != 0b1:
				raise ClockNotOK(portID, slaveID)

			asicType = self.read_config_register(portID, slaveID, 16, 0x0102)
			if asicType != 0x0002:
				raise ErrorInvalidAsicType(portID, slaveID, asicType)
			
			
		# Power on ASICs
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 2, 0x0213, 0b11) 
		sleep(0.1) # Wait for power to stabilize

		# Reset the ASICs configuration
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 2, 0x0201, 0b00)
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 1, 0x300, 0b1)
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 1, 0x300, 0b0)
		self.__asicConfigCache = None
		self.__asicConfigCache_TAC_Refresh = None

		# Check which ASICs react to the configuration
		asicConfigOK = [ False for x in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS) ]
		asicType = {}
		initialGlobalAsicConfig = {} # Store the default config we're uploading into each FEB/D

		for portID, slaveID in self.getActiveFEBDs():
			for chipID in range(MAX_CHIPS):
				try:
					status, readback = self.__doAsicCommand(portID, slaveID, chipID, "rdGlobalCfg")
					if readback == tofpet2b.GlobalConfigAfterReset:
						asicType[(portID, slaveID, chipID)] = "2B"
						tofpet2 = tofpet2b
					elif readback == tofpet2c.GlobalConfigAfterReset:
						asicType[(portID, slaveID, chipID)] = "2C"
						tofpet2 = tofpet2c
					else: 
						raise ErrorAsicUnknownConfigurationAfterReset(portID, slaveID, chipID, readback)
					
					gcfg = tofpet2.AsicGlobalConfig()
					ccfg = tofpet2.AsicChannelConfig()
						
					# Upload default configuration, adjusted for FEB/D firmware RX build
					tdc_clk_div, ddr, tx_nlinks = self.__getAsicLinkConfiguration(portID, slaveID)
					gcfg.setValue("tdc_clk_div", tdc_clk_div)
					gcfg.setValue("tx_ddr", ddr)
					gcfg.setValue("tx_nlinks", tx_nlinks)
					#  .. and with the TX logic to calibration
					gcfg.setValue("tx_mode", 0b01)
					initialGlobalAsicConfig[(portID, slaveID, chipID)] = gcfg

					self.__doAsicCommand(portID, slaveID, chipID, "wrGlobalCfg", value=gcfg)
					for n in range(64):
						self.__doAsicCommand(portID, slaveID, chipID, "wrChCfg", channel=n, value=ccfg)

					gID = chipID + MAX_CHIPS * slaveID + (MAX_CHIPS * MAX_SLAVES) * portID
					asicConfigOK[gID] = True
				except tofpet2b.ConfigurationError as e:
					pass


		if self.getTriggerUnit() is not None and [ self.getTriggerUnit() ] != self.getActiveFEBDs():
			# We have a distributed trigger
			# Run trigger signal calibration sequence
			for portID, slaveID in self.getActiveUnits(): self.write_config_register(portID, slaveID, 3, 0x0296, 0b001)
			for portID, slaveID in self.getActiveUnits(): self.write_config_register(portID, slaveID, 3, 0x0296, 0b011)
			for portID, slaveID in self.getActiveUnits(): self.write_config_register(portID, slaveID, 3, 0x0296, 0b111)
			sleep(0.010)
			for portID, slaveID in self.getActiveUnits(): self.write_config_register(portID, slaveID, 3, 0x0296, 0b011)
			for portID, slaveID in self.getActiveUnits(): self.write_config_register(portID, slaveID, 3, 0x0296, 0b001)
		elif self.getTriggerUnit() is None:
			for portID, slaveID in self.getActiveUnits(): self.write_config_register(portID, slaveID, 3, 0x0296, 0b000)


		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 2, 0x0201, 0b01)
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 2, 0x0201, 0b00)
		# Generate master sync (if available) and start acquisition
		# First, cycle master sync enable on/off to calibrate sync reception
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 2, 0x0201, 0b10)
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 2, 0x0201, 0b00)
		sleep(0.010)
		# Then enable master sync reception...
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 2, 0x0201, 0b10)
		# ... and generate the sync
		if self.getTriggerUnit() is not None:
			# We have either
			# (a) a single FEB/D with GbE
			# (b) a distributed trigger
			# Generate sync in the unit responsible for CLK and TGR
			portID, slaveID = self.getTriggerUnit()
			print "INFO: TGR unit is (%2d, %2d)" % (portID, slaveID)
			portID, slaveID = self.__triggerUnit
			self.write_config_register(portID, slaveID, 2, 0x0201, 0b01)
			self.write_config_register(portID, slaveID, 2, 0x0201, 0b00)
		
		# Enable acquisition
		# This will include a 220 ms sleep period for daqd and the DAQ card to clear buffers	
		self.__setAcquisitionMode(1)
		# Finally, disable sync reception
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 2, 0x0201, 0b00)

		# Check that the ASIC configuration has not changed after sync
		for portID, slaveID in self.getActiveFEBDs():
			for chipID in range(MAX_CHIPS):
				gID = chipID + MAX_CHIPS * slaveID + (MAX_CHIPS * MAX_SLAVES) * portID
				if not asicConfigOK[gID]: continue

				status, readback = self.__doAsicCommand(portID, slaveID, chipID, "rdGlobalCfg")
				if readback != initialGlobalAsicConfig[(portID, slaveID, chipID)]:
					raise tofpet2b.ConfigurationErrorBadRead(portID, slaveID, chipID, initialGlobalAsicConfig[(portID, slaveID, chipID)], readback)
			
		# Enable ASIC receiver logic for all ASIC
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 64, 0x0318, 0xFFFFFFFFFFFFFFFF)

		# Set all ASICs to receiver logic calibration  mode
		# Set ASIC receiver logic to calibration mode
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 1, 0x0301, 0b0)
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 1, 0x0301, 0b1)
		# Allow some time for the IDELAY adjustment
		sleep(10 * 2**14 / self.__systemFrequency + 0.010)
		# Set ASIC receiver logic to normal mode
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 1, 0x0301, 0b0)

		# Reconfigure ASICs TX to normal mode
		for portID, slaveID in self.getActiveFEBDs():
			# Try all possible chips, as we don't have a list of active ASIC yet
			for chipID in range(MAX_CHIPS):
				# This chipID didn't respond properly before, skip
				gID = chipID + MAX_CHIPS * slaveID + (MAX_CHIPS * MAX_SLAVES) * portID
				if not asicConfigOK[gID]: continue

				# Same configuration as before...
				gcfg = initialGlobalAsicConfig[(portID, slaveID, chipID)]
				# .. but with the TX logic to normal
				gcfg.setValue("tx_mode", 0b10)
				self.__doAsicCommand(portID, slaveID, chipID, "wrGlobalCfg", value=gcfg)

		# Allow some ms for the deserializer to lock to the 8B/10B pattern
		sleep(0.010)

		# Check which ASICs are receiving properly data words
		deserializerStatus = [ False for x in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS) ]
		decoderStatus = [ False for x in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS) ]

		for portID, slaveID in self.getActiveFEBDs():
			lDeserializerStatus = self.read_config_register(portID, slaveID, 64, 0x0302)
			lDecoderStatus = self.read_config_register(portID, slaveID, 64, 0x0310)

			lDeserializerStatus = [ lDeserializerStatus & (1<<n) != 0 for n in range(MAX_CHIPS) ]
			lDecoderStatus = [ lDecoderStatus & (1<<n) != 0 for n in range(MAX_CHIPS) ]

			k = slaveID * MAX_CHIPS + portID * MAX_CHIPS * MAX_SLAVES
			for n in range(MAX_CHIPS):
				deserializerStatus[k+n] = lDeserializerStatus[n]
				decoderStatus[k+n] = lDecoderStatus[n]

		self.__activeAsics = {}
		inconsistentStateAsics = []
		for gID in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS):
			statusTripplet = (asicConfigOK[gID], deserializerStatus[gID], decoderStatus[gID])
			chipID = gID % MAX_CHIPS
			slaveID = (gID / MAX_CHIPS) % MAX_SLAVES
			portID = gID / (MAX_CHIPS * MAX_SLAVES)

			if statusTripplet == (True, True, True):
				# All OK, ASIC is present and OK
				self.__activeAsics[(portID, slaveID, chipID)] = asicType[(portID, slaveID, chipID)]
			elif statusTripplet == (False, False, False):
				# All failed, ASIC is not present
				pass
			else:
				# Something is not good
				inconsistentStateAsics.append(((portID, slaveID, chipID), statusTripplet))

		if inconsistentStateAsics != []:
			print "WARNING: ASICs with inconsistent presence detection results"
			for portID, slaveID in self.getActiveFEBDs():
				lst = []
				for (lPortID, lSlaveID, lChipID), statusTripplet in inconsistentStateAsics:
					if lPortID != portID or lSlaveID != slaveID: continue
					lst.append((lChipID, statusTripplet))
				
				if lst != []:
					print " FEB/D (%2d, %2d)" % (portID, slaveID)
					for chipID, statusTripplet in lst:
						a, b, c = statusTripplet
						a = a and "OK" or "FAIL"
						b = b and "OK" or "FAIL"
						c = c and "OK" or "FAIL"						
						print "  ASIC %2d: config link: %4s data link pattern: %4s data word: %4s" % (chipID, a, b, c)
			
			if maxTries > 1: 
				print "Retrying..."
				return self.initializeSystem(maxTries - 1)
			else:
				raise ErrorAsicPresenceInconsistent(inconsistentStateAsics)

		self.__setSorterMode(True)

		for portID, slaveID in self.getActiveFEBDs():
			lst = []

			enable_vector = 0x0
			for lPortID, lSlaveID, lChipID in self.getActiveAsics():
				if lPortID != portID or lSlaveID != slaveID: continue
				lst.append(lChipID)
				enable_vector |= (1 << lChipID)

			# Enable ASIC receiver logic only for ASICs detected by software
			self.write_config_register(portID, slaveID, 64, 0x0318, enable_vector)

			if lst != []:
				n = len(lst)
				lst.sort()
				lst = (", ").join([str(lChipID) for lChipID in lst])
				print "INFO: FEB/D (%2d, %2d) has %2d active ASICs: %s" % (portID, slaveID, n, lst)
			else:
				print "INFO: FEB/D (%2d, %2d) has 0 active ASICs." % (portID, slaveID)

		self.__synchronizeDataToConfig()
		return None
		

	def __setAcquisitionMode(self, mode):
		template1 = "@HH"
		template2 = "@H"
		n = struct.calcsize(template1) + struct.calcsize(template2);
		data = struct.pack(template1, 0x01, n) + struct.pack(template2, mode)
		self.__socket.send(data)
		data = self.__socket.recv(2)
		assert len(data) == 2

	def __getAsicLinkConfiguration(self, portID, slaveID):
		nLinks = 1 + self.read_config_register(portID, slaveID, 2, 0x0100)
		if nLinks == 1:
			tx_nlinks = 0b00
		elif nLinks == 2:
			tx_nlinks = 0b01
		elif nLinks == 4:
			tx_nlinks = 0b10
		else:
			raise ErrorInvalidLinks(portID, slaveID, nLinks)

		speed = self.read_config_register(portID, slaveID, 2, 0x0101)
		if speed == 0x0:
			# SDR
			tdc_clk_div = 0b0
			ddr = 0b0

		elif speed == 0x1:
			# DDR
			tdc_clk_div = 0b0
			ddr = 0b1

		elif speed == 0x2:
			# 2SDR
			tdc_clk_div = 0b1
			ddr = 0b0

		else:
			# 2DDR
			tdc_clk_div = 0b1
			ddr = 0b1
		return (tdc_clk_div, ddr, tx_nlinks)


	def read_mem_ctrl(self, portID, slaveID, ctrl_id, word_width, base_address, n_words):
		n_bytes_per_word = int(math.ceil(word_width / 8.0))
		
		command = bytearray([ 0x00 , (n_words - 1) & 0xFF, ((n_words - 1) >> 8) & 0xFF, base_address & 0xFF, (base_address >> 8) & 0xFF ])
		reply = self.sendCommand(portID, slaveID, ctrl_id, command)

		assert len(reply) == 2 + (n_words * n_bytes_per_word)
		
		reply = reply[1:]
		reply = reply[:-1]
		
		r = [ 0 for j in range(n_words) ]
		for j in range(n_words):
			v = 0
			for i in range(n_bytes_per_word):
				v += (reply[j * n_bytes_per_word + i] << (8*i))
			r[j] = v
		return r
	
	
	def write_mem_ctrl(self, portID, slaveID, ctrl_id, word_width, base_address, data):
		n_words = len(data)
		n_bytes_per_word = int(math.ceil(word_width / 8.0))		
		data_bytes = [ (data[j] >> (8*i)) & 0xFF for i in range(n_bytes_per_word) for j in range(n_words) ]
		
		command = bytearray([ 0x01 , (n_words - 1) & 0xFF, ((n_words - 1) >> 8) & 0xFF, base_address & 0xFF, (base_address >> 8) & 0xFF ] + data_bytes)
		reply = self.sendCommand(portID, slaveID, ctrl_id, command)
		
		assert len(reply) == 1
		assert reply[0] == 0x00
		
		return None
	
	
	def read_config_register(self, portID, slaveID, word_width, base_address):
		n_bytes_per_word = int(math.ceil(word_width / 8.0))
		reply = self.read_mem_ctrl(portID, slaveID, 0x00, 8, base_address, n_bytes_per_word)
		r = 0
		for i in range(n_bytes_per_word):
			r += reply[i] << (8*i)
		return r
	
	def write_config_register(self, portID, slaveID, word_width, base_address, value):
		n_bytes_per_word = int(math.ceil(word_width / 8.0))
		data_bytes = [ (value >> (8*i)) & 0xFF for i in range(n_bytes_per_word) ]
		return self.write_mem_ctrl(portID, slaveID, 0x00, 8, base_address, data_bytes)

	def write_config_register_tgr(self, word_width, base_address, value):
		portID, slaveID = self.getTriggerUnit()
		self.write_config_register(portID, slaveID, word_width, base_address, value)
		return None

	def write_config_register_febds(self,  word_width, base_address, value):
		for portID, slaveID in self.getActiveFEBDs():
			self.write_config_register(portID, slaveID, word_width, base_address, value)
		return None
	
	def spi_master_execute(self, portID, slaveID, cfgFunctionID, chipID, cycle_length, sclk_en_on, sclk_en_off, cs_on, cs_off, mosi_on, mosi_off, miso_on, miso_off, mosi_data):
		if len(mosi_data) == 0:
			mosi_data = [ 0x00 ]
		
		command = [ chipID, 
			cycle_length & 0xFF, (cycle_length >> 8) & 0xFF,
			sclk_en_on & 0xFF, (sclk_en_on >> 8) & 0xFF,
			sclk_en_off & 0xFF, (sclk_en_off >> 8) & 0xFF,
			cs_on & 0xFF, (cs_on >> 8) & 0xFF,
			cs_off & 0xFF, (cs_off >> 8) & 0xFF,
			mosi_on & 0xFF, (mosi_on >> 8) & 0xFF,
			mosi_off & 0xFF, (mosi_off >> 8) & 0xFF,
			miso_on & 0xFF, (miso_on >> 8) & 0xFF,
			miso_off & 0xFF, (miso_off >> 8) & 0xFF 
			] + mosi_data
		
		return self.sendCommand(portID, slaveID, cfgFunctionID, bytearray(command))
			
	def __write_hv_ad5535(self, portID, slaveID, channelID, value):
		chipID = channelID / 32
		channelID = channelID % 32
		#chipID = 1 - whichDAC # Wrong decoding in ad5535.vhd
		
		channelID &= 0b11111
		value &= 0b11111111111111
		command = channelID << 14 | value
		return self.__write_ad5535_ll(portID, slaveID, 3, chipID, command)
		
	def __write_ad5535_ll(self, portID, slaveID, cfgFunctionID, chipID, data):
		# SPI master needs data in byte sizes
		# with SPI first bit being most significant bit of first byte
		data = data << 5
		command = [ (data >> 16) & 0xFF, (data >> 8) & 0xFF, (data >> 0) & 0xFF ]
		
		w = 19
		padding = [0x00 for n in range(2) ]
		p = 8 * len(padding)

		# Pad the cycle with zeros
		return self.spi_master_execute(portID, slaveID, cfgFunctionID, chipID, 
			p+w+p, 		# cycle
			p,p+w,	# sclk en
			p-1,p,		# cs
			0, p+w+p, 	# mosi
			p,p+w, 		# miso
			padding + command + padding)
		
	
	def __write_hv_ltc2668(self, portID, slaveID, channelID, value):
		command = [ 0b00110000 + channelID, (value >> 8) & 0xFF , value & 0xFF ]
		return self.__ltc2668_ll(portID, slaveID, 3, 1, command)
		
	def __ltc2668_ll(self, portID, slaveID, cfgFunctionID, chipID, command):
		w = 8 * len(command)
		padding = [0x00 for n in range(2) ]
		p = 8 * len(padding)

		# Pad the cycle with zeros
		return self.spi_master_execute(portID, slaveID, cfgFunctionID, chipID, 
			p+w+p, 		# cycle
			p,p+w, 		# sclk en
			p-1,p+w+1,	# cs
			0, p+w+p, 	# mosi
			p,p+w, 		# miso
			padding + command + padding)
		
	def read_hv_ad7194(self, portID, slaveID, channelID):
		# Reset
		self.__ad7194_ll(portID, slaveID, 3, 2, [0xFF for n in range(8) ], 0)

		# Set mode register
		r = self.__ad7194_ll(portID, slaveID, 3, 2, [0b00001000, 0b00011011, 0b00100100, 0b01100000], 0)

		# Set configuration register
		r =  self.__ad7194_ll(portID, slaveID, 3, 2, [0b00010000, 0b00000100, 0b00000000 + (channelID << 4), 0b01011000], 0)
		
		# Wait for conversion to be ready
		while True:
			r = self.__ad7194_ll(portID, slaveID, 3, 2, [0b01000000], 1)
			if r[1] & 0x80 == 0x00: break
			sleep(0.1)
			
		r = self.__ad7194_ll(portID, slaveID, 3, 2, [0x58], 4)
		v = (r[1] << 16) + (r[2] << 8) + r[3]
		
		return v
	
			
	def __ad7194_ll(self,  portID, slaveID, cfgFunctionID, chipID, command, read_count):
		command = [0x00] + command
		w = 8 * len(command)
		r = 8 * read_count
		p = 2
		w_padding = [ 0xFF for n in range(p) ]
		r_padding = [ 0xFF for n in range(p + read_count) ]
		p = 8 * p

		# Pad the cycle with zeros
		return self.spi_master_execute(portID, slaveID, cfgFunctionID, chipID, 
			p+w+r+p, 		# cycle
			p,p+w+r+1, 		# sclk en
			p-1,p+w+r+1,		# cs
			0, p+w+r+p, 		# mosi
			p+w,p+w+r, 		# miso
			w_padding + command + r_padding)
		
	def __m95256_ll(self, portID, slaveID, cfgFunctionID, chipID, command, read_count):
		w = 8 * len(command)
		r = 8 * read_count
		p = 2
		w_padding = [ 0xFF for n in range(p) ]
		r_padding = [ 0xFF for n in range(p + read_count) ]
		p = 8 * p

		# Pad the cycle with zeros
		return self.spi_master_execute(portID, slaveID, cfgFunctionID, chipID, 
			p+w+r+p, 		# cycle
			p,p+w+r+1, 		# sclk en
			p-0,p+w+r+0,		# cs
			0, p+w+r+p, 		# mosi
			p+w,p+w+r, 		# miso
			w_padding + command + r_padding)
		
	def read_hv_m95256(self, portID, slaveID, address, n_bytes):
		# Break down reads into 4 byte chunks due to DAQ
		rr = ''
		for a in range(address, address + n_bytes, 4):
			count = min([4, address + n_bytes - a])
			r = self.__m95256_ll(portID, slaveID, 3, 0, [0b00000011, (a >> 8) & 0xFF, a & 0xFF], count)
			r = r[1:-1]
			r = ('').join([chr(x) for x in r ])
			rr += r
		return rr
	
	def write_hv_m95256(self, portID, slaveID, address, data):
		data = [ ord(x) for x in data ]
		while True:
			# Check if Write In Progress is set and if so, sleep and try again
			r = self.__m95256_ll(portID, slaveID, 3, 0, [0b00000101], 1)
			if r[1] & 0x01 == 0:
				break
			sleep(0.010)
			
		# cycle WEL
		self.__m95256_ll(portID, slaveID, 3, 0, [0b00000100], 1)
		self.__m95256_ll(portID, slaveID, 3, 0, [0b00000110], 1)
		
		self.__m95256_ll(portID, slaveID, 3, 0, [0b00000010, (address >> 8) & 0xFF, address & 0xFF] + data, 0)
		while True:
			# Check if Write In Progress is set and if so, sleep and try again
			r = self.__m95256_ll(portID, slaveID, 3, 0, [0b00000101], 1)
			if r[1] & 0x01 == 0:
				break
			sleep(0.010)
		
		# Disable WEL (it should be automatic but...)
		self.__m95256_ll(portID, slaveID, 3, 0, [0b00000100], 1)
			
		
	

	## Sends a command to the FEB/D
	# @param portID  DAQ port ID where the FEB/D is connected
	# @param slaveID Slave ID on the FEB/D chain
        # @param cfgFunctionID Information for the FPGA firmware regarding the type of command being transmitted
	# @param payload The actual command to be transmitted
        # @param maxTries The maximum number of attempts to send the command without obtaining a valid reply   	
	def sendCommand(self, portID, slaveID, cfgFunctionID, payload, maxTries=10):
		nTries = 0;
		reply = None
		doOnce = True
		while doOnce or (reply == None and nTries < maxTries):
			doOnce = False

			nTries = nTries + 1
			if nTries > 5: print "Timeout sending command. Retry %d of %d" % (nTries, maxTries)

			sn = self.__lastSN
			self.__lastSN = (sn + 1) & 0x7FFF

			rawFrame = bytearray([ portID & 0xFF, slaveID & 0xFF] + [ (sn >> (8*n)) & 0xFF for n in range(16) ] + [ cfgFunctionID]) + payload
			rawFrame = str(rawFrame)

			#print [ hex(ord(x)) for x in rawFrame ]

			template1 = "@HH"
			n = struct.calcsize(template1) + len(rawFrame)
			data = struct.pack(template1, 0x05, n)
			self.__socket.send(data)
			self.__socket.send(rawFrame);

			template2 = "@H"
			n = struct.calcsize(template2)
			data = self.__socket.recv(n)
			nn, = struct.unpack(template2, data)

			if nn < 18:
				continue
			data = self.__socket.recv(nn)
			data = data[17:]
			reply = bytearray(data)
			

		if reply == None:
			print reply
			raise CommandErrorTimeout(portID, slaveID)

		return reply	

	## Writes in the FPGA register (Clock frequency, etc...)
	# @param regNum Identification of the register to be written
	# @param regValue The value to be written
	def setSI53xxRegister(self, regNum, regValue):
		reply = self.sendCommand(0, 0, 0x02, bytearray([0b00000000, regNum]))	
		reply = self.sendCommand(0, 0, 0x02, bytearray([0b01000000, regValue]))
		reply = self.sendCommand(0, 0, 0x02, bytearray([0b10000000]))
		return None

	## Defines all possible commands structure that can be sent to the ASIC and calls for sendCommand to actually transmit the command
        # @param asicID Identification of the ASIC that will receive the command
	# @param command Command type to be sent. The list of possible keys for this parameter is hardcoded in this function
        # @param value The actual value to be transmitted to the ASIC if it applies to the command type   
        # @param channel If the command is destined to a specific channel, this parameter sets its ID. 	  
	def __doAsicCommand(self, portID, slaveID, chipID, command, value=None, channel=None):
		nTry = 0
		while True:
			try:
				return self.___doAsicCommand(portID, slaveID, chipID, command, value=value, channel=channel)
			except tofpet2b.ConfigurationError as e:
				nTry = nTry + 1
				if nTry >= 5:
					raise e


	def ___doAsicCommand(self, portID, slaveID, chipID, command, value=None, channel=None):
		commandInfo = {
		#	commandID 	: (code,   ch,   read, data length)
			"wrChCfg"	: (0b0000, True, False, 125),
			"rdChCfg"	: (0b0001, True, True, 125),
			"wrGlobalCfg" 	: (0b1000, False, False, 184),
			"rdGlobalCfg" 	: (0b1001, False, True, 184)
		}
	
		commandCode, isChannel, isRead, dataLength = commandInfo[command]


		ccBits = bitarray_utils.intToBin(commandCode, 4)

		if isChannel:
			ccBits += bitarray_utils.intToBin(channel, 7)

		if not isRead:
			assert len(value) == dataLength
			ccBits += value

		nBytes = int(math.ceil(len(ccBits) / 8.0))
		paddedValue = ccBits + bitarray([ False for x in range((nBytes * 8) - dataLength) ])
		byteX = [ ord(x) for x in paddedValue.tobytes() ]		
		
		if isRead:
			bitsToRead = dataLength
		else:
			bitsToRead = 0

		nBitsToWrite= len(ccBits)
		cmd = [ chipID, nBitsToWrite, bitsToRead] + byteX
		cmd = bytearray(cmd)

		reply = self.sendCommand(portID, slaveID, 0x01, cmd)
		if len(reply) < 1: raise tofpet2b.ConfigurationErrorBadReply(1, len(reply))

		status = reply[0]
			
		if status == 0xE3:
			raise tofpet2b.ConfigurationErrorBadAck(portID, slaveID, chipID, 0)
		elif status == 0xE4:
			raise tofpet2b.ConfigurationErrorBadCRC(portID, slaveID, chipID )
		elif status == 0xE5:
			raise tofpet2b.ConfigurationErrorBadAck(portID, slaveID, chipID, 1)
		elif status != 0x00:
			raise tofpet2b.ConfigurationErrorGeneric(portID, slaveID, chipID , status)

		if isRead:
			expectedBytes = math.ceil(dataLength/8)
			if len(reply) < (1+expectedBytes): 
				print len(reply), (1+expectedBytes)
				raise tofpet2b.ConfigurationErrorBadReply(2+expectedBytes, len(reply))
			reply = str(reply[1:])
			data = bitarray()
			data.frombytes(reply)
			value = data[0:dataLength]
			return (status, value)
		else:
			# Check what we wrote
			readCommand = 'rd' + command[2:]
			readStatus, readValue = self.__doAsicCommand(portID, slaveID, chipID, readCommand, channel=channel)
			if readValue != value:
				raise tofpet2b.ConfigurationErrorBadRead(portID, slaveID, chipID, value, readValue)

			return (status, None)

	## Returns the configuration set in the ASICs registers as a dictionary of
	## - key: a tuple (portID, slaveID, chipID)
	## - value: a tofpet2.AsicConfig object
	## @param forceAccess Ignores the software cache and forces hardware access.
	def getAsicsConfig(self, forceAccess=False):
		if (forceAccess is True) or (self.__asicConfigCache is None):
			# Build the ASIC configuration cache
			self.__asicConfigCache = {}
			self.__asicConfigCache_TAC_Refresh = set()
			
			for portID, slaveID, chipID in self.getActiveAsics():
				if self.getAsicSubtype(portID, slaveID, chipID) == "2B":
					tofpet2 = tofpet2b
				else:
					tofpet2 = tofpet2c
					
				ac = tofpet2.AsicConfig()
				status, value = self.__doAsicCommand(portID, slaveID, chipID, "rdGlobalCfg")
				ac.globalConfig = tofpet2.AsicGlobalConfig(value)
				for n in range(64):
					status, value = self.__doAsicCommand(portID, slaveID, chipID, "rdChCfg", channel=n)
					ac.channelConfig[n] = tofpet2.AsicChannelConfig(value)
					
				# Store the ASIC configuration
				self.__asicConfigCache[(portID, slaveID, chipID)] = ac
				tacRefreshPeriod_1 = ac.globalConfig.getValue("tac_refresh_period")
				# Store a set of ASIC refresh settings
				tacRefreshPeriod_2 = ac.channelConfig[n].getValue("tac_max_age")
				self.__asicConfigCache_TAC_Refresh.add((tacRefreshPeriod_1, tacRefreshPeriod_2))
				
		return deepcopy(self.__asicConfigCache)

	## Sets the configuration into the ASICs registers
	# @param config is a dictionary, with the same form as returned by getAsicsConfig
	# @param forceAccess Ignores the software cache and forces hardware access.
	def setAsicsConfig(self, config, forceAccess=False):
		# If the ASIC config cache does not exist, make sure it exists
		if self.__asicConfigCache is None:
			self.getAsicsConfig()
		
		tacRefreshHardwareUpdated = False
		for portID, slaveID, chipID in self.getActiveAsics():
			cachedAC = self.__asicConfigCache[(portID, slaveID, chipID)]
			newAC = config[(portID, slaveID, chipID)]
		   
			cachedGC = cachedAC.globalConfig
			newGC = newAC.globalConfig
			
			if (forceAccess is True) or (newGC != cachedGC):
				self.__doAsicCommand(portID, slaveID, chipID, "wrGlobalCfg", value=newGC)
				if (forceAccess is True) or (cachedGC.getValue("tac_refresh_period") != newGC.getValue("tac_refresh_period")):
					tacRefreshHardwareUpdated = True
				cachedAC.globalConfig = deepcopy(newGC)
			
			for channelID in range(64):
				cachedCC = cachedAC.channelConfig[channelID]
				newCC = newAC.channelConfig[channelID]
				
				if (forceAccess is True) or (newCC != cachedCC):
					self.__doAsicCommand(portID, slaveID, chipID, "wrChCfg", channel=channelID, value=newCC)
					if (forceAccess is True) or (cachedCC.getValue("tac_max_age") != newCC.getValue("tac_max_age")):
						tacRefreshHardwareUpdated = True
					cachedAC.channelConfig[channelID] = deepcopy(newCC)
				
		if tacRefreshHardwareUpdated:
			# Rebuild the TAC refresh settings summary cache
			self.__asicConfigCache_TAC_Refresh = set()
			for portID, slaveID, chipID in self.getActiveAsics():
				cachedAC = self.__asicConfigCache[(portID, slaveID, chipID)]
				cachedGC = cachedAC.globalConfig
				for channelID in range(64):
					cachedCC = cachedAC.channelConfig[channelID]
					tacRefreshPeriod_1 = cachedGC.getValue("tac_refresh_period")
					tacRefreshPeriod_2 = cachedCC.getValue("tac_max_age")
					self.__asicConfigCache_TAC_Refresh.add((tacRefreshPeriod_1, tacRefreshPeriod_2))
					
			
				
		return None
	
	def getBiasType(self, portID, slaveID):
		r = self.__getActiveFEBDs()
		(bias_type, ) = r[(portID, slaveID)]
		return bias_type
	
	def __setAllBiasToZero(self):
		for portID, slaveID, channelID in self.getActiveBiasChannels():
			# 0 works for all types of bias mezzanines
			self.__write_hv_channel(portID, slaveID, channelID, 0, forceAccess=True)

	def __write_hv_channel(self, portID, slaveID, channelID, value, forceAccess=False):
		cacheKey = (portID, slaveID, channelID)
		if not forceAccess:
			try:
				lastValue = self.__hvdac_config_cache[cacheKey]
				if value == lastValue:
					return 0
			except KeyError:
				pass
		
		bias_type = self.getBiasType(portID, slaveID)
		if bias_type == 1:
			self.__write_hv_ad5535(portID, slaveID, channelID, value)
		else:
			self.__write_hv_ltc2668(portID, slaveID, channelID, value)
		self.__hvdac_config_cache[cacheKey] = value
		return 0


	## Returns the last value written in the bias voltage channel registers as a dictionary of
	## - key: a tupple of (portID, slaveID, channelID)
	## - value: an integer
	## WARNING: As the hardware does not support readback, this always returns the software cache
	def get_hvdac_config(self):
		return deepcopy(self.__hvdac_config_cache)

	## Sets the bias voltage channels
	# @param is a dictionary, as returned by get_hvdac_config
	# @param forceAccess Ignores the software cache and forces hardware access.
	def set_hvdac_config(self, config, forceAccess=False):
		for portID, slaveID, channelID in self.getActiveBiasChannels():
                        value = config[(portID, slaveID, channelID)]
			self.__write_hv_channel(portID, slaveID, channelID, value, forceAccess=forceAccess)
		

	def openRawAcquisition(self, fileNamePrefix, calMode = False):
                
                asicsConfig = self.getAsicsConfig()
		if fileNamePrefix != "/dev/null":
                	modeFile = open(fileNamePrefix + ".modf", "w")
		else:
			modeFile = open("/dev/null", "w")

                modeFile.write("#portID\tslaveID\tchipID\tchannelID\tmode\n")
                modeList = [] 
                for portID, slaveID, chipID in asicsConfig.keys():
                        ac = asicsConfig[(portID, slaveID, chipID)]
                        for channelID in range(64):
                                cc = ac.channelConfig[channelID]
                                mode = cc.getValue("qdc_mode") and "qdc" or "tot"
                                modeList.append(mode)
                                modeFile.write("%d\t%d\t%d\t%d\t%s\n" % (portID, slaveID, chipID, channelID, mode))
                                
                if(len(set(modeList))!=1):
                        qdcMode = "mixed"
                elif(modeList[0] == "tot"):
                        qdcMode = "tot" 
                else:
                        qdcMode = "qdc" 

                modeFile.close() 
        
		triggerID = -1
		trigger = self.getTriggerUnit()
		if trigger is None:
			triggerID = -1
		elif [ trigger ] == self.getActiveFEBDs():
			triggerID = 1
		else:
			portID, slaveID = trigger
			triggerID = 32 * portID + slaveID
		
		cmd = [ "./write_raw", \
			self.__shmName, \
			fileNamePrefix, \
			str(int(self.__systemFrequency)), \
			str(qdcMode), "%1.12f" % self.getAcquisitionStartTime(),
			calMode and 'T' or 'N', 
			str(triggerID) ]
		self.__helperPipe = subprocess.Popen(cmd, bufsize=1, stdin=subprocess.PIPE, stdout=subprocess.PIPE, close_fds=True)


	## Closes the current acquisition file
	def closeAcquisition(self):
		self.__helperPipe.terminate()
		sleep(0.5)
		self.__helperPipe.kill()
		self.__helperPipe = None


        ## Acquires data and decodes it, while writting through the acquisition pipeline 
        # @param step1 Tag to a given variable specific to this acquisition 
        # @param step2 Tag to a given variable specific to this acquisition
        # @param acquisitionTime Acquisition time in seconds 
	def acquire(self, acquisitionTime, step1, step2):
		(pin, pout) = (self.__helperPipe.stdin, self.__helperPipe.stdout)
		frameLength = 1024.0 / self.__systemFrequency
		nRequiredFrames = int(acquisitionTime / frameLength)

		template1 = "@ffIIi"
		template2 = "@I"
		n1 = struct.calcsize(template1)
		n2 = struct.calcsize(template2)

 		self.__synchronizeDataToConfig()
		wrPointer, rdPointer = (0, 0)
		while wrPointer == rdPointer:
			wrPointer, rdPointer = self.__getDataFrameWriteReadPointer()

		bs = self.__shm.getSizeInFrames()
		index = rdPointer % bs
		startFrame = self.__shm.getFrameID(index)
		stopFrame = startFrame + nRequiredFrames

                t0 = time()
		nBlocks = 0
		currentFrame = startFrame
		nFrames = 0
		lastUpdateFrame = currentFrame
		while currentFrame < stopFrame:
			wrPointer, rdPointer = self.__getDataFrameWriteReadPointer()
			while wrPointer == rdPointer:
				wrPointer, rdPointer = self.__getDataFrameWriteReadPointer()

			nFramesInBlock = abs(wrPointer - rdPointer)
			if nFramesInBlock > bs:
				nFramesInBlock = 2*bs - nFramesInBlock

                        # Don't use more frames than needed
                        framesToTarget = stopFrame - currentFrame
                        if nFramesInBlock > framesToTarget:
                                nFramesInBlock = framesToTarget


			# Do not feed more than bs/2 frame blocks to writeRaw in a single call
			# Because the entire frame block won't be freed until writeRaw is done, we can end up in a situation
			# where writeRaw owns all frames and daqd has no buffer space, even if writeRaw has already processed 
			# some/most of the frame block
			if nFramesInBlock > bs/2:
                                nFramesInBlock = bs/2

                        wrPointer = (rdPointer + nFramesInBlock) % (2*bs)

			data = struct.pack(template1, step1, step2, wrPointer, rdPointer, 0)
			pin.write(data); pin.flush()
			
			data = pout.read(n2)
			rdPointer,  = struct.unpack(template2, data)

			index = (rdPointer + bs - 1) % bs
			currentFrame = self.__shm.getFrameID(index)

			self.__setDataFrameReadPointer(rdPointer)

			nFrames = currentFrame - startFrame + 1
			nBlocks += 1
			if (currentFrame - lastUpdateFrame) * frameLength > 0.1:
				t1 = time()
				stdout.write("Python:: Acquired %d frames in %4.1f seconds, corresponding to %4.1f seconds of data (delay = %4.1f)\r" % (nFrames, t1-t0, nFrames * frameLength, (t1-t0) - nFrames * frameLength))
				stdout.flush()
				lastUpdateFrame = currentFrame
		t1 = time()
		print "Python:: Acquired %d frames in %4.1f seconds, corresponding to %4.1f seconds of data (delay = %4.1f)" % (nFrames, time()-t0, nFrames * frameLength, (t1-t0) - nFrames * frameLength)

		data = struct.pack(template1, step1, step2, wrPointer, rdPointer, 1)
		pin.write(data); pin.flush()

		data = pout.read(n2)
		rdPointer,  = struct.unpack(template2, data)
		self.__setDataFrameReadPointer(rdPointer)
		
		# Check ASIC link status at end of acquisition
		self.checkAsicRx()

		return None
	
	def checkAsicRx(self):
		bad_rx_found = False
		for portID, slaveID in self.getActiveFEBDs():
			asic_enable_vector = self.read_config_register(portID, slaveID, 64, 0x0318)
			asic_deserializer_vector =  self.read_config_register(portID, slaveID, 64, 0x0302)
			asic_decoder_vector = self.read_config_register(portID, slaveID, 64, 0x0310)
			
			asic_bad_rx = asic_enable_vector & ~(asic_deserializer_vector & asic_decoder_vector)
			
			for n in range(64):
				if (asic_bad_rx & (1 << n)) != 0:
					a = (asic_deserializer_vector >> n) & 1
					b = (asic_decoder_vector >> n) & 1
					print "ASIC (%2d, %2d, %2d) RX links are down (0b%d%d)" % (portID, slaveID, n, b, a)
					bad_rx_found = True
					
		if bad_rx_found:
			raise ErrorAsicLinkDown()

	## Gets the current write and read pointer
	def __getDataFrameWriteReadPointer(self):
		template = "@HH"
		n = struct.calcsize(template)
		data = struct.pack(template, 0x03, n);
		self.__socket.send(data)

		template = "@HII"
		n = struct.calcsize(template)
		data = self.__socket.recv(n);
		n, wrPointer, rdPointer = struct.unpack(template, data)

		return wrPointer, rdPointer

	def __setDataFrameReadPointer(self, rdPointer):
		template1 = "@HHI"
		n = struct.calcsize(template1) 
		data = struct.pack(template1, 0x04, n, rdPointer);
		self.__socket.send(data)

		template = "@I"
		n = struct.calcsize(template)
		data2 = self.__socket.recv(n);
		r2, = struct.unpack(template, data2)
		assert r2 == rdPointer

		return None
		
        ## Returns a data frame read form the shared memory block
	def __getDecodedDataFrame(self, nonEmpty=False):
		timeout = 0.5
		t0 = time()
		r = None
		while (r == None) and ((time() - t0) < timeout):
			wrPointer, rdPointer = self.__getDataFrameWriteReadPointer()
			bs = self.__shm.getSizeInFrames()
			while (wrPointer != rdPointer) and (r == None):
				index = rdPointer % bs
				rdPointer = (rdPointer + 1) % (2 * bs)

				nEvents = self.__shm.getNEvents(index)
				if nEvents == 0 and nonEmpty == True:
					continue;

				frameID = self.__shm.getFrameID(index)
				frameLost = self.__shm.getFrameLost(index)

				events = []
				for i in range(nEvents):
					events.append((	self.__shm.getChannelID(index, i), \
							self.__shm.getTacID(index, i), \
							self.__shm.getTCoarse(index, i), \
							self.__shm.getECoarse(index, i), \
							self.__shm.getTFine(index, i), \
							self.__shm.getEFine(index, i), \
						))

				r = { "id" : frameID, "lost" : frameLost, "events" : events }

			self.__setDataFrameReadPointer(rdPointer)

		return r

	## Discards all data frames which may have been generated before the function is called. Used to synchronize data reading with the effect of previous configuration commands.
	def __synchronizeDataToConfig(self, clearFrames=True):
		frameLength = 1024 / self.__systemFrequency

		# Check ASIC link status at start of acquisition
		# but wait for  firmware has finshed sync'ing after config and locking
		# - 8 frames for resync
		# - 4 frames for lock
		sleep(12 * frameLength)
		self.checkAsicRx()

		while True:	
			targetFrameID = self.getCurrentTimeTag() / 1024
			#print "Waiting for frame %1d" % targetFrameID
			while True:
				df = self.__getDecodedDataFrame()
				assert df != None
				if df == None:
					continue;

				if  df['id'] > targetFrameID:
					#print "Found frame %d (%f)" % (df['id'], df['id'] * frameLength)
					break

				# Set the read pointer to write pointer, in order to consume all available frames in buffer
				wrPointer, rdPointer = self.__getDataFrameWriteReadPointer();
				self.__setDataFrameReadPointer(wrPointer);

			# Do this until we took less than 100 ms to sync
			currentFrameID = self.getCurrentTimeTag() / 1024
			if (currentFrameID - targetFrameID) * frameLength < 0.100:
				break

		t0 = time()
		# For 50 ms, dump all data
		while (time() - t0) < 0.050:
			# Set the read pointer to write pointer, in order to consume all available frames in buffer
			wrPointer, rdPointer = self.__getDataFrameWriteReadPointer();
			self.__setDataFrameReadPointer(wrPointer);

		return

	def getCurrentTimeTag(self):
		triggerUnit = self.getTriggerUnit()
		if triggerUnit is not None:
			portID, slaveID = triggerUnit
		else:
			activeFEBD = self.getActiveFEBDs()
			if activeFEBD != []:
				portID, slaveID = activeFEBD[0]
			else:
				raise ErrorNoFEB()

		return self.read_config_register(portID, 0, 46, 0x0203)


	def getAcquisitionStartTime(self):
		currentTimeTag = self.getCurrentTimeTag()
		currentTime = time()
		return currentTime - currentTimeTag / self.__systemFrequency

	def __max111xx_ll(self, portID, slaveID, cfgFunctionID, chipID, command):
		w = 8 * len(command)
		padding = [0xFF for n in range(2) ]
		p = 8 * len(padding)

		# Pad the cycle with zeros
		return self.spi_master_execute(portID, slaveID, cfgFunctionID, chipID, 
			p+w+p, 		# cycle
			p,p+w, 		# sclk en
			0,p+w+p,	# cs
			0, p+w+p, 	# mosi
			p,p+w, 		# miso
			padding + command + padding)

	def fe_temp_check_max1111xx(self, portID, slaveID, chipID):
		m_config1 = 0x00008064  # single end ref; no avg; scan 16; normal power; echo on
		m_config2 = 0x00008800  # single end channels (0/1 -> 14/15, pdiff_com)
		m_config3 = 0x00009000  # unipolar convertion for channels (0/1 -> 14/15)
		m_control = 0x00000826  # manual external; channel 0; reset FIFO; normal power; ID present; CS control

		#reply = self.sendCommand(portID, slaveID, 0x04, bytearray([chipID, (m_config1 >> 8) & 0xFF, m_config1 & 0xFF]))
		#reply = self.sendCommand(portID, slaveID, 0x04, bytearray([chipID, (m_config2 >> 8) & 0xFF, m_config2 & 0xFF]))
		#reply = self.sendCommand(portID, slaveID, 0x04, bytearray([chipID, (m_config3 >> 8) & 0xFF, m_config3 & 0xFF]))
		reply = self.__max111xx_ll(portID, slaveID, 0x02, chipID, [(m_config1 >> 8) & 0xFF, m_config1 & 0xFF])
		reply = self.__max111xx_ll(portID, slaveID, 0x02, chipID, [(m_config2 >> 8) & 0xFF, m_config2 & 0xFF])
		reply = self.__max111xx_ll(portID, slaveID, 0x02, chipID, [(m_config3 >> 8) & 0xFF, m_config3 & 0xFF])
		
		if reply[1] == 0xFF and reply[2] == 0xFF: 
			return False
		
		if not (reply[1] == 0x88 and reply[2] == 0x0): 
			return False

		#reply = self.sendCommand(portID, slaveID, 0x04, bytearray([chipID, (m_control >> 8) & 0xFF, m_control & 0xFF]))
		reply = self.__max111xx_ll(portID, slaveID, 0x02, chipID, [(m_control >> 8) & 0xFF, m_control & 0xFF])
		if not(reply[1] == 0x90 and reply[2] == 0x0): 
			return False
		
		return True
	
	def fe_temp_read_max111xx(self, portID, slaveID, chipID, channelID):
		m_control = 0x00000826  # manual external; channel 0; reset FIFO; normal power; ID present; CS control
		m_repeat = 0x00000000
		
		command = m_control + (channelID << 7)
		#reply = self.sendCommand(portID, slaveID, 0x04, bytearray([chipID, (command >> 8) & 0xFF, command & 0xFF]))
		#reply = self.sendCommand(portID, slaveID, 0x04, bytearray([chipID, (m_repeat >> 8) & 0xFF, m_repeat & 0xFF]))
		reply = self.__max111xx_ll(portID, slaveID, 0x02, chipID, [(command >> 8) & 0xFF, command & 0xFF])
		reply = self.__max111xx_ll(portID, slaveID, 0x02, chipID, [(m_repeat >> 8) & 0xFF, m_repeat & 0xFF])
		v = reply[1] * 256 + reply[2]
		u = v & 0b111111111111
		ch = (v >> 12)
		assert ch == channelID
		return u
	
	## Initializes the temperature sensors in the FEB/As
	# Return the number of active sensors found in FEB/As
	def fe_temp_enumerate_tmp104(self, portID, slaveID):
		din = [ 3, 0x55, 0b10001100, 0b10010000 ]
		din = bytearray(din)
		dout = self.sendCommand(portID, slaveID, 0x04, din)

		if len(dout) < 4:
			# Reply is too short, chain is probably open
			raise TMP104CommunicationError(portID, slaveID, din, dout)
		
		if (dout[1:2] != din[1:2]) or ((dout[3] & 0xF0) != din[3]):
			# Reply does not match what is expected; a sensor is probably broken
			raise TMP104CommunicationError(portID, slaveID, din, dout)

		nSensors = dout[3] & 0x0F
	
		din = [ 3, 0x55, 0b11110010, 0b01100011]
		din = bytearray(din)
		dout = self.sendCommand(portID, slaveID, 0x04, din)
		if len(dout) < 4:
			raise TMP104CommunicationError(portID, slaveID, din, dout)

		din = [ 2 + nSensors, 0x55, 0b11110011 ]
		din = bytearray(din)
		dout = self.sendCommand(portID, slaveID, 0x04, din)
		if len(dout) < (3 + nSensors):
			raise TMP104CommunicationError(portID, slaveID, din, dout)

		return nSensors

	## Reads the temperature found in the specified FEB/D
	# @param portID  DAQ port ID where the FEB/D is connected
	# @param slaveID Slave ID on the FEB/D chain
	# @param nSensors Number of sensors to read
	def fe_temp_read_tmp104(self, portID, slaveID, nSensors):
			din = [ 2 + nSensors, 0x55, 0b11110001 ]
			din = bytearray(din)
			dout = self.sendCommand(portID, slaveID, 0x04, din)
			if len(dout) < (3 + nSensors):
				raise TMP104CommunicationError(portID, slaveID, din, dout)

			temperatures = dout[3:]
			for i, t in enumerate(temperatures):
				if t > 127: t = t - 256
				temperatures[i] = t
			return temperatures
		
	## Returns a 3 element tupple with the number of transmitted, received, and error packets for a given port 
	# @param port The port for which to get the desired output 
	def getPortCounts(self, port):
		template = "@HHH"
		n = struct.calcsize(template)
		data = struct.pack(template, 0x07, n, port)
		self.__socket.send(data);

		template = "@HQQQ"
		n = struct.calcsize(template)
		data = self.__socket.recv(n);
		length, tx, rx, rxBad = struct.unpack(template, data)		
		return (tx, rx, rxBad)

	## Returns a 3 element tupple with the number of transmitted, received, and error packets for a given FEB/D
	# @param portID  DAQ port ID where the FEB/D is connected
	# @param slaveID Slave ID on the FEB/D chain
	def getFEBDCount1(self, portID, slaveID):
		mtx = self.read_config_register(portID, slaveID, 64, 0x0401)
		mrx = self.read_config_register(portID, slaveID, 64, 0x0409)
		mrxBad = self.read_config_register(portID, slaveID, 64, 0x0411)

		slaveOn = self.read_config_register(portID, slaveID, 1, 0x0400)

		stx = self.read_config_register(portID, slaveID, 64, 0x0419)
		srx = self.read_config_register(portID, slaveID, 64, 0x0421)
		srxBad = self.read_config_register(portID, slaveID, 64, 0x0429)

		return (mtx, mrx, mrxBad, slaveOn, stx, srx, srxBad)
	

## Exception: a command to FEB/D was sent but a reply was not received.
#  Indicates a communication problem.
class CommandErrorTimeout(Exception):
	def __init__(self, portID, slaveID):
		self.addr = portID, slaveID
	def __str__(self):
		return "Time out from FEB/D at port %2d, slave %2d" % self.addr
## Exception: The number of ASIC data links read from FEB/D is invalid.
#  Indicates a communication problem or bad firmware in FEB/D.
class ErrorInvalidLinks(Exception):
	def __init__(self, portID, slaveID, value):
		self.addr = value, portID, slaveID
	def __str__(self):
		return "Invalid NLinks value (%d) from FEB/D at port %2d, slave %2d" % self.addr
## Exception: the ASIC type ID read from FEB/D is invalid.
#  Indicates a communication problem or bad firmware in FEB/D.
class ErrorInvalidAsicType(Exception): 
	def __init__(self, portID, slaveID, value):
		self.addr = portID, slaveID, value
	def __str__(self):
		return "Invalid ASIC type FEB/D at port %2d, slave %2d: %016llx" % self.addr
## Exception: no active FEB/D was found in any port.
#  Indicates that no FEB/D is plugged and powered on or that it's not being able to establish a data link.
class ErrorNoFEB(Exception):
	def __str__(self):
		return "No active FEB/D on any port"

## Exception: testing for ASIC presence in FEB/D has returned a inconsistent result.
#  Indicates that there's a FEB/A board is not properly plugged or a hardware problem.
class ErrorAsicPresenceInconsistent(Exception):
	def __init__(self, lst):
		self.__lst = lst
	def __str__(self):
		return "%d ASICs have inconsistent presence detection results" % len(self.__lst)

## Exception: testing for ASIC presence in FEB/D has changed state after system initialization.
#  Indicates that there's a FEB/A board is not properly plugged or a hardware problem.
class ErrorAsicPresenceChanged(Exception):
	def __init__(self, portID, slaveID, asicID):
		self.__data = (portID, slaveID, asicID)
	def __str__(self):
		return "ASIC at port %2d, slave %2d, asic %2d changed state" % (self.__data)

## Exception: reading of ASIC on reset configuration yielded an unexpected valud
class ErrorAsicUnknownConfigurationAfterReset(Exception):
	def __init__(self, portID, slaveID, chipID, value):
		self.__data = (portID, slaveID, chipID, value)
	def __str__(self):
		return "ASIC at port %2d, slave %2d, asic %02d: unknown configuration after reset %s" % self.data
	
class TMP104CommunicationError(Exception):
	def __init__(self, portID, slaveID, din, dout):
		self.__portID = portID
		self.__slaveID = slaveID
		self.__din = din
		self.__dout = dout
	def __str__(self):
		return "TMP104 read error at port %d, slave %d. Debug information:\nIN  = %s\nOUT = %s" % (self.__portID, self.__slaveID, [ hex(x) for x in self.__din ], [ hex(x) for x in self.__dout ])

## Exception: main FPGA clock is not locked
class ClockNotOK(Exception):
	def __init__(self, portID, slaveID):
		self.__portID = portID
		self.__slaveID = slaveID
	def __str__(self):
		return "Clock not locked at port %d, slave %d" % (self.__portID, self.__slaveID)

## Exception: trying to set a unknown auxilliary I/O
class UnknownAuxIO(Exception):
	def __init__(self, which):
		self.__which = which
	def __str__(self):
		return "Unkown auxilliary I/O: %s" % self.__which

class ErrorAsicLinkDown(Exception):
	def __str__(self):
		return "ASIC RX link is unexpectedly down"
