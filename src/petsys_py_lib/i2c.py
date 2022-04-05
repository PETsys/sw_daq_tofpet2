import time

class UnexpectedState(Exception):
	def __init__(self, portID, slaveID, busID, signal, expected_state, read_state):
		self.__portID = portID
		self.__slaveID = slaveID
		self.__busID = busID
		self.__signal = signal
		self.__expected_state = expected_state
		self.__read_state = read_state

	def __str__(self):
		return "I2C bus (%2d %2d %2d) %s should be %d but is %d" % (
			self.__portID,
			self.__slaveID,
			self.__busID,
			self.__signal,
			self.__expected_state,
			self.__read_state
			)

class NoAck(Exception):
	def __init__(self, portID, slaveID, busID):
		self.__portID = portID
		self.__slaveID = slaveID
		self.__busID = busID

	def __str__(self):
		return "I2C bus (%2d %2d %2d) %s ACK failed" % (
			self.__portID,
			self.__slaveID,
			self.__busID
			)


def set_i2c_bus(conn, portID, slaveID, enable, busID, scl, sda, check_sda=True):
	scl_o, sda_o = conn.i2c_master(portID, slaveID, enable, busID, scl, sda)
	# We need to write twice because it seems i2c_master is reading too fast
	scl_o, sda_o = conn.i2c_master(portID, slaveID, enable, busID, scl, sda)

	#if scl_o != scl:
		#raise UnexpectedState(portID, slaveID, busID, "scl", scl, scl_o)

	#if check_sda and (sda_o != sda):
		#raise UnexpectedState(portID, slaveID, busID, "sda", sda, sda_o)

	return scl_o, sda_o


def start(conn, portID, slaveID, busID):
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, 1)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, 0)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 0, 0)

def stop(conn, portID, slaveID, busID):
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, 0)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, 1)


def repeat_start(conn, portID, slaveID, busID):
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 0, 0)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 0, 1)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, 1)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, 0)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 0, 0)
	

def write_bit(conn, portID, slaveID, busID, sda):
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 0, sda)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, sda)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, sda)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 0, sda)

def read_bit(conn, portID, slaveID, busID):
	
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 0, 1, check_sda=False)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, 1, check_sda=False)
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 1, 1, check_sda=False)
	r = sda
	scl, sda = set_i2c_bus(conn, portID, slaveID, True, busID, 0, 1, check_sda=False)
	return r

def write_byte(conn, portID, slaveID, busID, value):

	for n in range(7, -1, -1):
		sda = (value >> n) & 0x1
		write_bit(conn, portID, slaveID, busID, sda)

	sda = read_bit(conn, portID, slaveID, busID)
	if sda != 0:
		raise NoAck(portID, slaveID, busID)

	return None
	

def ds44xx_set_register(conn, portID, slaveID, busID, chipID, regID, value):
	start(conn, portID, slaveID, busID)
	write_byte(conn, portID, slaveID, busID, chipID)
	write_byte(conn, portID, slaveID, busID, regID)
	write_byte(conn, portID, slaveID, busID, value)
	stop(conn, portID, slaveID, busID)
	
	
