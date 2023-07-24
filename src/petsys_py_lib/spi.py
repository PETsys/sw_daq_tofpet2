# kate: indent-mode: python; indent-pasted-text false; indent-width 8; replace-tabs: off;
# vim: tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab

#
# A library of functions to control various SPI devices based on the system SPI master
#

import time

def spi_reg_ll(conn, portID, slaveID, chipID, data_out):
	p = 2
	padding = [ 0xFF for n in range(p) ]
	p = 8 * p
	w = len(data_out) * 8

	# Pad the cycle with zeros
	return conn.spi_master_execute(portID, slaveID, chipID,
		p+w+p,          # cycle
		p,p+w,          # sclk en
		p-1,p+w+1,      # cs
		0, p+w+p,       # mosi
		p,p+w,
		padding + data_out + padding, miso_edge = "falling")

def spi_reg(conn, portID, slaveID, chipID, l, data_out):
	data_out = [ (data_out >> k) & 0xFF for k in range(0, l, 8) ]
	data_out = data_out[::-1]
	

	r = spi_reg_ll(conn, portID, slaveID, chipID, data_out)
	
	
	r = r[1:-1]
	#print(r)
	r = [ v << (l - 8 - 8*k) for k, v in enumerate(r) ]
	r = sum(r)
	
	return r

class ADCException(Exception): pass
class DACException(Exception): pass

def ad5535_ll(conn, portID, slaveID, chipID, data):
	"""! AD5535 DAC SPI low level coding

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param data Data to be transmitted over the SPI bus.

	@return Data received from the SPI bus returned by spi_master_execute()
	"""

	# SPI master needs data in byte sizes
	# with SPI first bit being most significant bit of first byte
	data = data << 5
	command = [ (data >> 16) & 0xFF, (data >> 8) & 0xFF, (data >> 0) & 0xFF ]

	w = 19
	padding = [0x00 for n in range(2) ]
	p = 8 * len(padding)

	# Pad the cycle with zeros
	return conn.spi_master_execute(portID, slaveID, chipID,
		p+w+p, 		# cycle
		p,p+w,	# sclk en
		p-1,p,		# cs
		0, p+w+p, 	# mosi
		p,p+w, 		# miso
		padding + command + padding,
		freq_sel = 0)

def ad5535_set_channel(conn, portID, slaveID, chipID, channelID, value):
	"""! Set AD5535 channel

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param channelID DAC channel number to be set
	@param value DAC value to be set

	@return Data received from the SPI bus returned by spi_master_execute()
	"""

#	chipID = channelID // 32
#	channelID = channelID % 32
#	#chipID = 1 - whichDAC # Wrong decoding in ad5535.vhd

	channelID &= 0b11111
	value &= 0b11111111111111
	command = channelID << 14 | value
	return ad5535_ll(conn, portID, slaveID, chipID, command)


def ltc2668_ll(conn, portID, slaveID, chipID, command):
	"""! LTC2668 DAC SPI low level coding

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param data Data to be transmitted over the SPI bus.

	@return Data received from the SPI bus returned by spi_master_execute()
	"""
	w = 8 * len(command)
	padding = [0x00 for n in range(2) ]
	p = 8 * len(padding)

	# Pad the cycle with zeros
	return conn.spi_master_execute(portID, slaveID, chipID,
		p+w+p, 		# cycle
		p,p+w, 		# sclk en
		p-1,p+w+1,	# cs
		0, p+w+p, 	# mosi
		p,p+w, 		# miso
		padding + command + padding,
		freq_sel = 1,	# WORKAROUND: 10 MHz operation unreliable on FEB/D 8K revC
		miso_edge = "falling")


def ltc2668_set_channel(conn, portID, slaveID, chipID, channelID, value):
	"""! Set AD5535 channel

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param channelID DAC channel number to be set
	@param value DAC value to be set

	@return Data received from the SPI bus returned by spi_master_execute()
	"""
	
	# Do multiple attempts at setting the DAC if needed
	for attempt in range(2):
		if ltc2668_set_channel_(conn, portID, slaveID, chipID, channelID, value):
			return True
	raise DACException("LTC2668 set failed")
	
def ltc2668_set_channel_(conn, portID, slaveID, chipID, channelID, value):	
	command = [ 0b00110000 + channelID, (value >> 8) & 0xFF , value & 0xFF ]
	ltc2668_ll(conn, portID, slaveID, chipID, command)

	# Read the result of the previous command to check it was sent properly
	command2 = [ 0b11110000 + channelID, (value >> 8) & 0xFF , value & 0xFF ]
	r = ltc2668_ll(conn, portID, slaveID, chipID, command2)
	r = [ r[2], r[3], r[1] ]
	
	return command == r


def ltc2418_ll(conn, portID, slaveID, chipID, command):
	"""! LTC2418 ADC SPI low level coding

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param data Data to be transmitted over the SPI bus.

	@return Data received from the SPI bus returned by spi_master_execute()
	"""
	w = 8 * len(command)
	padding = [0x00 for n in range(3) ]
	p = 8 * len(padding)

	# Pad the cycle with zeros
	return conn.spi_master_execute(portID, slaveID, chipID,
		w+p+1, 		# cycle
		0,w+p+1,    # sclk en
		0,w+p+1,	# cs
		0, w+p, 	# mosi
		0, w+p, 		# miso
		command + padding,
		freq_sel = 0,
		miso_edge = "falling",
		mosi_edge = "rising")


def ltc2418_read(conn, portID, slaveID, chipID, channel, MAX_TRIES = 10):
	"""! LTC2418 ADC SPI read channel

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param channel ADC channel to be read

	@return Data returned by ADC
	"""
	MAX_CONVERSION_TIME = 0.17 #Conversion should take at most 164ms
	
	# EN  = 1 to change channel
	# SGL = 1 for single-ended mode
	base_cmd = 0b10110000  # 1 0 EN SGL OS A2 A1 A0
	os       = 8 * (channel %2) # ODD/SIGN
	adr      = channel // 2     # A2 A1 A0
	command  = [base_cmd + os + adr]
	
	expected_readback = command[0] & 0x1F 

	#Change Channel
	r = ltc2418_ll(conn, portID, slaveID, chipID, command)

	#Get Reading for desired channel
	t0 = time.time()
	while True:
		r = ltc2418_ll(conn, portID, slaveID, chipID, command)
		
		read_bytes = int.from_bytes(r[1:-1], "big")
		signal     = (read_bytes >> 29) & 0x1       #1  bit
		value      = (read_bytes >>  6) & 0x7FFFFF  #23 bits
		readback   = (read_bytes >>  1) & 0x1F      #5  bits
		eoc        = (read_bytes >> 31) & 0x1


		if eoc == 0 and (readback == expected_readback):
			break
			
		if (time.time() - t0) > (MAX_TRIES*MAX_CONVERSION_TIME):
			raise ADCException("End Of Conversion failed.")
			
		time.sleep(0.2*MAX_CONVERSION_TIME)

	if signal != 1:
		value = -value
	return value

def ltc2439_ll(conn, portID, slaveID, chipID, command):
    """! LTC2439 ADC SPI low level coding

    @param conn daqd connection object
    @param portID FEB/D portID
    @param slaveID FEB/D slaveID
    @param chipID SPI slave number
    @param data Data to be transmitted over the SPI bus.

    @return Data received from the SPI bus returned by spi_master_execute()
    """
    w = 19
    command = command << 16
    
    command = [ (command >> 16) & 0xFF, (command >> 8) & 0xFF, command & 0xFF ]
    
    padding = [ ]
    p = 8 * len(padding)

    # Pad the cycle with zeros
    r =  conn.spi_master_execute(portID, slaveID, chipID,
        w+p+1, 		# cycle
        0,w+p,    # sclk en
        0,w+p+1,	# cs
        0, w+p, 	# mosi
        0, w+p, 		# miso
        command + padding,
        freq_sel = 3,
        miso_edge = "falling",
        mosi_edge = "rising")
    
    read_bytes = int.from_bytes(r[1:-1], "big")
    read_bytes = read_bytes >> 5
    return read_bytes


def ltc2439_read(conn, portID, slaveID, chipID, channel, MAX_TRIES = 10):
    """! LTC2439 ADC SPI read channel

    @param conn daqd connection object
    @param portID FEB/D portID
    @param slaveID FEB/D slaveID
    @param chipID SPI slave number
    @param channel ADC channel to be read

    @return Data returned by ADC
    """
    MAX_CONVERSION_TIME = 0.17 #Conversion should take at most 164ms
    
    # EN  = 1 to change channel
    # SGL = 1 for single-ended mode
    base_cmd = 0b10110000  # 1 0 EN SGL OS A2 A1 A0
    os       = 8 * (channel %2) # ODD/SIGN
    adr      = channel // 2     # A2 A1 A0
    command  =  base_cmd + os + adr 
    
    #Change Channel
    r = ltc2439_ll(conn, portID, slaveID, chipID, command)
    
    #Get Reading for desired channel
    t0 = time.time()
    while True:
        read_bytes = ltc2439_ll(conn, portID, slaveID, chipID, command)
        signal     = (read_bytes >> 16) & 0x1       #1  bit
        value      = (read_bytes >>  0) & 0xFFFF  #16 bits
        eoc        = (read_bytes >> 18) & 0x1


        if eoc == 0:
            break
            
        if (time.time() - t0) > (MAX_TRIES*MAX_CONVERSION_TIME):
            raise ADCException("End Of Conversion failed.")
            
        time.sleep(0.2*MAX_CONVERSION_TIME)

    if signal != 1:
        value = -value
    return value


def ad7194_ll(conn,  portID, slaveID, chipID, command, read_count):
	"""! AD7194 ADC SPI low level coding

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param data Data to be transmitted over the SPI bus.

	@return Data received from the SPI bus returned by spi_master_execute()
	"""
	command = [0x00] + command
	w = 8 * len(command)
	r = 8 * read_count
	p = 2
	w_padding = [ 0xFF for n in range(p) ]
	r_padding = [ 0xFF for n in range(p + read_count) ]
	p = 8 * p

	# Pad the cycle with zeros
	r = conn.spi_master_execute(portID, slaveID, chipID,
		p+w+r+p, 		# cycle
		p,p+w+r+1, 		# sclk en
		p-1,p+w+r+1,		# cs
		0, p+w+r+p, 		# mosi
		p+w,p+w+r, 		# miso
		w_padding + command + r_padding,
		freq_sel=1,
		miso_edge="falling")
	
	return r


def ad7194_get_channel(conn, portID, slaveID, chipID, channelID):
	"""! Read AD7194 channel

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param channelID ADC channel number to be read

	@return Value read fom ADC
	"""

	# Reset
	ad7194_ll(conn, portID, slaveID, chipID, [0xFF for n in range(8) ], 0)

	# Set mode register
	r = ad7194_ll(conn, portID, slaveID, chipID, [0b00001000, 0b00011011, 0b00100100, 0b01100000], 0)

	# Set configuration register
	r =  ad7194_ll(conn, portID, slaveID, chipID, [0b00010000, 0b00000100, 0b00000000 + (channelID << 4), 0b01011000], 0)

	# Wait for conversion to be ready
	while True:
		r = ad7194_ll(conn, portID, slaveID, chipID, [0b01000000], 1)
		if r[1] & 0x80 == 0x00: break
		time.sleep(0.1)

	r = ad7194_ll(conn, portID, slaveID, chipID, [0x58], 4)
	v = (r[1] << 16) + (r[2] << 8) + r[3]

	return v



def max111xx_ll(conn, portID, slaveID, chipID, command):
	w = 8 * len(command)
	padding = [0xFF for n in range(2) ]
	p = 8 * len(padding)

	# Pad the cycle with zeros
	return conn.spi_master_execute(portID, slaveID, chipID,
		p+w+p, 		# cycle
		p,p+w, 		# sclk en
		0,p+w+p,	# cs
		0, p+w+p, 	# mosi
		p,p+w, 		# miso
		padding + command + padding,
		freq_sel=1,
		miso_edge="falling")

def max111xx_check_once(conn, portID, slaveID, chipID):
	m_config1 = 0x00008064  # single end ref; no avg; scan 16; normal power; echo on
	m_config2 = 0x00008800  # single end channels (0/1 -> 14/15, pdiff_com)
	m_config3 = 0x00009000  # unipolar convertion for channels (0/1 -> 14/15)
	m_control = 0x00000826  # manual external; channel 0; reset FIFO; normal power; ID present; CS control

	reply = max111xx_ll(conn, portID, slaveID, chipID, [(m_config1 >> 8) & 0xFF, m_config1 & 0xFF])
	reply = max111xx_ll(conn, portID, slaveID, chipID, [(m_config2 >> 8) & 0xFF, m_config2 & 0xFF])
	reply = max111xx_ll(conn, portID, slaveID, chipID, [(m_config3 >> 8) & 0xFF, m_config3 & 0xFF])

	if reply[1] == 0xFF and reply[2] == 0xFF:
		return False

	if not (reply[1] == 0x88 and reply[2] == 0x0):
		return False

	reply = max111xx_ll(conn, portID, slaveID, chipID, [(m_control >> 8) & 0xFF, m_control & 0xFF])
	if not(reply[1] == 0x90 and reply[2] == 0x0):
		return False

	return True

def max111xx_check(conn, portID, slaveID, chipID): #! This fixes a known bug of unknown origin
	for _ in range(2): check_result = max111xx_check_once(conn, portID, slaveID, chipID)
	return check_result

def max111xx_read(conn, portID, slaveID, chipID, channelID):
	m_control = 0x00000826  # manual external; channel 0; reset FIFO; normal power; ID present; CS control
	m_repeat = 0x00000000

	command = m_control + (channelID << 7)
	reply = max111xx_ll(conn, portID, slaveID, chipID, [(command >> 8) & 0xFF, command & 0xFF])
	reply = max111xx_ll(conn, portID, slaveID, chipID, [(m_repeat >> 8) & 0xFF, m_repeat & 0xFF])
	v = reply[1] * 256 + reply[2]
	u = v & 0b111111111111
	ch = (v >> 12)
	assert ch == channelID
	return u


def ltc2984_ll(conn, portID, slaveID, chipID, command, rd):
                w = 8 * len(command)
                padding = [0x00 for n in range(2) ]
                p = 8 * len(padding)


                # Pad the cycle with zeros
                return conn.spi_master_execute(portID, slaveID, chipID,
                        p+w+rd+p,               # cycle
                        p,p+rd+w,               # sclk en
                        p-1,p+w+rd+1,   # cs
                        0, p+w+p,       # mosi
                        p+w,p+w+rd,             # miso
                        padding + command + padding,
                        freq_sel = 4, miso_edge="falling")

def ltc2984_write(conn, portID, slaveID, chipID, addr, n, value):
        cmd = (addr << (n*8)) | value
        cmd = [ 0x02 ] + [ (cmd >> (8*k)) & 0xFF for k in range(n+1, -1, -1) ]
        ltc2984_ll(conn, portID, slaveID, chipID, cmd, 0)

def ltc2984_read(conn, portID, slaveID, chipID, addr, n):
        cmd = addr
        cmd = [ 0x03] + [ (cmd >> (8*k)) & 0xFF for k in range(1, -1, -1) ]
        r = ltc2984_ll(conn, portID, slaveID, chipID, cmd, 8*n)
        return r


def si534x_ll(conn, portID, slaveID, chipID, command):
	w = 8 * len(command)
	padding = [0xFF for n in range(2) ]
	p = 8 * len(padding)

	# Pad the cycle with zeros
	return conn.spi_master_execute(portID, slaveID, chipID,
		p+w+p, 		# cycle
		0,p+w+p, 		# sclk en
		p,p+w,	# cs
		0, p+w+p, 	# mosi
		p,p+w, 		# miso
		padding + command + padding,
		miso_edge="falling",
		freq_sel=4)

def si534x_command(conn, portID, slaveID, chipID, command):
	return si534x_ll(conn, portID, slaveID, chipID, command)


def max5136_ll(conn, portID, slaveID, chipID, command):
	"""! MAX5136 DAC SPI low level coding

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param data Data to be transmitted over the SPI bus.

	@return Data received from the SPI bus returned by spi_master_execute()
	"""
	w = 8 * len(command)
	padding = [0xFF for n in range(1) ]
	p = 8 * len(padding)

	# Pad the cycle with zeros
	return conn.spi_master_execute(portID, slaveID, chipID,
		p+w+p, 		# cycle
		p,p+w, 		# sclk en
		p-1,p+w+1,	# cs
		0, p+w+p, 	# mosi
		p,p+w, 		# miso
		padding + command + padding,
		freq_sel = 1,	
		miso_edge = "falling", mosi_edge = "falling")

def max5136_wrt_through(conn, portID, slaveID, chipID, channelID, value):
	"""! Set MAX5136 DAC

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param channelID DAC channel number to be set
	@param value DAC value to be set

	@return Data received from the SPI bus returned by spi_master_execute()
	"""
	if channelID not in [0 , 1]:
		DACException("Invalid DAC channelID.")

	control_byte = 0b00110000 | (0b1 << channelID)
	dac_high = value >> 8
	dac_low  = value & 0xFF
	
	command = [control_byte, dac_high, dac_low]

	return max5136_ll(conn, portID, slaveID, chipID, command)

##
## EEPROMS
## Many SPI EEPROMs of a given typpe have similiar protocols for reading
##

# Maximum data read/write size the system can handle
MAX_PROM_DATA_PACKET_SIZE = 4

class EEPROM_Exception(Exception):
	pass

class EEPROM_Timeout(EEPROM_Exception):
	pass

class EEPROM_EraseError(EEPROM_Exception):
	pass

class EEPROM_WriteError(EEPROM_Exception):
	pass

## 8K NOR
## M95080 SPI EEPROM

def m95080_ll(conn, portID, slaveID, spiID, command, read_count):
    """! ST m95080 low level coding

    @param conn daqd connection object
    @param portID FEB/D portID
    @param slaveID FEB/D slaveID
    @param spiID SPI slave number
    @param data Data to be transmitted over the SPI bus.

    @return Data received from the SPI bus returned by spi_master_execute()
    """
    w = 8 * len(command)
    r = 8 * read_count
    p = 1
    w_padding = [ 0xFF for n in range(p) ]
    r_padding = [ 0xFF for n in range(p + read_count) ]
    p = 8 * p

    # Pad the cycle with zeros
    return conn.spi_master_execute(portID, slaveID, spiID,
        p+w+r+p, 		# cycle
        p,w+r+p+1, 		# sclk en
        p-1,p+w+r+0,    # cs
        0, p+w+r+p, 	# mosi
        p+w, w+r+p,		# miso
        w_padding + command + r_padding, #mosi data
        freq_sel = 1, # WORKAROUND: 10 MHz operation unreliable with FEB/D 8K revA (at least)
        miso_edge = "falling", mosi_edge = "rising")

def m95080_wip(conn, portID, slaveID, spiID, MAX_TRIES = 5):
    """! Return ST m95080 EEPROM Write in Progress Status

    @param conn daqd connection object
    @param portID FEB/D portID
    @param slaveID FEB/D slaveID
    @param spiID SPI slave number
    @param MAX_TRIES maximun number of retry

    @return bool
    """
    # Check if Write In Progress is set and if so, sleep and try again
    n = 0
    while (n < MAX_TRIES):
        n += 1
        r = m95080_ll(conn, portID, slaveID, spiID, [0b00000101], 1)
        if r[1] & 0x01 == 0:
            return False
        time.sleep(0.010)

    return True

def m95080_read(conn, portID, slaveID, spiID, address, n_bytes):
    """! Read ST m95080 EEPROM

    @param conn daqd connection object
    @param portID FEB/D portID
    @param slaveID FEB/D slaveID
    @param spiID SPI slave number
    @param address Initial address from where data is to be read
    @param n_bytes Number of bytes to be read

    @return Data read from EEPROM
    """
    #Check Write in Progress
    if not m95080_wip(conn, portID, slaveID, spiID):
        pass
    else:
        return False

    # Break down reads into MAX_PROM_DATA_PACKET_SIZE byte chunks due to DAQ
    rr = bytes()
    for a in range(address, address + n_bytes, 1):
        count = min([1, address + n_bytes - a])
        r = m95080_ll(conn, portID, slaveID, spiID, [0b00000011, (a >> 8) & 0xFF, a & 0xFF], count)
        r = r[1:-1]
        rr += r
    return rr


def m95080_write(conn, portID, slaveID, spiID, address, data):
    """! Write ST m95080 EEPROM

    @param conn daqd connection object
    @param portID FEB/D portID
    @param slaveID FEB/D slaveID
    @param spiID SPI slave number
    @param address Initial address  where data is to be written to
    @param data Data to be written to EEPROM

    @return bool
    """
    n_bytes = len(data)
    # Break down reads into MAX_PROM_DATA_PACKET_SIZE byte chunks due to DAQ
    for a in range(address, address + n_bytes, 1):
        count = min([1, address + n_bytes - a])

        #Check Write in Progress
        if not m95080_wip(conn, portID, slaveID, spiID):
            pass
        else:
            return False

        #Write Enable
        m95080_ll(conn, portID, slaveID, spiID, [0b00000110], 0)

        #Write
        first_byte = a - address
        packet = data[first_byte:first_byte+count] 
        m95080_ll(conn, portID, slaveID, spiID, [0b00000010, (a >> 8) & 0xFF, a & 0xFF] + packet, 0)

    return True



## 256K NOR
## M95256 SPI EEPROM

def m95256_ll(conn, portID, slaveID, chipID, command, read_count):
	"""! ST M95256 low level coding

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param data Data to be transmitted over the SPI bus.

	@return Data received from the SPI bus returned by spi_master_execute()
	"""

	w = 8 * len(command)
	r = 8 * read_count
	p = 2
	w_padding = [ 0xFF for n in range(p) ]
	r_padding = [ 0xFF for n in range(p + read_count) ]
	p = 8 * p

	# Pad the cycle with zeros
	return conn.spi_master_execute(portID, slaveID, chipID,
		p+w+r+p, 		# cycle
		p,p+w+r+1, 		# sclk en
		p-0,p+w+r+0,		# cs
		0, p+w+r+p, 		# mosi
		p+w,p+w+r, 		# miso
		w_padding + command + r_padding,
		freq_sel = 0,
		miso_edge = "falling")

def m95256_read(conn, portID, slaveID, chipID, address, n_bytes):
	"""! Read ST M95256 EEPROM

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param address Initial address from where data is to be read
	@param n_bytes Number of bytes to be read

	@return Data read from EEPROM
	"""


	# Break down reads into 4 byte chunks due to DAQ
	rr = bytes()
	for a in range(address, address + n_bytes, 2):
		count = min([2, address + n_bytes - a])
		r = m95256_ll(conn, portID, slaveID, chipID, [0b00000011, (a >> 8) & 0xFF, a & 0xFF], count)
		r = r[1:-1]
		rr += r
	return rr

def m95256_write(conn, portID, slaveID, chipID, address, data):
	"""! Write ST M95256 EEPROM

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param address Initial address  where data is to be written to
	@param data Data to be written to EEPROM

	@return None
	"""

	while True:
		# Check if Write In Progress is set and if so, sleep and try again
		r = m95256_ll(conn, portID, slaveID, chipID, [0b00000101], 1)
		if r[1] & 0x01 == 0:
			break
		time.sleep(0.010)

	# cycle WEL
	m95256_ll(conn, portID, slaveID, chipID, [0b00000100], 1)
	m95256_ll(conn, portID, slaveID, chipID, [0b00000110], 1)

	m95256_ll(conn, portID, slaveID, chipID, [0b00000010, (address >> 8) & 0xFF, address & 0xFF] + data, 0)
	while True:
		# Check if Write In Progress is set and if so, sleep and try again
		r = m95256_ll(conn, portID, slaveID, chipID, [0b00000101], 1)
		if r[1] & 0x01 == 0:
			break
		time.sleep(0.010)

	# Disable WEL (it should be automatic but...)
	m95256_ll(conn, portID, slaveID, chipID, [0b00000100], 1)
	return None


##
## 128 Mbit NAND
## Currently MX25L12835F and N25Q128A are being used


def generic_nand_flash_ll(conn, portID, slaveID, chipID, command, read_count):
	"""! Generic SPI NAND flash low level coding

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param data Data to be transmitted over the SPI bus.

	@return Data received from the SPI bus returned by spi_master_execute()
	"""

	w = 8 * len(command)
	r = 8 * read_count
	p = 2
	w_padding = [ 0xFF for n in range(p) ]
	r_padding = [ 0xFF for n in range(p + read_count) ]
	p = 8 * p

	# Pad the cycle with zeros
	return conn.spi_master_execute(portID, slaveID, chipID,
		p+w+r+p, 		# cycle
		p,p+w+r+1, 		# sclk en
		p-0,p+w+r+0,		# cs
		0, p+w+r+p, 		# mosi
		p+w,p+w+r, 		# miso
		w_padding + command + r_padding,
		freq_sel = 0,
		miso_edge = "falling")


def generic_nand_flash_getid(conn, portID, slaveID, chipID):
	"""! Get generic SPI NAND flash identification information

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number

	@return Data read from Flash
	"""

	return generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x9F], 3)

def generic_nand_flash_read(conn, portID, slaveID, chipID, offset, count):
	"""! Read data from generic SPI NAND flash

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param address Initial address from where data is to be read
	@param count Number of bytes to be read

	@return Read data
	"""

	d = bytes()
	for addr in range(offset, offset + count, MAX_PROM_DATA_PACKET_SIZE):
		c = min(MAX_PROM_DATA_PACKET_SIZE, count, offset + count - addr)
		r = generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x03, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF], c)
		r = r[1:-1]
		d += r

	return d


def generic_nand_flash_wait_write(conn, portID, slaveID, chipID, timeout=1):
	"""! Wait for any pending write operations on a generic SPI NAND flash to complete

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number

	@return None
	"""

	t0 = time.time()
	while (time.time() - t0) < timeout:
		# Check if Write In Progress is set and if so, time.sleep and try again
		r = generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x05], 1)
		if r[1] & 0x01 == 0:
			return None
		time.sleep(0.010)

	raise EEPROM_Timeout()


def n25q128a_bulk_erase(conn, portID, slaveID, chipID):
	"""! Bulk erase a N25Q128A flash

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number

	@return None
	"""

	# Wait for any pending operations
	generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)
	# Clear flags register
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x50], 0)

	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x06], 0)
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0xC7], 0)
	generic_nand_flash_wait_write(conn, portID, slaveID, chipID, timeout=300)

	flags = generic_nand_flash_ll(conn, portID, slaveID, chipID, [ 0x70  ], 1)
	if flags[1] & 0x20 != 0:
		raise EEPROM_EraseError()


	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)
	return None


def n25q128a_64k_erase(conn, portID, slaveID, chipID, sectorOffset, sectorCount):
	"""! Erase 64KiB sectors on a N25Q128A flash

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param sectorOffset First sector to be erased
	@param sectorCount Number of sectors to be erased

	@return None
	"""

	# Wait for any pending operations
	generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)
	# Clear flags register
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x50], 0)


	for sector in range(sectorOffset, sectorOffset + sectorCount):
		generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x06], 0)
		# 64 KiB sector erase takes address in bytes but only at 64 KiB boundaries
		generic_nand_flash_ll(conn, portID, slaveID, chipID, [0xD8, sector, 0x00, 0x00 ], 0)
		generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
		flags = generic_nand_flash_ll(conn, portID, slaveID, chipID, [ 0x70  ], 1)
		if flags[1] & 0x20 != 0:
			raise EEPROM_EraseError()

	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)
	return None


def n25q128a_write(conn, portID, slaveID, chipID, offset, data):
	"""! Write data to N25Q128A flash

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param address Initial address  where data is to be written to
	@param data Data to be written to EEPROM

	@return None
	"""

	# Wait for any pending operations
	generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)
	# Clear flags register
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x50], 0)

	for o in range(0, len(data), MAX_PROM_DATA_PACKET_SIZE):
		d = data[o:o+MAX_PROM_DATA_PACKET_SIZE]
		d = list(d)
		l = len(d)

		addr = offset + o

		generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x06], 0)
		generic_nand_flash_ll(conn, portID, slaveID, chipID, [ 0x02, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF ] + d, 0);
		generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
		flags = generic_nand_flash_ll(conn, portID, slaveID, chipID, [ 0x70  ], 1)
		if flags[1] & 0x10 != 0:
			raise EEPROM_WriteError()

	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)

def mx25l12835f_bulk_erase(conn, portID, slaveID, chipID):
	"""! Bulk erase a N25Q128A flash

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number

	@return None
	"""

	# Wait for any pending operations
	generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)

	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x06], 0)
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0xC7], 0)
	generic_nand_flash_wait_write(conn, portID, slaveID, chipID, timeout=300)

	flags = generic_nand_flash_ll(conn, portID, slaveID, chipID, [ 0x2B  ], 1)
	if flags[1] & 0x40 != 0:
		raise EEPROM_EraseError()


	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)
	return None


def mx25l12835f_64k_erase(conn, portID, slaveID, chipID, sectorOffset, sectorCount):
	"""! Erase 64KiB sectors on a N25Q128A flash

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param sectorOffset First sector to be erased
	@param sectorCount Number of sectors to be erased

	@return None
	"""

	# Wait for any pending operations
	generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)


	for sector in range(sectorOffset, sectorOffset + sectorCount):
		generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x06], 0)
		# 64 KiB sector erase takes address in bytes but only at 64 KiB boundaries
		generic_nand_flash_ll(conn, portID, slaveID, chipID, [0xD8, sector, 0x00, 0x00 ], 0)
		generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
		flags = generic_nand_flash_ll(conn, portID, slaveID, chipID, [ 0x2B  ], 1)
		if flags[1] & 0x40 != 0:
			raise EEPROM_EraseError()

	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)
	return None


def mx25l12835f_write(conn, portID, slaveID, chipID, offset, data):
	"""! Write data to N25Q128A flash

	@param conn daqd connection object
	@param portID FEB/D portID
	@param slaveID FEB/D slaveID
	@param chipID SPI slave number
	@param address Initial address  where data is to be written to
	@param data Data to be written to EEPROM

	@return None
	"""

	# Wait for any pending operations
	generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)

	for o in range(0, len(data), MAX_PROM_DATA_PACKET_SIZE):
		d = data[o:o+MAX_PROM_DATA_PACKET_SIZE]
		d = list(d)
		l = len(d)

		addr = offset + o

		generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x06], 0)
		generic_nand_flash_ll(conn, portID, slaveID, chipID, [ 0x02, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF ] + d, 0);
		generic_nand_flash_wait_write(conn, portID, slaveID, chipID)
		flags = generic_nand_flash_ll(conn, portID, slaveID, chipID, [ 0x2B  ], 1)
		if flags[1] & 0x20 != 0:
			raise EEPROM_WriteError()

	# Ensure WEL is disabled
	generic_nand_flash_ll(conn, portID, slaveID, chipID, [0x04], 0)

