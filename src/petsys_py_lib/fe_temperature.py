from math import sqrt
from . import spi

def lmt86(v):
	return 30-(10.888-sqrt(118.548544+0.01388*(1777.3-v)))/0.00694

def lmt70(v):
	return 205.5894-0.1814103*v-3.325395*10**-6*(v)**2-1.809628*10**-9*(v)**3

class UnknownTemperatureSensorType(Exception):
	pass

class UnknownModuleType(Exception):
	pass

class max111xx_sensor:
	def __init__(self, conn, portID, slaveID, spi_id, channel_id, location, chip_type):
		self.__conn = conn
		self.__portID = portID
		self.__slaveID = slaveID
		self.__spi_id = spi_id
		self.__channel_id = channel_id
		
		self.__location = location
		if chip_type == "LMT86":
			self.__function = lambda u: lmt86(u*2.5/4.096)
		elif chip_type == "LMT70":
			self.__function = lambda u: lmt70(u*2.5/4.096)
		else:
			raise UnknownTemperatureSensor()
		
		
	def get_location(self):
		return self.__location
	
	def get_temperature(self):
		u = spi.max111xx_read(self.__conn, self.__portID, self.__slaveID, self.__spi_id, self.__channel_id)
		return self.__function(u)

## Initializes the temperature sensors in the FEB/As
# Return the number of active sensors found in FEB/As
def fe_temp_enumerate_tmp104(self, portID, slaveID):
	din = [ 3, 0x55, 0b10001100, 0b10010000 ]
	din = bytes(din)
	dout = self.sendCommand(portID, slaveID, 0x04, din)

	if len(dout) < 4:
		# Reply is too short, chain is probably open
		raise TMP104CommunicationError(portID, slaveID, din, dout)

	if (dout[1:2] != din[1:2]) or ((dout[3] & 0xF0) != din[3]):
		# Reply does not match what is expected; a sensor is probably broken
		raise TMP104CommunicationError(portID, slaveID, din, dout)

	nSensors = dout[3] & 0x0F

	din = [ 3, 0x55, 0b11110010, 0b01100011]
	din = bytes(din)
	dout = self.sendCommand(portID, slaveID, 0x04, din)
	if len(dout) < 4:
		raise TMP104CommunicationError(portID, slaveID, din, dout)

	din = [ 2 + nSensors, 0x55, 0b11110011 ]
	din = bytes(din)
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
		din = bytes(din)
		dout = self.sendCommand(portID, slaveID, 0x04, din)
		if len(dout) < (3 + nSensors):
			raise TMP104CommunicationError(portID, slaveID, din, dout)

		temperatures = dout[3:]
		for i, t in enumerate(temperatures):
			if t > 127: t = t - 256
			temperatures[i] = t
		return temperatures


def list_fem128(conn, portID, slaveID, n):
	result = []
	
	n_fems = conn.getUnitInfo(portID, slaveID)["c_n_fems"]
	for module_id in range(n_fems):
		spi_id = module_id * 256 + 4

		if not spi.max111xx_check(conn, portID, slaveID, spi_id):
			continue
		
		result.append(max111xx_sensor(conn, portID, slaveID, spi_id, 3, (portID, slaveID, module_id, 0, "asic"), "LMT86"))
		result.append(max111xx_sensor(conn, portID, slaveID, spi_id, 2, (portID, slaveID, module_id, 0, "sipm"), "LMT70"))

		result.append(max111xx_sensor(conn, portID, slaveID, spi_id, 0, (portID, slaveID, module_id, 1, "asic"), "LMT86"))
		result.append(max111xx_sensor(conn, portID, slaveID, spi_id, 1, (portID, slaveID, module_id, 1, "sipm"), "LMT70"))
		
		
	return result
		
def list_fem256(conn, portID, slaveID):
	result = []
	
	n_fems = conn.getUnitInfo(portID, slaveID)["c_n_fems"]
	for module_id in range(n_fems):
		spi_id = module_id * 256 + 4

		if not spi.max111xx_check(conn, portID, slaveID, spi_id):
			continue
		
		for i in range(4):		
			result.append(max111xx_sensor(conn, portID, slaveID, spi_id, i+4, (portID, slaveID, module_id, i, "asic"), "LMT86"))
			result.append(max111xx_sensor(conn, portID, slaveID, spi_id, i+0, (portID, slaveID, module_id, i, "sipm"), "LMT70"))
		
	return result
	



def get_sensor_list(conn):
	result = []
	for portID, slaveID in conn.getActiveFEBDs():
		fem_type = conn.read_config_register(portID, slaveID, 16, 0x0002)
		
		if fem_type == 0x0001:
			result += list_fem128(conn, portID, slaveID, 1)
		
		elif fem_type == 0x0011:
			result += list_fem128(conn, portID, slaveID, 4)
		
		elif fem_type == 0x0111:
			result += list_fem256(conn, portID, slaveID)
		else:
			raise UnknownModuleType()
			
			
	return result
			
			
		
	
