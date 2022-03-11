from math import sqrt

def lmt87(v):
	return (13.582 - sqrt((-13.582)**2 + 4 * 0.00433 * (2230.8 - v)))/(2 * -0.00433) + 30

def lmt86(v):
	return 30-(10.888-sqrt(118.548544+0.01388*(1777.3-v)))/0.00694

def lmt85(v):
	return (8.194 - sqrt((-8.194)**2 + 4 * 0.00262 * (1324 - v)))/(2 * - 0.00262) + 30

def lmt70(v):
	return 205.5894-0.1814103*v-3.325395*10**-6*(v)**2-1.809628*10**-9*(v)**3

class UnknownTemperatureSensorType(Exception):
	pass

class UnknownModuleType(Exception):
	pass

class max111xx_sensor:
	def __init__(self, connection, portID, slaveID, spi_id, channel_id, location, chip_type):
		self.__conn = connection
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
		u = self.__conn.fe_temp_read_max111xx(self.__portID, self.__slaveID, self.__spi_id, self.__channel_id)
		return self.__function(u)

def list_fem128(connection, portID, slaveID, n):
	result = []
	
	# Eventually this should be made dependent on the number of ports in the FEB/D
	for module_id in range(8):
		spi_id = module_id * n
		if not connection.fe_temp_check_max1111xx(portID, slaveID, spi_id):
			continue
		
		result.append(max111xx_sensor(connection, portID, slaveID, spi_id, 3, (portID, slaveID, module_id, 0, "asic"), "LMT86"))
		result.append(max111xx_sensor(connection, portID, slaveID, spi_id, 2, (portID, slaveID, module_id, 0, "sipm"), "LMT70"))

		result.append(max111xx_sensor(connection, portID, slaveID, spi_id, 0, (portID, slaveID, module_id, 1, "asic"), "LMT86"))
		result.append(max111xx_sensor(connection, portID, slaveID, spi_id, 1, (portID, slaveID, module_id, 1, "sipm"), "LMT70"))
		
		
	return result
		
def list_fem256(connection, portID, slaveID):
	result = []
	
	# Eventually this should be made dependent on the number of ports in the FEB/D
	for module_id in range(8):
		spi_id = module_id * 4
		if not connection.fe_temp_check_max1111xx(portID, slaveID, spi_id):
			continue
		
		for i in range(4):		
			result.append(max111xx_sensor(connection, portID, slaveID, spi_id, i+4, (portID, slaveID, module_id, i, "asic"), "LMT86"))
			result.append(max111xx_sensor(connection, portID, slaveID, spi_id, i+0, (portID, slaveID, module_id, i, "sipm"), "LMT70"))
		
	return result
	



def get_sensor_list(connection):
	result = []
	for portID, slaveID in connection.getActiveFEBDs():
		fem_type = connection.read_config_register(portID, slaveID, 16, 0x0002)
		
		if fem_type == 0x0001:
			result += list_fem128(connection, portID, slaveID, 1)
		
		elif fem_type == 0x0011:
			result += list_fem128(connection, portID, slaveID, 4)
		
		elif fem_type == 0x0111:
			result += list_fem256(connection, portID, slaveID)
		else:
			raise UnknownModuleType()
			
			
	return result
			
			
		
	
