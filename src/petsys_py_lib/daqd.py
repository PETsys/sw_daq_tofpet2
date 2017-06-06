# -*- coding: utf-8 -*-

## @package daqd
# Handles interaction with the system via daqd

import shm_raw
import tofpet2
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
		self.__activeFEBDs = []
		self.__activeAsics = []

		self.__asicConfigCache = {}
		self.__ad5553ConfigCache = {}

		self.__helperPipe = None

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

	## Returns an array of (portID, slaveID) for the active FEB/Ds (PAB) 
	def getActiveFEBDs(self):
		if self.__activeFEBDs == []:
			self.__activeFEBDs = self.__getActiveFEBDs()
		return self.__activeFEBDs

	def __getActiveFEBDs(self):
		r = []
		for portID in self.getActivePorts():
			r.append((portID, 0))
			slaveID = 0
			while self.readFEBDConfig(portID, slaveID, 0, 12) != 0x0:
				slaveID += 1
				r.append((portID, slaveID))
			
		return r		

	def getActiveAsics(self):
		return self.__activeAsics

	## Disables test pulse 
	def setTestPulseNone(self):
		self.__setSorterMode(True)
		for portID, slaveID in self.getActiveFEBDs():
			self.writeFEBDConfig(portID, slaveID, 0, 19, 0x0)
		return None

        ## Sets the properties of the internal FPGA pulse generator
        # @param length Sets the length of the test pulse, from 1 to 1023 clock periods. 0 disables the test pulse.
        # @param interval Sets the interval between test pulses. The actual interval will be (interval+1)*1024 clock cycles.
        # @param finePhase Defines the delay of the test pulse regarding the start of the frame, in units of 1/392 of the clock.
        # @param invert Sets the polarity of the test pulse: active low when ``True'' and active high when ``False''
	def setTestPulsePLL(self, length, interval, finePhase, invert=False):
		# Check that the pulse interval does not cause problem with the ASIC TAC refresh period
		problematicSettings = set()
		asicsConfig = self.getAsicsConfig()
		for ac in asicsConfig.values():
			gc = ac.globalConfig
			if gc.getValue("tac_refresh_en") == 0: continue
			tacRefreshPeriod_1 = gc.getValue("tac_refresh_period")
		 	for cc in ac.channelConfig:
				tacRefreshPeriod_2 = cc.getValue("tac_max_age")
				tacRefreshPeriod = 64 * (tacRefreshPeriod_1 + 1) * (tacRefreshPeriod_2 + 1)
				if interval % tacRefreshPeriod == 0:
					problematicSettings.add((tacRefreshPeriod, tacRefreshPeriod_1, tacRefreshPeriod_2))

		for tacRefreshPeriod, tacRefreshPeriod_1, tacRefreshPeriod_2 in problematicSettings:
			print "WARNING: Test pulse period %d is a multiple of TAC refresh period %d (%d %d)." % (interval, tacRefreshPeriod, tacRefreshPeriod_1, tacRefreshPeriod_2)

		finePhase = int(round(finePhase * 6*56))	# WARNING: This should be firmware dependent..
	
		if interval < (length + 1):
			raise "Interval (%d) must be greater than length (%d) + 1" % (interval, length)

		interval = interval - (length + 1)

		value = 0x1 << 63
		value |= (length & 0x3FF)
		value |= (interval & 0x1FFFFF) << 10
		value |= (finePhase & 0xFFFFFF) << 31
		if invert: value |= 1 << 61

		for portID, slaveID in self.getActiveFEBDs():
			self.writeFEBDConfig(portID, slaveID, 0, 19, value)
		return None

	def __setSorterMode(self, mode):
		pass

	def disableEventGate(self):
		pass

	def disableCoincidenceTrigger(self):
		pass


	## Reads a configuration register from a FEB/D
	# @param portID  DAQ port ID where the FEB/D is connected
	# @param slaveID Slave ID on the FEB/D chain
	# @param addr1 Register block (0..127)
	# @param addr2 Register address (0..255)
	def readFEBDConfig(self, portID, slaveID, addr1, addr2):
		header = [ 0x00 | (addr1 & 0x7F), (addr2 >> 8) & 0xFF, addr2 & 0xFF ]
		data = [ 0x00 for n in range(8)]
		command = bytearray(header + data)

		reply = self.sendCommand(portID, slaveID, 0x05, command);
		
		d = reply[2:]
		value = 0
                size = min(len(d),8)
		for n in range(size):#####  it was 8		
                        value = value + (d[n] << (8*n))
		return value

	## Writes a FEB/D configuration register
	# @param portID  DAQ port ID where the FEB/D is connected
	# @param slaveID Slave ID on the FEB/D chain
	# @param addr1 Register block (0..127)
	# @param addr2 Register address (0..255)
	# @param value The value to written
	def writeFEBDConfig(self, portID, slaveID, addr1, addr2, value):
		header = [ 0x80 | (addr1 & 0x7F), (addr2 >> 8) & 0xFF, addr2 & 0xFF ]
		data = [ value >> (8*n) & 0xFF for n in range(8) ]
		command = bytearray(header + data)

		reply = self.sendCommand(portID, slaveID, 0x05, command);
		
		d = reply[2:]
		value = 0
		for n in range(8):#####  it was 8		
			value = value + (d[n] << (8*n))
		return value


	## Sends the entire configuration (needs to be assigned to the abstract Connection.config data structure) to the ASIC and starts to write data to the shared memory block
        # @param maxTries The maximum number of attempts to read a valid dataframe after uploading configuration 
	def initializeSystem(self, maxTries = 5):
		# Stop the acquisition, if the system was still acquiring
		self.__setAcquisitionMode(0)

		activePorts = self.getActivePorts()
		print "INFO: active FEB/D on ports: ", (", ").join([str(x) for x in self.getActivePorts()])

		# Disable everything
		self.setTestPulseNone()
		self.disableEventGate()
		self.disableCoincidenceTrigger()

		# Check FEB/D board status
		for portID, slaveID in self.getActiveFEBDs():
			coreClockNotOK = self.readFEBDConfig(portID, slaveID, 0, 11)
			if coreClockNotOK != 0x0:
				raise ClockNotOK(portID, slaveID)

			asicType = self.readFEBDConfig(portID, slaveID, 0, 0)
			if asicType != 0x00010002:
				raise ErrorInvalidAsicType(portID, slaveID, asicType)

		# Reset the ASICs configuration
		for portID, slaveID in self.getActiveFEBDs(): self.writeFEBDConfig(portID, slaveID, 0, 18, 0b011);
		for portID, slaveID in self.getActiveFEBDs(): self.writeFEBDConfig(portID, slaveID, 0, 18, 0b001);
		for portID, slaveID in self.getActiveFEBDs(): self.writeFEBDConfig(portID, slaveID, 0, 18, 0b000);
		self.__asicConfigCache = {}

		# Check which ASICs react to the configuration
		asicConfigOK = [ False for x in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS) ]
		asicConfigByFEBD = {} # Store the default config we're uploading into each FEB/D
		
		for portID, slaveID in self.getActiveFEBDs():
			# Upload default configuration, adjusted for FEB/D firmware RX build
			gcfg = tofpet2.AsicGlobalConfig()
			tdc_clk_div, ddr, tx_nlinks = self.__getAsicLinkConfiguration(portID, slaveID)
			gcfg.setValue("tdc_clk_div", tdc_clk_div)
			gcfg.setValue("tx_ddr", ddr)
			gcfg.setValue("tx_nlinks", tx_nlinks)
			#  .. and with the TX logic to calibration
			gcfg.setValue("tx_mode", 0b01)
			asicConfigByFEBD[(portID, slaveID)] = gcfg
			
			ccfg= tofpet2.AsicChannelConfig()

			for chipID in range(MAX_CHIPS):
				try:
					self.__doAsicCommand(portID, slaveID, chipID, "wrGlobalCfg", value=gcfg)
					for n in range(64):
						self.__doAsicCommand(portID, slaveID, chipID, "wrChCfg", channel=n, value=ccfg)

					gID = chipID + MAX_CHIPS * slaveID + (MAX_CHIPS * MAX_SLAVES) * portID
					asicConfigOK[gID] = True
				except tofpet2.ConfigurationError as e:
					pass


		# (Locally) Sync ASICs
		for portID, slaveID in self.getActiveFEBDs(): self.writeFEBDConfig(portID, slaveID, 0, 18, 0b100);
		for portID, slaveID in self.getActiveFEBDs(): self.writeFEBDConfig(portID, slaveID, 0, 18, 0b000);
		# Generate local sync (if available) and start acquisition
		self.__setAcquisitionMode(1)
		sleep (0.120)	# Sync is at least 100 ms

		# Check that the ASIC configuration has not changed after sync
		for portID, slaveID in self.getActiveFEBDs():
			for chipID in range(MAX_CHIPS):
				gID = chipID + MAX_CHIPS * slaveID + (MAX_CHIPS * MAX_SLAVES) * portID
				if not asicConfigOK[gID]: continue

				status, readback = self.__doAsicCommand(portID, slaveID, chipID, "rdGlobalCfg")
				if readback != asicConfigByFEBD[(portID, slaveID)]:
					raise tofpet2.ConfigurationErrorBadRead(portID, slaveID, i, asicConfigByFEBD[(portID, slaveID)], readback)
			
		
		# Set ASIC receiver logic to calibration mode
		for portID, slaveID in self.getActiveFEBDs(): self.writeFEBDConfig(portID, slaveID, 0, 4, 0b0)
		for portID, slaveID in self.getActiveFEBDs(): self.writeFEBDConfig(portID, slaveID, 0, 4, 0b1)
		# Allow some time for the IDELAY adjustment
		sleep(10 * 2**14 / self.__systemFrequency + 0.010)
		# Set ASIC receiver logic to normal mode
		for portID, slaveID in self.getActiveFEBDs(): self.writeFEBDConfig(portID, slaveID, 0, 4, 0b0)

		# Reconfigure ASICs TX to normal mode
		for portID, slaveID in self.getActiveFEBDs():
			# Same configuration as before...
			gcfg = asicConfigByFEBD[(portID, slaveID)]
			# .. but with the TX logic to normal
			gcfg.setValue("tx_mode", 0b10)
			for chipID in range(MAX_CHIPS):
				gID = chipID + MAX_CHIPS * slaveID + (MAX_CHIPS * MAX_SLAVES) * portID
				if not asicConfigOK[gID]: continue
				self.__doAsicCommand(portID, slaveID, chipID, "wrGlobalCfg", value=gcfg)

		# Allow some ms for the deserializer to lock to the 8B/10B pattern
		sleep(0.010)

		# Check which ASICs are receiving properly data words
		deserializerStatus = [ False for x in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS) ]
		decoderStatus = [ False for x in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS) ]

		for portID, slaveID in self.getActiveFEBDs():
			lDeserializerStatus = self.readFEBDConfig(portID, slaveID, 0, 2)
			lDecoderStatus = self.readFEBDConfig(portID, slaveID, 0, 3)

			lDeserializerStatus = [ lDeserializerStatus & (1<<n) != 0 for n in range(MAX_CHIPS) ]
			lDecoderStatus = [ lDecoderStatus & (1<<n) != 0 for n in range(MAX_CHIPS) ]

			k = slaveID * MAX_CHIPS + portID * MAX_CHIPS * MAX_SLAVES
			for n in range(MAX_CHIPS):
				deserializerStatus[k+n] = lDeserializerStatus[n]
				decoderStatus[k+n] = lDecoderStatus[n]

		self.__activeAsics = []
		inconsistentStateAsics = []
		for gID in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS):
			statusTripplet = (asicConfigOK[gID], deserializerStatus[gID], decoderStatus[gID])
			chipID = gID % MAX_CHIPS
			slaveID = (gID / MAX_CHIPS) % MAX_SLAVES
			portID = gID / (MAX_CHIPS * MAX_SLAVES)

			if statusTripplet == (True, True, True):
				# All OK, ASIC is present and OK
				self.__activeAsics.append((portID, slaveID, chipID))
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
					print " FEB/D port %2d slave %2d" % (portID, slaveID)
					for chipID, statusTripplet in lst:
						a, b, c = statusTripplet
						a = a and "OK" or "FAIL"
						b = b and "OK" or "FAIL"
						c = c and "OK" or "FAIL"						
						print "  ASIC %2d: config link: %4s data link pattern: %4s data word: %4s" % (chipID, a, b, c)
			
			if maxTries > 1: 
				print "Retrying..."
				self.initializeSystem(maxTries - 1)
			else:
				raise ErrorAsicPresenceInconsistent(inconsistentStateAsics)

		self.__setSorterMode(True)

		print "INFO: Active ASICs found:"
		for portID, slaveID in self.getActiveFEBDs():
			lst = []
			for lPortID, lSlaveID, lChipID in self.getActiveAsics():
				if lPortID != portID or lSlaveID != slaveID: continue
				lst.append(lChipID)

			if lst != []:
				lst = (", ").join([str(lChipID) for lChipID in lst])
				print " FEB/D port %2d slave %2d: %s" % (portID, slaveID, lst)

		for portID, slaveID in self.getActiveFEBDs():
			for n in range(64):
				self.__setAD5533Channel(portID, slaveID, n, 0)
		

	def __setAcquisitionMode(self, mode):
		template1 = "@HH"
		template2 = "@H"
		n = struct.calcsize(template1) + struct.calcsize(template2);
		data = struct.pack(template1, 0x01, n) + struct.pack(template2, mode)
		self.__socket.send(data)
		data = self.__socket.recv(2)
		assert len(data) == 2

	def __getAsicLinkConfiguration(self, portID, slaveID):
		data = self.readFEBDConfig(portID, slaveID, 0, 1)
		nLinks = data & 0xF
		if nLinks == 1:
			tx_nlinks = 0b00
		elif nLinks == 2:
			tx_nlinks = 0b01
		elif nLinks == 4:
			tx_nlinks = 0b10
		else:
			raise ErrorInvalidLinks(portID, slaveID, nLinks)

		speed = (data >> 4) & 0xF
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


	## Sends a command to the FEB/D
	# @param portID  DAQ port ID where the FEB/D is connected
	# @param slaveID Slave ID on the FEB/D chain
        # @param commandType Information for the FPGA firmware regarding the type of command being transmitted
	# @param payload The actual command to be transmitted
        # @param maxTries The maximum number of attempts to send the command without obtaining a valid reply   	
	def sendCommand(self, portID, slaveID, commandType, payload, maxTries=10):
		nTries = 0;
		reply = None
		doOnce = True
		while doOnce or (reply == None and nTries < maxTries):
			doOnce = False

			nTries = nTries + 1
			if nTries > 5: print "Timeout sending command. Retry %d of %d" % (nTries, maxTries)

			sn = self.__lastSN
			self.__lastSN = (sn + 1) & 0x7FFF

			rawFrame = bytearray([ portID & 0xFF, slaveID & 0xFF, (sn >> 8) & 0xFF, (sn >> 0) & 0xFF, commandType]) + payload
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

			if nn < 4:
				continue
			data = self.__socket.recv(nn)
			data = data[3:]
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
	def __doAsicCommand(self, portID, slaveID, chipID, command, value=None, channel=None, forceAccess=False):
		nTry = 0
		while True:
			try:
				return self.___doAsicCommand(portID, slaveID, chipID, command, value=value, channel=channel, forceAccess=forceAccess)
			except tofpet2.ConfigurationError as e:
				nTry = nTry + 1
				if nTry >= 5:
					raise e


	def ___doAsicCommand(self, portID, slaveID, chipID, command, value=None, channel=None, forceAccess=False):
		commandInfo = {
		#	commandID 	: (code,   ch,   read, data length)
			"wrChCfg"	: (0b0000, True, False, 125),
			"rdChCfg"	: (0b0001, True, True, 125),
			"wrGlobalCfg" 	: (0b1000, False, False, 184),
			"rdGlobalCfg" 	: (0b1001, False, True, 184)
		}
	
		commandCode, isChannel, isRead, dataLength = commandInfo[command]

		cacheKey = (command[2:], portID, slaveID, chipID, channel)
		if not forceAccess:
			# Let's see if we have this in cache
			if not isRead:
				try:
					lastValue = self.__asicConfigCache[cacheKey]
					if value == lastValue:
						return (0, None)
				except KeyError:
					pass
			else:
				try:
					lastValue = self.__asicConfigCache[cacheKey]
					return (0, lastValue)
				except KeyError:
					pass


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

		reply = self.sendCommand(portID, slaveID, 0x00, cmd)
		if len(reply) < 2: raise tofpet2.ConfigurationErrorBadReply(2, len(reply))
		status = reply[1]
			
		if status == 0xE3:
			raise tofpet2.ConfigurationErrorBadAck(portID, slaveID, chipID, 0)
		elif status == 0xE4:
			raise tofpet2.ConfigurationErrorBadCRC(portID, slaveID, chipID )
		elif status == 0xE5:
			raise tofpet2.ConfigurationErrorBadAck(portID, slaveID, chipID, 1)
		elif status != 0x00:
			raise tofpet2.ConfigurationErrorGeneric(portID, slaveID, chipID , status)

		if isRead:
			expectedBytes = math.ceil(dataLength/8)
			if len(reply) < (2+expectedBytes): 
				print len(reply), (2+expectedBytes)
				raise tofpet2.ConfigurationErrorBadReply(2+expectedBytes, len(reply))
			reply = str(reply[2:])
			data = bitarray()
			data.frombytes(reply)
			value = data[0:dataLength]
			self.__asicConfigCache[cacheKey] = bitarray(value)
			return (status, value)
		else:
			# Check what we wrote
			readCommand = 'rd' + command[2:]
			readStatus, readValue = self.__doAsicCommand(portID, slaveID, chipID, readCommand, channel=channel, forceAccess=True)
			if readValue != value:
				raise tofpet2.ConfigurationErrorBadRead(portID, slaveID, chipID, value, readValue)

			return (status, None)

	## Returns the configuration set in the ASICs registers as a dictionary of
	## - key: a tuple (portID, slaveID, chipID)
	## - value: a tofpet2.AsicConfig object
	## @param forceAccess Ignores the software cache and forces hardware access.
	def getAsicsConfig(self, forceAccess=False):
		data = {}
		for portID, slaveID, chipID in self.getActiveAsics():
			ac = tofpet2.AsicConfig()
			status, value = self.__doAsicCommand(portID, slaveID, chipID, "rdGlobalCfg", forceAccess=forceAccess)
			ac.globalConfig = tofpet2.AsicGlobalConfig(value)
			for n in range(64):
				status, value = self.__doAsicCommand(portID, slaveID, chipID, "rdChCfg", channel=n, forceAccess=forceAccess)
				ac.channelConfig[n] = tofpet2.AsicChannelConfig(value)
			data[(portID, slaveID, chipID)] = ac
		return data

	## Sets the configuration into the ASICs registers
	# @param config is a dictionary, with the same form as returned by getAsicsConfig
	# @param forceAccess Ignores the software cache and forces hardware access.
	def setAsicsConfig(self, config, forceAccess=False):
		for (portID, slaveID, chipID), ac in config.items():
			self.__doAsicCommand(portID, slaveID, chipID, "wrGlobalCfg", value=ac.globalConfig, forceAccess=forceAccess)
			for n in range(64):
				self.__doAsicCommand(portID, slaveID, chipID, "wrChCfg", channel=n, value=ac.channelConfig[n], forceAccess=forceAccess)

		return None

	## Sets a channel of the AD5535 DAC
	def __setAD5533Channel(self, portID, slaveID, channelID, value, forceAccess=False):
		cacheKey = (portID, slaveID, channelID)
		if not forceAccess:
			try:
				lastValue = self.__ad5553ConfigCache[cacheKey]
				if value == lastValue:
					return 0
			except KeyError:
				pass
		
		whichDAC = channelID / 32
		channel = channelID % 32
		whichDAC = 1 - whichDAC # Wrong decoding in ad5535.vhd
		dacBits = bitarray_utils.intToBin(whichDAC, 1) + bitarray_utils.intToBin(channel, 5) + bitarray_utils.intToBin(value, 14) + bitarray('0000')
		dacBytes = bytearray(dacBits.tobytes())
		self.sendCommand(portID, slaveID, 0x01, dacBytes)
		self.__ad5553ConfigCache[cacheKey] = value
		return 0

	## Returns the last value written in the bias voltage channel registers as a dictionary of
	## - key: a tupple of (portID, slaveID, channelID)
	## - value: an integer
	## WARNING: As the hardware does not support readback, this always returns the software cache
	def getAD5535Config(self):
		return deepcopy(self.__ad5553ConfigCache)

	## Sets the bias voltage channels
	# @param is a dictionary, as returned by getAD5535Config
	# @param forceAccess Ignores the software cache and forces hardware access.
	def setAD5535Config(self, config, forceAccess=False):
		for (portID, slaveID, channelID), value in config.items():
			self.__setAD5533Channel(portID, slaveID, channelID, value, forceAccess=forceAccess)
		

	def openRawAcquisition(self, fileNamePrefix, qdcMode=False):
		cmd = [ "./write_raw", self.__shmName, fileNamePrefix, str(int(self.__systemFrequency)), qdcMode and 'Q' or 'T' ]
		self.__helperPipe = subprocess.Popen(cmd, bufsize=1, stdin=subprocess.PIPE, stdout=subprocess.PIPE, close_fds=True)

        ## Acquires data and decodes it, while writting through the acquisition pipeline 
        # @param step1 Tag to a given variable specific to this acquisition 
        # @param step2 Tag to a given variable specific to this acquisition
        # @param acquisitionTime Acquisition time in seconds 
	def acquire(self, acquisitionTime, step1, step2):
		self.__synchronizeDataToConfig()

		#print "Python:: acquiring %f %f"  % (step1, step2)
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

			# Do not feed more than bs/2 frame blocks to writeRaw in a single call
			# Because the entire frame block won't be freed until writeRaw is done, we can end up in a situation
			# where writeRaw owns all frames and daqd has no buffer space, even if writeRaw has already processed 
			# some/most of the frame block
			if nFramesInBlock > bs/2:
				wrPointer = (rdPointer + bs/2) % (2*bs)

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

		return None

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
		while True:	
			targetFrameID = self.__getCurrentFrameID()
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
			currentFrameID = self.__getCurrentFrameID()
			if (currentFrameID - targetFrameID) * frameLength < 0.100:
				break
		

		return

	def __getCurrentFrameID(self):
		activePorts = self.getActivePorts()
		if activePorts == []:
			raise ErrorNoFEB()

		portID = min(activePorts)
		gray = self.readFEBDConfig(portID, 0, 0, 7)
		timeTag = bitarray_utils.binToInt(bitarray_utils.grayToBin(bitarray_utils.intToBin(gray, 46)))
		frameID = timeTag / 1024
		return frameID
	
	## Initializes the temperature sensors in the FEB/As
	# Return the number of active sensors found in FEB/As
	def getNumberOfTMP104(self, portID, slaveID):
		din = [ 3, 0x55, 0b10001100, 0b10010000 ]
		din = bytearray(din)
		dout = self.sendCommand(portID, slaveID, 0x04, din)
		if len(dout) < 5:
			raise TMP104CommunicationError(portID, slaveID, din, dout)
		
		nSensors = dout[4] & 0x0F
	
		din = [ 3, 0x55, 0b11110010, 0b01100011]
		din = bytearray(din)
		dout = self.sendCommand(portID, slaveID, 0x04, din)
		if len(dout) < 5:
			raise TMP104CommunicationError(portID, slaveID, din, dout)

		din = [ 2 + nSensors, 0x55, 0b11110011 ]
		din = bytearray(din)
		dout = self.sendCommand(portID, slaveID, 0x04, din)
		if len(dout) < (4 + nSensors):
			raise TMP104CommunicationError(portID, slaveID, din, dout)

		return nSensors

	## Reads the temperature found in the specified FEB/D
	# @param portID  DAQ port ID where the FEB/D is connected
	# @param slaveID Slave ID on the FEB/D chain
	# @param nSensors Number of sensors to read
	def getTMP104Readings(self, portID, slaveID, nSensors):
			din = [ 2 + nSensors, 0x55, 0b11110001 ]
			din = bytearray(din)
			dout = self.sendCommand(portID, slaveID, 0x04, din)
			if len(dout) < (4 + nSensors):
				raise TMP104CommunicationError(portID, slaveID, din, dout)

			temperatures = dout[4:]
			for i, t in enumerate(temperatures):
				if t > 127: t = t - 256
				temperatures[i] = t
			return temperatures

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
	
class TMP104CommunicationError(Exception):
	def __init__(self, portID, slaveID, din, dout):
		self.__portID = portID
		self.__slaveID = slaveID
		self.__din = din
		self.__dout = dout
	def __str__(self):
		return "TMP104 read error at port %d, slave %d. IN = %s, OUT = %s" % (self.__portID, self.__slaveID, [ hex(x) for x in self.__din ], [ hex(x) for x in self.__dout ])

## Exception: main FPGA clock is not locked
class ClockNotOK(Exception):
	def __init__(self, portID, slaveID):
		self.__portID = portID
		self.__slaveID = slaveID
	def __str__(self):
		return "Clock not locked at port %d, slave %d" % (self.__portID, self.__slaveID)
