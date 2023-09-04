# kate: indent-mode: python; indent-pasted-text false; indent-width 8; replace-tabs: off;
# vim: tabstop=8 shiftwidth=8

from math import sqrt
from . import info, spi, fe_eeprom

def lmt87(v):
	return (13.582 - sqrt((-13.582)**2 + 4 * 0.00433 * (2230.8 - v)))/(2 * -0.00433) + 30

def lmt86(v):
    return 30-(10.888-sqrt(118.548544+0.01388*(1777.3-v)))/0.00694

def lmt87(v):
    return 30-(13.582-sqrt(184.470724+0.01732*(2230.8-v)))/0.00866

def lmt85(v):
	return (8.194 - sqrt((-8.194)**2 + 4 * 0.00262 * (1324 - v)))/(2 * - 0.00262) + 30

def lmt70(v):
    return 205.5894-0.1814103*v-3.325395*10**-6*(v)**2-1.809628*10**-9*(v)**3

def get_max111xx_spiID(module_id):
    return module_id * 256 + 4
class UnknownTemperatureSensorType(Exception):
    pass

class UnknownModuleType(Exception):
    pass

class TMP104CommunicationError(Exception):
    #!Implement This As You Wish Ricardo; Maybe pass custom error message as argument?
    def __init__(self, portID, slaveID, din, dout):
        self.portID, self.slaveID, self.din, self.dout = portID, slaveID, din, dout
        self.message = "Error Message goes here!"
        super().__init__(self.message)

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
        elif chip_type == "LMT87":
            self.__function = lambda u: lmt87(u*2.5/4.096)
        elif chip_type == "LMT70":
            self.__function = lambda u: lmt70(u*2.5/4.096)
        elif chip_type == "LMT85":
            self.__function = lambda u: lmt85(u*2.5/4.096)
        elif chip_type == "NA":
            self.__function = lambda u: u*0.0
        else:
            raise UnknownTemperatureSensorType()
        
        
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


def list_fem128(conn, portID, slaveID, module_id):
    result = []
    
    spi_id = get_max111xx_spiID(module_id)

    if not spi.max111xx_check(conn, portID, slaveID, spi_id):
        return result
    
    result.append(max111xx_sensor(conn, portID, slaveID, spi_id, 3, (portID, slaveID, module_id, 0, "asic"), "LMT86"))
    result.append(max111xx_sensor(conn, portID, slaveID, spi_id, 2, (portID, slaveID, module_id, 0, "sipm"), "LMT70"))

    result.append(max111xx_sensor(conn, portID, slaveID, spi_id, 0, (portID, slaveID, module_id, 1, "asic"), "LMT86"))
    result.append(max111xx_sensor(conn, portID, slaveID, spi_id, 1, (portID, slaveID, module_id, 1, "sipm"), "LMT70"))
        
        
    return result
        
def list_fem256(conn, portID, slaveID, module_id):
    result = []

    spi_id = get_max111xx_spiID(module_id)

    if not spi.max111xx_check(conn, portID, slaveID, spi_id):
        return result
    
    for i in range(4):		
        result.append(max111xx_sensor(conn, portID, slaveID, spi_id, i+4, (portID, slaveID, module_id, i, "asic"), "LMT86"))
        result.append(max111xx_sensor(conn, portID, slaveID, spi_id, i+0, (portID, slaveID, module_id, i, "sipm"), "LMT70"))
        
    return result
    
def list_from_eeprom(conn, portID, slaveID, module_id):
    result = []

    spi_id = get_max111xx_spiID(module_id)

    adc_ch_cfg_size = fe_eeprom.S_CFG_BYTES_PER_CH
    eeprom = fe_eeprom.m95080_eeprom(conn, portID, slaveID, module_id)
    s_cfg_adr = fe_eeprom.m95080_eeprom.PROM_TEMPLATE['s_cfg'][0]
    s_cfg_len = fe_eeprom.m95080_eeprom.PROM_TEMPLATE['s_cfg'][1]

    for adc_ch, adr in enumerate(range(s_cfg_adr, s_cfg_adr + s_cfg_len, adc_ch_cfg_size)):
        ch_cfg = [x for x in eeprom.read(adr,adc_ch_cfg_size)]
        if ch_cfg == [0xFF for x in range(adc_ch_cfg_size)]: continue
        location = ch_cfg[0]
        device   = next(key for key, value in fe_eeprom.DEVICE_TO_BYTE.items() if value == ch_cfg[1]) #Reverse dict lookup ("byte_to_device")
        s_type   = next(key for key, value in fe_eeprom.SENSOR_TO_BYTE.items() if value == ch_cfg[2]) #Reverse dict lookup ("byte_to_sensor")
        result.append(max111xx_sensor(conn, portID, slaveID, spi_id, adc_ch, (portID, slaveID, module_id, location, device), s_type))

    spi.max111xx_check(conn, portID, slaveID, spi_id)
    return result

def get_sensor_list(conn,debug=False):
    result = []
    for portID, slaveID in conn.getActiveFEBDs():
        fem_type = conn.read_config_register(portID, slaveID, 16, 0x0002)

        base_pcb, fw_variant, prom_variant  = conn.getUnitInfo(portID, slaveID)

        n_fems  = info.fem_per_febd((base_pcb, fw_variant, prom_variant))

        fw_variant = (fw_variant >> 32) & 0xFFFF

        for module_id in range(n_fems):
            eeprom = fe_eeprom.m95080_eeprom(conn,portID,slaveID,module_id)
            if eeprom.detect():
                if debug: print(f'INFO: EEPROM Detected @ moduleID {portID},{slaveID},{module_id}')
                if eeprom.is_programmed(): # Nesting required here for improved debugging
                    if debug: print(f'INFO: ({portID},{slaveID},{module_id}) has been previously PROGRAMMED. Generating list from memory.')
                    result += list_from_eeprom(conn, portID, slaveID, module_id)
            elif fw_variant == 0x0000:
                # TB64, pass
                pass
            elif fw_variant == 0x0001:
                result += list_fem128(conn, portID, slaveID, module_id)

            elif fw_variant == 0x0002:
                # Panda
                pass
            elif fw_variant == 0x0011:
                result += list_fem128(conn, portID, slaveID, module_id)

            elif fw_variant == 0x0111:
                result += list_fem256(conn, portID, slaveID, module_id)

            elif fw_variant == 0x0211:
                # FEM 512
                pass
            else:
                raise UnknownModuleType()
            
            
    return result
