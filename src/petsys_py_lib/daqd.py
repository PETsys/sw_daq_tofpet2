# kate: indent-mode: python; indent-pasted-text false; indent-width 8; replace-tabs: off;
# vim: tabstop=8 shiftwidth=8

## @package daqd
# Handles interaction with the system via daqd

from . import shm_raw
from . import tofpet2b
from . import tofpet2c
from . import info
from . import bias
from . import spi
from . import fe_power, fe_power_8k
import socket
from random import randrange
import struct
from time import sleep, time
from bitarray import bitarray
from . import bitarray_utils
import math
import subprocess
from sys import stdout
from copy import deepcopy
import os, stat, os.path

MAX_PORTS = 32
MAX_SLAVES = 32
MAX_CHIPS = 64

PROTOCOL_VERSION = 0x102

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
		self.__activeUnits = {}
		self.__activeBiasSlots = {}
		self.__activeAsics = {}

		self.__asicConfigCache = None
		self.__asicConfigCache_TAC_Refresh = None
		self.__hvdac_config_cache = {}
		self.__hvdac_max_values = {}

		self.__writerPipe = None
		self.__monitorPipe = None
		
		self.__temperatureSensorList = {}

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

	def getDAQTemp(self):
		template = "@HH"
		n = struct.calcsize(template)
		data = struct.pack(template, 0x14, n)
		self.__socket.send(data);

		template = "@HQ"
		n = struct.calcsize(template)
		data = self.__socket.recv(n);
		length, temps = struct.unpack(template, data)
		reply = [ ((temps >> n*16) & 0xFFFF)/100 for n in range(4)]
		return reply

	def getActiveUnits(self):
		if self.__activeUnits == {}: self.__scanUnits_ll()
		return sorted(self.__activeUnits.keys())

	def getTriggerUnit(self):
		if self.__activeUnits == {}: self.__scanUnits_ll()
		triggerUnits = [ a for a, d in self.__activeUnits.items() if info.is_trigger(d) ]

		if len(triggerUnits) == 0:
			return None
		elif len(triggerUnits) == 1:
			return triggerUnits[0]
		else:
			raise ErrorTooManyTriggerUnits(triggerUnits)

	## Returns an array of (portID, slaveID) for the active FEB/Ds (PAB) 
	def getActiveFEBDs(self):
		if self.__activeUnits == {}: self.__scanUnits_ll()
		febds = [ a for a, d in self.__activeUnits.items() if info.is_febd(d) ]
		return sorted(febds)

	def getUnitInfo(self, portID, slaveID):
		if self.__activeUnits == {}: self.__scanUnits_ll()
		try:
			return self.__activeUnits[(portID, slaveID)]
		except KeyError as e:
			raise ErrorUnitNotPresent(portID, slaveID)
	
	def __scanUnits_ll(self):
		activeUnits = {}
		activeBiasSlots = {}
		for portID in self.getActivePorts():
			slaveID = 0
			while True:
				protocol = self.read_config_register(portID, slaveID, 64, 0xFFF8)
				if protocol != PROTOCOL_VERSION:
					raise ErrorUnknownProtocol(portID, slaveID, protocol)

				base_pcb = self.read_config_register(portID, slaveID, 16, 0x0000)
				fw_variant = self.read_config_register(portID, slaveID, 64, 0x0008)
				prom_variant = None

				activeUnits[(portID, slaveID)] = (base_pcb, fw_variant, prom_variant)

				if info.is_febd((base_pcb, fw_variant, prom_variant)):
					for slotID in range(info.bias_slots((base_pcb, fw_variant, prom_variant))):
						activeBiasSlots[(portID, slaveID, slotID)] = bias.read_bias_slot_info(self, portID, slaveID, slotID)



				if self.read_config_register(portID, slaveID, 1, 0x0400) != 0b0:
					# This FEB/D has a slave
					slaveID += 1
				else:
					break

		self.__activeUnits = activeUnits
		self.__activeBiasSlots = activeBiasSlots

	def getActiveAsics(self):
		return sorted(self.__activeAsics.keys())

	def getAsicSubtype(self, portID, slaveID, chipID):
		return self.__activeAsics[(portID, slaveID, chipID)]
	
	def getActiveAsicsChannels(self):
		return [ (p, s, a, c) for c in range(64) for (p, s, a) in self.getActiveAsics() ]
	
	def getActiveBiasSlots(self):
		if self.__activeUnits == {}: self.__scanUnits_ll()
		return sorted(self.__activeBiasSlots.keys())

	def getBiasSlotInfo(self, portID, slaveID, slotID):
		if self.__activeUnits == {}: self.__scanUnits_ll()
		return self.__activeBiasSlots[(portID, slaveID, slotID)]

	def getActiveBiasChannels(self):
		if self.__activeUnits == {}: self.__scanUnits_ll()
		lst = []
		for portID, slaveID, slotID in self.getActiveBiasSlots():
			bias_slot_info = self.getBiasSlotInfo(portID, slaveID, slotID)
			n_ch = bias.get_number_channels(bias_slot_info)
			lst += [ (portID, slaveID, slotID, k) for k in range(n_ch) ]
		return lst

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
				print("WARNING: Test pulse period %d is a multiple of TAC refresh period %d (%d %d) in some ASICs." % (interval, tacRefreshPeriod, tacRefreshPeriod_1, tacRefreshPeriod_2))
		
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
		disableWord = 0b0000000000000000
		for portID, slaveID in self.getActiveFEBDs():
			self.write_config_register(portID, slaveID, 16, 0x0602, disableWord)
		if self.getTriggerUnit() is not None:
			portID, slaveID = self.getTriggerUnit()
			self.write_config_register(portID, slaveID, 16, 0x0602, disableWord)

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


	## Detects if there are any legacy FEM-128 connected to FEB/D 1K
	## and sets the legacy FEM bit for the respective ports
	def set_legacy_fem_mode(self, portID, slaveID):
		# Only FEB/D 1K supports legacy FEM-128
		if not info.allows_legacy_module(self.getUnitInfo(portID, slaveID)): return

		# FEB/D 1K has 8 ports
		for module_id in range(8):
			# Legacy FEM-128 have a MAX111xx ADC at device 4
			if spi.max111xx_check(self, portID, slaveID, module_id * 256 + 4):
				# Communication was successful, let's not do anything else
				continue

			# Communication failed, flip the mode bit for this port and try again
			port_bit = (1 << module_id)
			mode_vector = self.read_config_register(portID, slaveID, 8, 0x02B8)
			mode_vector = mode_vector ^ port_bit
			self.write_config_register(portID, slaveID, 8, 0x02B8, mode_vector)

			if spi.max111xx_check(self, portID, slaveID, module_id * 256 + 4):
				# Communication was successful, let's not do anything else
				continue

			# Communication failed too
			# Flip the mode bit back to the previous value
			mode_vector = self.read_config_register(portID, slaveID, 8, 0x02B8)
			mode_vector = mode_vector ^ port_bit
			self.write_config_register(portID, slaveID, 8, 0x02B8, mode_vector)


	## Sends the entire configuration (needs to be assigned to the abstract Connection.config data structure) to the ASIC and starts to write data to the shared memory block
	# @param maxTries The maximum number of attempts to read a valid dataframe after uploading configuration 
	def initializeSystem(self, maxTries = 5, skipFEM = False, power_lst = []):
		# Stop the acquisition, if the system was still acquiring
		self.__setAcquisitionMode(0)

		activePorts = self.getActivePorts()
		print("INFO: active units on ports: ", (", ").join([str(x) for x in self.getActivePorts()]))

		# Disable everything
		self.setTestPulseNone()
		self.disableEventGate()
		self.disableCoincidenceTrigger()

		# Set all bias to minimum
		# Setting to zero DAC but will be saturated by mezzanine specific code
		for portID, slaveID, slotID, channelID in self.getActiveBiasChannels():
			self.__write_hv_channel(portID, slaveID, slotID, channelID, 0, forceAccess=True)

		# Check FEB/D board status
		for portID, slaveID in self.getActiveFEBDs():
			pllLocked = self.read_config_register(portID, slaveID, 1, 0x200)
			if pllLocked != 0b1:
				raise ClockNotOK(portID, slaveID)

			asicType = self.read_config_register(portID, slaveID, 16, 0x0102)
			if asicType != 0x0002:
				raise ErrorInvalidAsicType(portID, slaveID, asicType)
			
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


		# Generate a local sync
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
			if [(portID,slaveID)] != self.getActiveFEBDs():
				print("INFO: TGR unit is (%2d, %2d)" % (portID, slaveID))
			else:
				print("INFO: Evaluation kit: FEB/D with GBE connection @ (%2d, %2d)" % (portID, slaveID))
			self.write_config_register(portID, slaveID, 2, 0x0201, 0b01)
			self.write_config_register(portID, slaveID, 2, 0x0201, 0b00)

		# Enable acquisition
		# This will include a 220 ms sleep period for daqd and the DAQ card to clear buffers
		self.__setAcquisitionMode(1)
		# Finally, disable sync reception
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 2, 0x0201, 0b00)

		if skipFEM:
			return None
			
		# Power on ASICs
		if not power_lst:
			for portID, slaveID in self.getActiveFEBDs(): fe_power.set_fem_power(self, portID, slaveID, "on")
		else:
			for portID, slaveID in power_lst: fe_power.set_fem_power(self, portID, slaveID, "on")

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
		initialAsicChannelConfig = {}

		asic_enable_mask = 0xFFFFFFFFFFFFFFFF
		if "PETSYS_ASIC_MASK" in os.environ.keys():
			asic_enable_mask = int(os.environ["PETSYS_ASIC_MASK"], base=16)
			print("WARING: PETSYS_ASIC_MASK has been set to 0x%016X, some ASICs may be masked OFF" % asic_enable_mask)

		for portID, slaveID in self.getActiveFEBDs():

			supportsTxCalibration2 = (self.read_config_register(portID, slaveID, 64, 0xFFF8) & 0x2)
			for chipID in range(MAX_CHIPS):
				if (asic_enable_mask & (1<<chipID)) == 0:
					continue
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
					if supportsTxCalibration2:
						gcfg.setValue("tx_mode", 0b10)
					else:
						gcfg.setValue("tx_mode", 0b01)
					ccfg.setValue("trigger_mode_1", 0b11)
					initialGlobalAsicConfig[(portID, slaveID, chipID)] = gcfg
					initialAsicChannelConfig[(portID, slaveID, chipID)] = ccfg

					self.__doAsicCommand(portID, slaveID, chipID, "wrGlobalCfg", value=gcfg)
					for n in range(64):
						self.__doAsicCommand(portID, slaveID, chipID, "wrChCfg", channel=n, value=ccfg)

					gID = chipID + MAX_CHIPS * slaveID + (MAX_CHIPS * MAX_SLAVES) * portID
					asicConfigOK[gID] = True
				except tofpet2b.ConfigurationError as e:
					pass




		# Check that the ASIC configuration has not changed after sync
		for portID, slaveID in self.getActiveFEBDs():
			for chipID in range(MAX_CHIPS):
				gID = chipID + MAX_CHIPS * slaveID + (MAX_CHIPS * MAX_SLAVES) * portID
				if not asicConfigOK[gID]: continue

				status, readback = self.__doAsicCommand(portID, slaveID, chipID, "rdGlobalCfg")
				if readback != initialGlobalAsicConfig[(portID, slaveID, chipID)]:
					raise tofpet2b.ConfigurationErrorBadRead(portID, slaveID, chipID, initialGlobalAsicConfig[(portID, slaveID, chipID)], readback)
			
		# Enable ASIC receiver logic for all unmasked ASICs
		for portID, slaveID in self.getActiveFEBDs(): self.write_config_register(portID, slaveID, 64, 0x0318, asic_enable_mask)

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
				ccfg = initialAsicChannelConfig[(portID, slaveID, chipID)]
				# .. but with the TX logic to normal
				gcfg.setValue("tx_mode", 0b10)
				ccfg.setValue("trigger_mode_1", 0b00)
				self.__doAsicCommand(portID, slaveID, chipID, "wrGlobalCfg", value=gcfg)
				for n in range(64):
					self.__doAsicCommand(portID, slaveID, chipID, "wrChCfg", channel=n, value=ccfg)

		# FEB\D-8K -> Ramp up VDD1 rail again after ASIC configuration
		for portID, slaveID in self.getActiveFEBDs():
			if self.__activeUnits[(portID, slaveID)][0] in [ 0x0005 ]:
				print(f'INFO: Found FEB\D 8K @ ({portID},{slaveID}). Ramping up VDD1 rail after ASIC configuration.')
				busID_lst = fe_power_8k.detect_active_bus(self, portID, slaveID)
				for busID, moduleVersion in busID_lst:
					current_dac_setting = fe_power_8k.read_dac(self, portID, slaveID, busID, moduleVersion, 'vdd1')
					vdd1_iterable = range(current_dac_setting, fe_power_8k.VDD1_PRESET[moduleVersion]["max"] + 1, 2)
					new_dac_setting = fe_power_8k.ramp_up_rail(self, portID, slaveID, busID, moduleVersion, "vdd1", vdd1_iterable, fe_power_8k.VDD1_PRESET[moduleVersion]["max"], fe_power_8k.VDD1_TARGET)
					#print(portID, slaveID, busID, moduleVersion, current_dac_setting, new_dac_setting)
		
		# Allow some ms for the deserializer to lock to the 8B/10B pattern
		sleep(0.010)

		# Check which ASICs are receiving properly data words
		deserializerStatus = [ False for x in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS) ]
		decoderStatus = [ False for x in range(MAX_PORTS * MAX_SLAVES * MAX_CHIPS) ]

		for portID, slaveID in self.getActiveFEBDs():
			lDeserializerStatus = self.read_config_register(portID, slaveID, 64, 0x0302)
			lDecoderStatus = self.read_config_register(portID, slaveID, 64, 0x0310)

			lDeserializerStatus &= asic_enable_mask
			lDecoderStatus &= asic_enable_mask

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
			slaveID = (gID // MAX_CHIPS) % MAX_SLAVES
			portID = gID // (MAX_CHIPS * MAX_SLAVES)

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
			print("WARNING: ASICs with inconsistent presence detection results")
			for portID, slaveID in self.getActiveFEBDs():
				lst = []
				for (lPortID, lSlaveID, lChipID), statusTripplet in inconsistentStateAsics:
					if lPortID != portID or lSlaveID != slaveID: continue
					lst.append((lChipID, statusTripplet))
				
				if lst != []:
					print(" FEB/D (%2d, %2d)" % (portID, slaveID))
					for chipID, statusTripplet in lst:
						a, b, c = statusTripplet
						a = a and "OK" or "FAIL"
						b = b and "OK" or "FAIL"
						c = c and "OK" or "FAIL"						
						print("  ASIC %2d: config link: %4s data link pattern: %4s data word: %4s" % (chipID, a, b, c))
			
			if maxTries > 1: 
				print("Retrying...")
				return self.initializeSystem(maxTries - 1, power_lst = power_lst)
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
				print("INFO: FEB/D (%2d, %2d) has %2d active ASICs: %s" % (portID, slaveID, n, lst))
			else:
				print("INFO: FEB/D (%2d, %2d) has 0 active ASICs." % (portID, slaveID))

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

	def stopAcquisition(self):
		self.__setAcquisitionMode(0)

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
		
		command = bytes([ 0x00 , (n_words - 1) & 0xFF, ((n_words - 1) >> 8) & 0xFF, base_address & 0xFF, (base_address >> 8) & 0xFF ])
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
		
		command = bytes([ 0x01 , (n_words - 1) & 0xFF, ((n_words - 1) >> 8) & 0xFF, base_address & 0xFF, (base_address >> 8) & 0xFF ] + data_bytes)
		reply = self.sendCommand(portID, slaveID, ctrl_id, command)
		
		assert len(reply) == 1
		assert reply[0] == 0x00
		
		return None


	def write_mem_ctrl2(self, portID, slaveID, ctrl_id, word_width, base_address, data):
		n_bytes_per_word = int(math.ceil(word_width / 8.0))
		n_words = int(math.ceil(len(data)/n_bytes_per_word))
		data_bytes = [ (data[i] >> (8*j)) & 0xFF for j in range(n_words)  for i in range(n_bytes_per_word)]

		command = bytes([ 0x01 , (n_words - 1) & 0xFF, ((n_words - 1) >> 8) & 0xFF, base_address & 0xFF, (base_address >> 8) & 0xFF ] + data_bytes)
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
	
	def spi_master_execute(self, portID, slaveID, chipID, cycle_length, sclk_en_on, sclk_en_off, cs_on, cs_off, mosi_on, mosi_off, miso_on, miso_off, mosi_data, freq_sel=1, miso_edge="rising", mosi_edge="rising"):

		if miso_edge == "rising":
			miso_edge = 0
		elif miso_edge == "falling":
			miso_edge = 1

		if mosi_edge == "rising":
			mosi_edge = 0
		else:
			mosi_edge = 1


		if len(mosi_data) == 0:
			mosi_data = [ 0x00 ]
		
		command = [
			(miso_edge << 7) | (mosi_edge << 6) | freq_sel,
			(chipID  >> 8) & 0xFF, chipID & 0xFF,
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
		

		return self.sendCommand(portID, slaveID, 0x02, bytes(command))

	## Performs an I2C transtions
	# @param portID  DAQ port ID where the FEB/D is connected
	# @param slaveID Slave ID on the FEB/D chain
	# @param busID ID of I2C bus
	# @param s Byte sequence containing the operations
	def i2c_master(self, portID, slaveID, busID, s):
		# Contents of s
		# bit 0: State to set SCL (0 pull down, 1 high-Z)
		# bit 1: State to set SCA (0 pull down, 1 hight-Z)
		# bit 2: Check that SCL went to the desired state
		# bit 3: Check that SDA went to the desired state (should be 0 for read bits)

		# return a sequenceof bytes with same length of 0
		# bit 0: state of SCL
		# bit 1: state of SDA
		# Bits 7-4: 0x0 when OK, 0xE during a bus error

		word0 = (busID >> 8) & 0xFF
		word1 = (busID >> 0) & 0xFF

		r = self.sendCommand(portID, slaveID, 3, bytes([word0, word1] + s))

		status = r[0]
		error = (status & 0xE0) != 0

		return r
	

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
			if nTries > 5: print("Timeout sending command. Retry %d of %d" % (nTries, maxTries))

			sn = self.__lastSN
			self.__lastSN = (sn + 1) & 0x7FFF

			rawFrame = bytes([ portID & 0xFF, slaveID & 0xFF] + [ (sn >> (8*n)) & 0xFF for n in range(16) ] + [ cfgFunctionID]) + payload
			#rawFrame = str(rawFrame)

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
			reply = bytes(data)
			

		if reply == None:
			print(reply)
			raise CommandErrorTimeout(portID, slaveID)

		return reply	


	## Defines all possible commands structure that can be sent to the ASIC and calls for sendCommand to actually transmit the command
	# @param asicID Identification of the ASIC that will receive the command
	# @param command Command type to be sent. The list of possible keys for this parameter is hardcoded in this function
	# @param value The actual value to be transmitted to the ASIC if it applies to the command type   
	# @param channel If the command is destined to a specific channel, this parameter sets its ID. 	  
	def __doAsicCommand(self, portID, slaveID, chipID, command, value=None, channel=None):

		# Remap the chipID to the spiID
		#
		chip_per_module = info.asic_per_module(self.getUnitInfo(portID, slaveID))
		a = chipID // chip_per_module
		b = chipID % chip_per_module
		spi_id = a * 256 + b

		nTry = 0
		while True:
			try:
				return self.___doAsicCommand(portID, slaveID, chipID, spi_id, command, value=value, channel=channel)
			except tofpet2b.ConfigurationError as e:
				nTry = nTry + 1
				if nTry >= 5:
					raise e


	def ___doAsicCommand(self, portID, slaveID, chipID, spi_id, command, value=None, channel=None):
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
		byteX = paddedValue.tobytes()
		
		if isRead:
			bitsToRead = dataLength
		else:
			bitsToRead = 0

		nBitsToWrite= len(ccBits)
		cmd = bytes([ (spi_id >> 8) & 0xFF, spi_id & 0xFF, nBitsToWrite, bitsToRead]) + byteX

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
			expectedBytes = math.ceil(dataLength//8)
			if len(reply) < (1+expectedBytes): 
				print(len(reply), (1+expectedBytes))
				raise tofpet2b.ConfigurationErrorBadReply(2+expectedBytes, len(reply))
			reply = reply[1:]
			data = bitarray()
			data.frombytes(reply)
			value = data[0:dataLength]
			return (status, value)
		else:
			# Check what we wrote
			readCommand = 'rd' + command[2:]
			readStatus, readValue = self.___doAsicCommand(portID, slaveID, chipID, spi_id, readCommand, channel=channel)
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
	

	def __write_hv_channel(self, portID, slaveID, slotID, channelID, value, forceAccess=False):
		if not forceAccess:
			try:
				lastValue = self.__hvdac_config_cache[(portID, slaveID, slotID, channelID)]
				if value == lastValue:
					return 0
			except KeyError:
				pass

		r = bias.set_channel(self, portID, slaveID, slotID, channelID, value)
		self.__hvdac_config_cache[(portID, slaveID, slotID, channelID)] = value
		return r


	## Returns the last value written in the bias voltage channel registers as a dictionary of
	## - key: a tupple of (portID, slaveID, channelID)
	## - value: an integer
	## WARNING: As the hardware does not support readback, this always returns the software cache
	def get_hvdac_config(self):
		if self.__hvdac_config_cache == {}:
			for portID, slaveID, slotID, channelID in self.getActiveBiasChannels(): 
				self.__hvdac_config_cache[(portID, slaveID, slotID, channelID)] = 0
		return deepcopy(self.__hvdac_config_cache)

	## Sets the bias voltage channels
	# @param is a dictionary, as returned by get_hvdac_config
	# @param forceAccess Ignores the software cache and forces hardware access.
	def set_hvdac_config(self, config, forceAccess=False):
		active_bias_channels = self.getActiveBiasChannels()

		# Get max HV value. Implemented for 32P-AG7200.
		max_value = {}
		for portID, slaveID, slotID, channelID in active_bias_channels: 
			value = config[(portID, slaveID, slotID, channelID)]
			max_value[slotID] = max(max_value[slotID], value) if slotID in max_value else value

		# Set BIAS-32P-AG7200 DCDC output
		BIAS_32P_DAC_ONEVOLT = int(2**16/60.01)
		vdc_delta = 2.0 # Set op-amp rails 2V above max HV output
		for portID, slaveID, slotID in self.getActiveBiasSlots(): 
			if self.__activeBiasSlots[(portID, slaveID, slotID)] == "BIAS_32P_AG":
				vdc_dcdc = (max_value[slotID]/BIAS_32P_DAC_ONEVOLT) + vdc_delta
				for dacID in range(2):
					bias.set_ag7200_dcdc(self, portID, slaveID, slotID, dacID, vdc_dcdc)

		# Set channels
		for portID, slaveID, slotID, channelID in active_bias_channels: 
			value = config[(portID, slaveID, slotID, channelID)]
			self.__write_hv_channel(portID, slaveID, slotID, channelID, value, forceAccess=forceAccess)
		
		# Cache max values after applying
		self.__hvdac_max_values = max_value.copy()


	def waitOnNamedPipe(self, fn):
		if not stat.S_ISFIFO(os.stat(fn).st_mode):
			raise Exception("'%s' is not a FIFO" % fn)
		print("INFO: Waiting for a byte to be written to '%s' to start acquiring" % fn)
		print("INFO: If using the GUI, press Start")
		f = open(fn, 'r')
		d = f.read(1)
		f.close()
		return None

	def openRawAcquisition(self, fileNamePrefix, calMode = False):
		return self.__openRawAcquisition(fileNamePrefix, calMode, None, None, None)
		
	def openRawAcquisitionWithMonitor(self, fileNamePrefix, monitor_config, monitor_toc, monitor_exec=os.path.join(os.path.dirname(__file__), '..', 'online_monitor')):
		return self.__openRawAcquisition(fileNamePrefix, False, monitor_config, monitor_toc, monitor_exec=monitor_exec)
		
	def __openRawAcquisition(self, fileNamePrefix, calMode, monitor_config, monitor_toc, monitor_exec):
		
		asicsConfig = self.getAsicsConfig()
		if fileNamePrefix != "/dev/null":
			modeFile = open(fileNamePrefix + ".modf", "w")
		else:
			modeFile = open("/dev/null", "w")

		modeFile.write("#portID\tslaveID\tchipID\tchannelID\tmode\n")
		modeList = [] 
		for portID, slaveID, chipID in list(asicsConfig.keys()):
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
  		

		# Determine current time and and estimate acquisition wallclock start time
		fileCreationDAQTime = self.getCurrentTimeTag()
		currentTime = time()
		daqSynchronizationEpoch = currentTime - fileCreationDAQTime / self.__systemFrequency
		
		cmd = [ os.path.join(os.path.dirname(__file__), '..', "write_raw"), \
			self.__shmName, \
			fileNamePrefix, \
			str(int(self.__systemFrequency)), \
			str(qdcMode), "%1.12f" % daqSynchronizationEpoch,
			str(fileCreationDAQTime),
			calMode and 'T' or 'N', 
			str(triggerID) ]
		self.__writerPipe = subprocess.Popen(cmd, bufsize=1, stdin=subprocess.PIPE, stdout=subprocess.PIPE, close_fds=True)
		
		if monitor_exec is not None:
			cmd = [
				monitor_exec,
				str(int(self.__systemFrequency)),
				(qdcMode == "tot") and "tot" or "qdc",
				monitor_config,
				self.__shmName,
				monitor_toc,
				str(triggerID), 
				"%1.12f" % daqSynchronizationEpoch
				]
			self.__monitorPipe = subprocess.Popen(cmd, bufsize=1, stdin=subprocess.PIPE, stdout=subprocess.PIPE, close_fds=True)


	## Closes the current acquisition file
	def closeAcquisition(self):
		workers = [self.__writerPipe ]
		if self.__monitorPipe is not None:
			workers += [ self.__monitorPipe ]

		for worker in workers:
			worker.stdin.close()

		sleep(0.5)

		for worker in workers:
			worker.kill()
			
		self.__writerPipe = None
		self.__monitorPipe = None


	## Acquires data and decodes it, while writting through the acquisition pipeline 
	# @param step1 Tag to a given variable specific to this acquisition 
	# @param step2 Tag to a given variable specific to this acquisition
	# @param acquisitionTime Acquisition time in seconds 
	def acquire(self, acquisitionTime, step1, step2):
		# WARNING Only the writerPipe returns valid frame/event counters
		# So it sould always be the last one to be read
		workers = []
		if self.__monitorPipe is not None:
			workers += [(self.__monitorPipe.stdin, self.__monitorPipe.stdout) ]
		workers += [(self.__writerPipe.stdin, self.__writerPipe.stdout) ]
			
		frameLength = 1024.0 / self.__systemFrequency
		nRequiredFrames = int(acquisitionTime / frameLength)
		nRequiredFrames = max(nRequiredFrames, 1) # Attempt to acquire at least 1 frame

		template1 = "@ffIIi"
		template2 = "@I"
		template3 = "@qqq"
		n1 = struct.calcsize(template1)
		n2 = struct.calcsize(template2)
		n3 = struct.calcsize(template3)

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
			try:
				wrPointer, rdPointer = self.__getDataFrameWriteReadPointer()
				while wrPointer == rdPointer:
					wrPointer, rdPointer = self.__getDataFrameWriteReadPointer()
			except ErrorAcquisitionStopped as e:
				# Acquisition must have been stopped by a different program
				break

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
			if nFramesInBlock > bs//2:
				nFramesInBlock = bs//2

			wrPointer = (rdPointer + nFramesInBlock) % (2*bs)

			if nBlocks == 0:
				# First block in step
				data = struct.pack(template1, step1, step2, wrPointer, rdPointer, 0)
			else:
				# Other blocks in step
				data = struct.pack(template1, step1, step2, wrPointer, rdPointer, 1)
			for pin, pout in workers:
				pin.write(data); pin.flush()
			
			for pin, pout in workers:
				data = pout.read(n2)
			rdPointer,  = struct.unpack(template2, data)

			for pin, pout in workers:
				data = pout.read(n3)

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
		print("Python:: Acquired %d frames in %4.1f seconds, corresponding to %4.1f seconds of data (delay = %4.1f)" % (nFrames, time()-t0, nFrames * frameLength, (t1-t0) - nFrames * frameLength))

		# Send end of step block (with wrPointer = rdPointer)
		data = struct.pack(template1, step1, step2, rdPointer, rdPointer, 2)
		for pin, pout in workers:
			pin.write(data); pin.flush()

		for pin, pout in workers:
			data = pout.read(n2)
		rdPointer,  = struct.unpack(template2, data)
		self.__setDataFrameReadPointer(rdPointer)
		
		for pin, pout in workers:
			data = pout.read(n3)
		stepFrames, stepFramesLost, stepEvents = struct.unpack(template3, data)

		# Check ASIC link status at end of acquisition
		self.checkAsicRx()

		return stepFrames, stepFramesLost, stepEvents

        ## Acquires data and decodes it into a bytes buffer
        # @param acquisitionTime Acquisition time in seconds
	# @return A bytes buffer containing events as per shw_raw_py.cpp/unpacked_event_t
	def acquireAsBytes(self, acquisitionTime):
		frameLength = 1024.0 / self.__systemFrequency
		nRequiredFrames = int(acquisitionTime / frameLength)

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
		data = bytes()

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

			#data = struct.pack(template1, step1, step2, wrPointer, rdPointer, 0)
			#pin.write(data); pin.flush()

			#data = pout.read(n2)
			#rdPointer,  = struct.unpack(template2, data)

			data += self.__shm.events_as_bytes(rdPointer, wrPointer)
			rdPointer = wrPointer


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
		print("Python:: Acquired %d frames in %4.1f seconds, corresponding to %4.1f seconds of data (delay = %4.1f)" % (nFrames, time()-t0, nFrames * frameLength, (t1-t0) - nFrames * frameLength))

		# Check ASIC link status at end of acquisition
		self.checkAsicRx()

		return data
	
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
					print("ASIC (%2d, %2d, %2d) RX links are down (0b%d%d)" % (portID, slaveID, n, b, a))
					bad_rx_found = True
					
		if bad_rx_found:
			raise ErrorAsicLinkDown()

	## Gets the current write and read pointer
	def __getDataFrameWriteReadPointer(self):
		template = "@HH"
		n = struct.calcsize(template)
		data = struct.pack(template, 0x03, n);
		self.__socket.send(data)

		template = "@HIII"
		n = struct.calcsize(template)
		data = self.__socket.recv(n);
		n, wrPointer, rdPointer, amAcquiring = struct.unpack(template, data)

		if amAcquiring == 0:
			raise ErrorAcquisitionStopped()

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

		targetFrameID = self.getCurrentTimeTag() // 1024

		template1 = "@HH"
		template2 = "@Q"
		n = struct.calcsize(template1) + struct.calcsize(template2)
		data = struct.pack(template1, 0x13, n) + struct.pack(template2, targetFrameID)
		self.__socket.send(data)
		data = self.__socket.recv(4)
		assert len(data) == 4

		# Set the read pointer to write pointer, in order to consume all available frames in buffer
		wrPointer, rdPointer = self.__getDataFrameWriteReadPointer()
		self.__setDataFrameReadPointer(wrPointer)
		return

		# WARNING
		#: Don't actually remove code below until the new synchronization scheme has been better tested

		frameLength = 1024 / self.__systemFrequency

		# Check ASIC link status at start of acquisition
		# but wait for  firmware has finshed sync'ing after config and locking
		# - 8 frames for resync
		# - 4 frames for lock
		sleep(12 * frameLength)
		self.checkAsicRx()

		while True:	
			targetFrameID = self.getCurrentTimeTag() // 1024
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
			currentFrameID = self.getCurrentTimeTag() // 1024
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

class ErrorUnitNotPresent(Exception):
	def __init__(self, portID, slaveID):
		self.__portID = portID
		self.__slaveID = slaveID
	def __str__(self):
		return "Unit (%2d, %2d) does not exist" % (self.__portID, self.__slaveID)

class ErrorFEBDNotPresent(Exception):
	def __init__(self, portID, slaveID):
		self.__portID = portID
		self.__slaveID = slaveID
	def __str__(self):
		return "Unit (%2d, %2d) does not exist or is not a FEB/D" % (self.__portID, self.__slaveID)


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
		return "ASIC at port %2d, slave %2d, asic %02d: unknown configuration after reset %s" % self.__data
	
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

class ErrorUnknownProtocol(Exception):
	def __init__(self, port, slave, protocol):
		self.__port = port
		self.__slave = slave
		self.__protocol = protocol

	def __str__(self):
		return "Unit (%d, %d) has protocol version 0x%04X but 0x%04X is required" % (self.__port, self.__slave, self.__protocol, PROTOCOL_VERSION)


class ErrorTooManyTriggerUnits(Exception):
	def __init__(self, trigger_list):
		self.__trigger_list = trigger_list

	def __str__(self):
		l = [ "(%02d, %02d)" % (p, s) for p, s in self.__trigger_list ]
		s = (", ").join(l)
		return "More than one trigger unit has been found: %s" % s

class ErrorAcquisitionStopped(Exception):
	pass
