import time

class BusError(Exception):
	def __init__(self, portID, slaveID, busID):
		self.__portID = portID
		self.__slaveID = slaveID
		self.__busID = busID

	def __str__(self):
		return "I2C bus (%2d %2d %2d) error" % (
			self.__portID,
			self.__slaveID,
			self.__busID
			)

class NoAck(Exception):
	def __init__(self, portID, slaveID, busID):
		self.__portID = portID
		self.__slaveID = slaveID
		self.__busID = busID

	def __str__(self):
		return "I2C bus (%2d %2d %2d) ACK failed" % (
			self.__portID,
			self.__slaveID,
			self.__busID
			)


def ds44xx_set_register(conn, portID, slaveID, busID, chipID, regID, value, debug_error=False):


	sequence = []
	ack_position = []

	sequence += [ 0b1111, 0b1101, 0b1100 ] # Start condition

	# Write chip address byte
	for n in range(7, -1, -1):
		sda = (chipID >> n) & 0x1
		sda = sda << 1
		sequence += [ 0b1100 | sda, 0b1101 | sda, 0b1100 ]

	sequence += [ 0b0110, 0b0111, 0b0110 ]
	ack_position += [ len(sequence) - 2 ]

	## Write register address
	for n in range(7, -1, -1):
		sda = (regID >> n) & 0x1
		sda = sda << 1
		sequence += [ 0b1100 | sda, 0b1101 | sda, 0b1100 ]

	sequence += [ 0b0110, 0b0111, 0b0110 ]
	ack_position += [ len(sequence) - 2 ]

	# Write register value
	for n in range(7, -1, -1):
		sda = (value >> n) & 0x1
		sda = sda << 1
		sequence += [ 0b1100 | sda, 0b1101 | sda, 0b1100 ]

	sequence += [ 0b0110, 0b0111, 0b0110 ]
	ack_position += [ len(sequence) - 2 ]

	sequence+= [ 0b0100, 0b0001, 0b0011 ] # Stop condition


	t0 = time.time()
	reply = conn.i2c_master(portID, slaveID, busID, sequence)
	dt = time.time() - t0

	error =  [ (x & 0xE0) != 0 for x in reply ]
	ack = [ x & 0b10 == 0 for i,x in enumerate(reply) if i in ack_position ]

	## This code is useful for debugging the bus
	if debug_error and ((True in error) or (False in ack)):
		print("SCL OUT", ("").join([ "‾" if (x & 0b01) != 0 else "_" for x in sequence ]) )
		print("\nSDA OUT", ("").join([ "‾" if (x & 0b10) != 0 else "_" for x in sequence ]) )

		print("\nSCL IN ", ("").join([ "‾" if (x & 0b01) != 0 else "_" for x in reply ]) )
		print("\nSDA IN ", ("").join([ "‾" if (x & 0b10) != 0 else "_" for x in reply ]) )
		print("\nACK    ", ("").join([ "|" if k in ack_position else " " for k in range(len(reply)) ]) )
		print("ERROR  ", ("").join([ "E" if (x & 0xE0) != 0 else " " for x in reply ]) )
		print([ "%02X" % x for x in reply ])
		print("%4.0f us" % (1e6 * len(sequence) * 100e-9 * 2**5))
		print("%4.0f us" % (1e6 * 3*9 * 10e-6))
		print("%4.0f us" % (1e6 * dt))

	if True in error:
		# Generate a no-check stop condition to relase the bus
		conn.i2c_master(portID, slaveID, busID, [ 0b0000, 0b0001, 0b0011 ])
		raise BusError(portID, slaveID, busID)

	if False in ack:
		raise NoAck(portID, slaveID, busID)
