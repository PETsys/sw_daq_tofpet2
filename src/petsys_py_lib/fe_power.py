# kate: indent-mode: python; indent-pasted-text false; indent-width 4; replace-tabs: on;
# vim: tabstop=8 softtabstop=0 expandtab shiftwidth=4 smarttab

from . import i2c, spi
from time import sleep
from itertools import chain

DS44XX_ADR  = { "vdd1" : [0x90, 0xF8, 0x4], # [chipID, regID, muxID]
                "vdd2" : [0x90, 0xF8, 0x5], # [chipID, regID, muxID]
                "vdd3" : [0x90, 0xF9, 0x5]} # [chipID, regID, muxID]

VDD1_TARGET = 1.4
VDD2_TARGET = 2.75
VDD3_TARGET = 3.25

VDD1_PRESET = {"min" : 0x80 + 0x7F, "max": 0x00 + 0x7F, "baseline": 12}
VDD2_PRESET = {"min" : 0x80 + 0x7F, "max": 0x00 + 0x7F, "baseline": 0x80 + 23}
VDD3_PRESET = {"min" : 0x80 + 0x7F, "max": 0x00 + 0x7F, "baseline": 45}

class RSenseReadError(Exception):
    def __init__(self, portID, slaveID, busID, adcID):
        self.portID, self.slaveID, self.busID, self.adcID = portID, slaveID, busID, adcID
        self.message = f"ERROR: (portID, slaveID) = ({portID}, {slaveID}). Unable to communicate with ADC @ busID = {busID} | adcID = {adcID}."
        super().__init__(self.message)

class DACMaximumReached(Exception):
    def __init__(self, portID, slaveID, busID, rail):
        self.portID, self.slaveID, self.busID, self.rail = portID, slaveID, busID, rail
        self.message = f"ERROR: (portID, slaveID) = ({portID}, {slaveID}). {rail.upper()} DAC @ busID {busID} maxed out. FEB/I may require external power."
        super().__init__(self.message)

def read_power_good(conn, portID, slaveID):
    cfg_reg = spi.spi_reg_ll(conn, portID, slaveID, 0x901B, [ 0x00, 0x00 ])   # {2'b00, !switch1, leds_oe, vdd3_pg, vdd2_pg, vdd1_pg }
    return [cfg_reg[2] & 0x0F, (cfg_reg[2] >> 4) & 0x0F, cfg_reg[1] & 0x0F] # [vdd1_pg, vdd2_pg, vdd3_pg]

def chk_power_good(conn, portID, slaveID, busID):
    pg_lst = [ (x >> (busID-1)) & 0x1 for x in read_power_good(conn, portID, slaveID)]
    if pg_lst != [1, 1, 1]:
        fem_power_8k(conn, portID, slaveID, "off")
        print(f'ERROR: Power Good Check FAILED @ busID {busID}! pg_reg = {pg_lst} !')
        exit(1)
    return pg_lst


def read_sense(conn, portID, slaveID, busID, debug = False, gnd_max_filter = 0.1):
    if debug: print(f'Reading busID {busID}')
    reading = []
    adc_base_addr = 0x9000 
    adcID = busID - 1
    if not spi.max111xx_check(conn, portID, slaveID, adc_base_addr + adcID): 
        raise RSenseReadError(portID,slaveID,busID,adcID) 
    for port in range(4):
        port_reading = []
        if debug: print(f"busID {busID} port {port} = ", end="  ")
        rail_name = [ "2V7", "3V2", "1V35", "GND" ]
        rail_scale = [ 2, 2, 1, 1 ]

        #Get GND Level:
        gnd_u = spi.max111xx_read(conn, portID, slaveID, adc_base_addr + adcID, 4*port + 3)
        gnd_v = 2.5 * gnd_u / 4096
        
        for rail_index in [0,1,2]:
            u = spi.max111xx_read(conn, portID, slaveID, adc_base_addr + adcID, 4*port + rail_index)
            v = (2.5 * u / 4096 * rail_scale[rail_index]) - gnd_v
            port_reading.append(v)
            if debug and gnd_v < gnd_max_filter: print(f'{rail_name[rail_index]} = {v: 5.3f}V', end = "\t")
        if debug and gnd_v < gnd_max_filter: print(f'GND = {gnd_v:5.3f}', end=" ") 
        if debug: print("")
        port_reading.append(gnd_v)
        reading.append(port_reading)

    sleep(0.001) #!This should not be needed. Possible firmware issue. Take in consideration if futures issues appear.
    return reading

def set_dac(conn, portID, slaveID, busID, rail, setting):
    chipID = DS44XX_ADR[rail][0]
    regID  = DS44XX_ADR[rail][1]
    muxID  = DS44XX_ADR[rail][2]
    i2c.PI4MSD5V9540B_set_register(conn, portID, slaveID, busID, 0xE0, muxID, debug_error=False)
    i2c.ds44xx_set_register(conn, portID, slaveID, busID, chipID, regID, setting, debug_error=False)

def set_all_dacs(conn, portID, slaveID, busID, vdd1, vdd2, vdd3):
    set_dac(conn, portID, slaveID, busID, "vdd1", vdd1) # VDD1
    set_dac(conn, portID, slaveID, busID, "vdd2", vdd2) # VDD2
    set_dac(conn, portID, slaveID, busID, "vdd3", vdd3) # VDD3

def ramp_up_rail(conn, portID, slaveID, busID, rail, range_to_iterate, max, target):
    #print(f'Ramping up {rail.upper()} rail')
    read_idx = {"vdd1" : 2, "vdd2": 0, "vdd3": 1, "gnd": 3}
    setpoint = max
    for dac_setting in range_to_iterate:
        reading_connected = []
        setpoint = dac_setting
        set_dac(conn, portID, slaveID, busID, rail, dac_setting)
        reading = read_sense(conn, portID, slaveID, busID)
        reading_connected = [r[read_idx[rail]] for r in reading if r[read_idx["gnd"]] < 0.1] #Filters for connected ports only 
        if rail == "vdd2": reading_connected = [r for r in reading_connected if r > 1.5]     #Filters for old FEB\I
        if not reading_connected:
            break;
        if min(reading_connected)  > target: 
            break;
    
    if setpoint == max:
        fem_power_8k(conn, portID, slaveID, "off")
        raise DACMaximumReached(portID, slaveID, busID, rail)
    else:
        return setpoint


def fem_power_8k(conn, portID, slaveID, power):
    if power == "on":
        #Initialize DC-DC blocks
        print("INFO: Initializing DCDC blocks.")
        busID_lst = []
        for testID in [1,2,3,4]:
            for reading in read_sense(conn, portID, slaveID, testID):
                if reading[3] < 0.1:
                    busID_lst.append(testID)
                    break;
        print(f"INFO: Found the following active busIDs = {busID_lst}")
        for busID in busID_lst:
            set_all_dacs(conn, portID, slaveID, busID, VDD1_PRESET["baseline"], VDD2_PRESET["baseline"], VDD3_PRESET["baseline"])
        #Power ON and stabilize
        conn.write_config_register(portID, slaveID, 2, 0x0213, 0b11) 
        sleep(0.2) #?Some time is required here. Minimum unknown.
        #Check Power Goods
        for busID in busID_lst:
            chk_power_good(conn, portID, slaveID, busID)
        #Ramp up rails bus by bus
        for busID in busID_lst:
            print(f"INFO: Ramping up busID {busID}.")
            vdd1_iterable =       range(VDD1_PRESET["baseline"], VDD1_PRESET["max"] + 1,  4)
            vdd2_iterable = chain(range(VDD2_PRESET["baseline"], 0x80 + 1, -2), range(1, VDD2_PRESET["max"] + 1, 4))
            vdd3_iterable =       range(VDD3_PRESET["baseline"], VDD3_PRESET["max"] + 1,  4)

            vdd1_setpoint = ramp_up_rail(conn, portID, slaveID, busID, "vdd1", vdd1_iterable, VDD1_PRESET["max"], VDD1_TARGET)
            vdd2_setpoint = ramp_up_rail(conn, portID, slaveID, busID, "vdd2", vdd2_iterable, VDD2_PRESET["max"], VDD2_TARGET)
            vdd3_setpoint = ramp_up_rail(conn, portID, slaveID, busID, "vdd3", vdd3_iterable, VDD3_PRESET["max"], VDD3_TARGET)

            #Ramp up VDD1 again
            vdd1_iterable2 = range(vdd1_setpoint, VDD1_PRESET["max"] + 1, 1)
            vdd1_setpoint = ramp_up_rail(conn, portID, slaveID, busID, "vdd1", vdd1_iterable2, VDD1_PRESET["max"], VDD1_TARGET)
            
            #read_sense(conn, portID, slaveID, busID, debug = True)
        #Check Power Goods
        for busID in busID_lst:
            chk_power_good(conn, portID, slaveID, busID)
        print("INFO: Power is ON.")

    
    elif power == "off":
        conn.write_config_register(portID, slaveID, 2, 0x0213, 0)
        for busID in [1,2,3,4]:
            try:
                set_all_dacs(conn, portID, slaveID, busID, 0, 0, 0)
            except:
                print(f"WARNING: FAILED TO COMMUNICATE WITH DCDC MODULE {busID}. IF A MODULE IS PRESENT CONTACT SUPPORT.")
            else:
                print(f"INFO: Shutting down DCDC module {busID}")
        print("INFO: Power is OFF.")

def fem_power_original(conn, portID, slaveID, power):
    reg_value = 0
    if power == 'on':
        reg_value = 0b11

    conn.write_config_register(portID, slaveID, 2, 0x0213, reg_value) 

def set_fem_power(conn, portID, slaveID, power):
    base_pcb = conn.read_config_register(portID, slaveID, 16, 0x0000)

    if base_pcb in [ 0x0005 ]:
        fem_power_8k(conn, portID, slaveID, power)
    else:
        fem_power_original(conn, portID, slaveID, power)
