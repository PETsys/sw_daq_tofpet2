def start(conn, portID, slaveID, busID):
	conn.i2c_master(portID, slaveID, True, busID, 1, 1)
	conn.i2c_master(portID, slaveID, True, busID, 1, 0)
	conn.i2c_master(portID, slaveID, True, busID, 0, 0)

def stop(conn, portID, slaveID, busID):
	conn.i2c_master(portID, slaveID, True, busID, 1, 0)
	conn.i2c_master(portID, slaveID, True, busID, 1, 1)

def repeat_start(conn, portID, slaveID, busID):
	conn.i2c_master(portID, slaveID, True, busID, 0, 0)
	conn.i2c_master(portID, slaveID, True, busID, 0, 1)
	conn.i2c_master(portID, slaveID, True, busID, 1, 1)
	conn.i2c_master(portID, slaveID, True, busID, 1, 0)
	conn.i2c_master(portID, slaveID, True, busID, 0, 0)
	

def write_bit(conn, portID, slaveID, busID, sda):
	conn.i2c_master(portID, slaveID, True, busID, 0, sda)
	conn.i2c_master(portID, slaveID, True, busID, 1, sda)
	conn.i2c_master(portID, slaveID, True, busID, 1, sda)
	conn.i2c_master(portID, slaveID, True, busID, 0, sda)

def read_bit(conn, portID, slaveID, busID):
	
	conn.i2c_master(portID, slaveID, True, busID, 0, 1)
	conn.i2c_master(portID, slaveID, True, busID, 1, 1)
	scl, sda = conn.i2c_master(portID, slaveID, True, busID, 1, 1)
	conn.i2c_master(portID, slaveID, True, busID, 0, 1)
	return sda

def write_byte(conn, portID, slaveID, busID, value):

	for n in range(8):
		sda = (value & 0x80) != 0
		write_bit(conn, portID, slaveID, busID, sda)
		value = value << 1

	ack = read_bit(conn, portID, slaveID, busID)
	return not ack
	

def ds44xx_set_register(conn, portID, slaveID, busID, chipID, regID, value):
	start(conn, portID, slaveID, busID)
	write_byte(conn, portID, slaveID, busID, chipID)
	write_byte(conn, portID, slaveID, busID, regID)
	write_byte(conn, portID, slaveID, busID, value)
	stop(conn, portID, slaveID, busID)
	
	
