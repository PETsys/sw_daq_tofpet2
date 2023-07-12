# kate: indent-mode: python; indent-pasted-text false; indent-width 4; replace-tabs: on;
# vim: tabstop=8 softtabstop=0 expandtab shiftwidth=4 smarttab

from . import i2c, spi, fe_power
from time import sleep, time

DS44XX_ADR  =   {
                'TI'     :  {       # [chipID, regID, muxID]
                             "vdd1" : [0x96, 0xF8, None],
                             "vdd2" : [0x90, 0xF8, None],
                             "vdd3" : [0x90, 0xF9, None]
                            },
                'MURATA' :  {       # [chipID, regID, muxID]
                             "vdd1" : [0x90, 0xF8, 0x4],
                             "vdd2" : [0x90, 0xF8, 0x5],
                             "vdd3" : [0x90, 0xF9, 0x5]
                            }
                }

VDD1_TARGET = 1.35
VDD2_TARGET = 2.75
VDD3_TARGET = 3.25

VDD1_PRESET = { 'TI'    : {"min":  -31, "max":   31, "baseline":  -30},
                'MURATA': {"min": -127, "max":  127, "baseline":    2} }
VDD2_PRESET = { 'TI'    : {"min": -127, "max":  127, "baseline":   37},
                'MURATA': {"min": -127, "max":  127, "baseline":  -23} }
VDD3_PRESET = { 'TI'    : {"min": -127, "max":  127, "baseline":   69},
                'MURATA': {"min": -127, "max":  127, "baseline":   40} }


class PowerGoodError(Exception): 
    def __init__(self, portID, slaveID):
        self.portID, self.slaveID = portID, slaveID
        self.message = f"ERROR: Failed FEM power good check! @ (portID, slaveID) = ({portID}, {slaveID})."
        super().__init__(self.message)

class RSenseReadError(Exception):
    def __init__(self, portID, slaveID, busID, adcID):
        self.portID, self.slaveID, self.busID, self.adcID = portID, slaveID, busID, adcID
        self.message = f"ERROR: (portID, slaveID) = ({portID}, {slaveID}). Unable to communicate with ADC @ busID = {busID} | adcID = {adcID}."
        super().__init__(self.message)

class DACMaximumReached(Exception):
    def __init__(self, portID, slaveID, busID, rail, status):
        self.portID, self.slaveID, self.busID, self.rail = portID, slaveID, busID, rail
        m = [ "%d: %4.2f V" % (4*(busID - 1) + i, vdd_effective) for i, vdd_effective in status.items() ]
        m = ", ".join(m)
        self.message = f"ERROR: (portID, slaveID) = ({portID}, {slaveID}). {rail.upper()} DAC @ busID {busID} maxed out ({m})."
        super().__init__(self.message)

def read_power_good(conn, portID, slaveID):
    cfg_reg = spi.spi_reg_ll(conn, portID, slaveID, 0x901B, [ 0x00, 0x00 ])   # {2'b00, !switch1, leds_oe, vdd3_pg, vdd2_pg, vdd1_pg }
    return [cfg_reg[2] & 0x0F, (cfg_reg[2] >> 4) & 0x0F, cfg_reg[1] & 0x0F] # [vdd1_pg, vdd2_pg, vdd3_pg]

def chk_power_good(conn, portID, slaveID, busID):
    t0  = time()
    while (time() - t0) < 1.0:
        pg_lst = [ (x >> (busID-1)) & 0x1 for x in read_power_good(conn, portID, slaveID)]
        if pg_lst == [1, 1, 1]: break
        sleep(0.1)

    if pg_lst != [1, 1, 1]:
        set_fem_power(conn, portID, slaveID, "off")
        raise PowerGoodError(portID, slaveID)
    return pg_lst

def get_module_version(conn, portID, slaveID, busID):
    try:
        i2c.PI4MSD5V9540B_set_register(conn, portID, slaveID, busID, 0xE0, 0x5, debug_error=False) #Check for Murata DCDC Multiplexer
    except:
        return 'TI'
    else:
        return 'MURATA'

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

def read_dac_ti(conn, portID, slaveID, busID, rail):
    chipID = DS44XX_ADR['TI'][rail][0]
    regID  = DS44XX_ADR['TI'][rail][1]
    return i2c.ds44xx_read_register(conn, portID, slaveID, busID, chipID, regID)

def set_dac_ti(conn, portID, slaveID, busID, rail, setting):
    chipID = DS44XX_ADR['TI'][rail][0]
    regID  = DS44XX_ADR['TI'][rail][1]
    i2c.ds44xx_set_register(conn, portID, slaveID, busID, chipID, regID, setting, debug_error=False)

def read_dac_murata(conn, portID, slaveID, busID, rail):
    chipID = DS44XX_ADR['MURATA'][rail][0]
    regID  = DS44XX_ADR['MURATA'][rail][1]
    muxID  = DS44XX_ADR['MURATA'][rail][2]
    i2c.PI4MSD5V9540B_set_register(conn, portID, slaveID, busID, 0xE0, muxID, debug_error=False)
    return i2c.ds44xx_read_register(conn, portID, slaveID, busID, chipID, regID)

def set_dac_murata(conn, portID, slaveID, busID, rail, setting):
    chipID = DS44XX_ADR['MURATA'][rail][0]
    regID  = DS44XX_ADR['MURATA'][rail][1]
    muxID  = DS44XX_ADR['MURATA'][rail][2]
    i2c.PI4MSD5V9540B_set_register(conn, portID, slaveID, busID, 0xE0, muxID, debug_error=False)
    i2c.ds44xx_set_register(conn, portID, slaveID, busID, chipID, regID, setting, debug_error=False)

#Integer to DAC format setting converter (bit 8 is source/sink selector)
def int_to_dac(value):
    if value < 0:
        return 0x80 + abs(value)
    else:
        return value

#Interface function for different DCDC modules
def read_dac(conn, portID, slaveID, busID, moduleVersion, rail):
    if moduleVersion == 'MURATA':
        return read_dac_murata(conn, portID, slaveID, busID, rail)
    elif moduleVersion == 'TI':
        return read_dac_ti(conn, portID, slaveID, busID, rail)

def set_dac(conn, portID, slaveID, busID, moduleVersion, rail, setting):
    converted_setting = int_to_dac(setting)
    if moduleVersion == 'MURATA':
        set_dac_murata(conn, portID, slaveID, busID, rail, converted_setting)
    elif moduleVersion == 'TI':
        set_dac_ti(conn, portID, slaveID, busID, rail, converted_setting)

def set_all_dacs(conn, portID, slaveID, busID, moduleVersion, vdd1, vdd2, vdd3):
    set_dac(conn, portID, slaveID, busID, moduleVersion, "vdd1", vdd1) # VDD1
    set_dac(conn, portID, slaveID, busID, moduleVersion, "vdd2", vdd2) # VDD2
    set_dac(conn, portID, slaveID, busID, moduleVersion, "vdd3", vdd3) # VDD3

def ramp_up_rail(conn, portID, slaveID, busID, moduleVerison, rail, range_to_iterate, max, target):
    #print(f'Ramping up busID {busID} : {rail.upper()} rail ({moduleVerison} module)')
    read_idx = {"vdd1" : 2, "vdd2": 0, "vdd3": 1, "gnd": 3}
    setpoint = max
    for dac_setting in range_to_iterate:
        reading_connected = []
        setpoint = dac_setting
        set_dac(conn, portID, slaveID, busID, moduleVerison, rail, dac_setting)
        reading = read_sense(conn, portID, slaveID, busID)

        reading_connected = [ (i, (r[read_idx[rail]] - r[read_idx["gnd"]])) for i, r in enumerate(reading) if r[read_idx["gnd"]] < 0.1 ]
        if rail == "vdd2":
            reading_connected = [ (i, vdd_effective) for i, vdd_effective in reading_connected if vdd_effective > 1.5 ]

        reading_connected = dict(reading_connected)

        if not reading_connected:
            break;
        if min(reading_connected.values())  > target: 
            break;
    
    if setpoint == max:
        set_fem_power(conn, portID, slaveID, "off")
        raise DACMaximumReached(portID, slaveID, busID, rail, reading_connected )
    else:
        return setpoint

def detect_active_bus(conn, portID, slaveID, testID_lst = [1,2,3,4]):
    busID_lst = []
    for testID in testID_lst:
        for reading in read_sense(conn, portID, slaveID, testID):
            if reading[3] < 0.1:
                detected_module_type = get_module_version(conn, portID, slaveID, testID)
                busID_lst.append((testID,detected_module_type))
                break;
    return busID_lst

def set_fem_power(conn, portID, slaveID, power):
    if power == "on":
        #Initialize DC-DC blocks
        print("INFO: Initializing DCDC blocks.")
        busID_lst = detect_active_bus(conn, portID, slaveID)
        print(f"INFO: Found the following active busIDs = {busID_lst}")

        #Set all DACs to baseline values (enough for 1 ASIC directly connected to board)
        for busID, moduleVersion in busID_lst:
            set_all_dacs(conn, portID, slaveID, busID, moduleVersion, VDD1_PRESET[moduleVersion]["baseline"], VDD2_PRESET[moduleVersion]["baseline"], VDD3_PRESET[moduleVersion]["baseline"])

        #Power ON and stabilize
        bias_en = fe_power.get_bias_power_status(conn, portID, slaveID)
        power_state = 0b01 | (bias_en << 1) # Keep BIAS_EN state
        conn.write_config_register(portID, slaveID, 2, 0x0213, power_state)
        sleep(0.05)
        #Check Power Goods
        for busID, _ in busID_lst:
            chk_power_good(conn, portID, slaveID, busID)

        #Ramp up rails bus by bus
        for busID, moduleVersion in busID_lst:
            print(f"INFO: Ramping up busID {busID}.")
            vdd1_iterable = range(VDD1_PRESET[moduleVersion]["baseline"], VDD1_PRESET[moduleVersion]["max"] + 1, 3)
            vdd2_iterable = range(VDD2_PRESET[moduleVersion]["baseline"], VDD2_PRESET[moduleVersion]["max"] + 1, 4)
            vdd3_iterable = range(VDD3_PRESET[moduleVersion]["baseline"], VDD3_PRESET[moduleVersion]["max"] + 1, 3)

            vdd1_setpoint = ramp_up_rail(conn, portID, slaveID, busID, moduleVersion, "vdd1", vdd1_iterable, VDD1_PRESET[moduleVersion]["max"], VDD1_TARGET)
            vdd2_setpoint = ramp_up_rail(conn, portID, slaveID, busID, moduleVersion, "vdd2", vdd2_iterable, VDD2_PRESET[moduleVersion]["max"], VDD2_TARGET)
            vdd3_setpoint = ramp_up_rail(conn, portID, slaveID, busID, moduleVersion, "vdd3", vdd3_iterable, VDD3_PRESET[moduleVersion]["max"], VDD3_TARGET)

            #Ramp up VDD1 again
            vdd1_iterable2 = range(vdd1_setpoint, VDD1_PRESET[moduleVersion]["max"] + 1, 1)
            vdd1_setpoint = ramp_up_rail(conn, portID, slaveID, busID, moduleVersion, "vdd1", vdd1_iterable2, VDD1_PRESET[moduleVersion]["max"], VDD1_TARGET)
            
            #read_sense(conn, portID, slaveID, busID, debug = True)

        #Check Power Goods
        for busID, _ in busID_lst:
            chk_power_good(conn, portID, slaveID, busID)
        print("INFO: Power is ON.")

    
    elif power == "off":
        fe_power.set_bias_power(conn, portID, slaveID, 'off') # To reset DACs
        conn.write_config_register(portID, slaveID, 2, 0x0213, 0)
        for busID in [1,2,3,4]: #! REWRITE THIS!!!!!!!!!!!!!!!!
            try:
                set_all_dacs(conn, portID, slaveID, busID, 'TI', 0, 0, 0)
            except:
                try:
                    set_all_dacs(conn, portID, slaveID, busID, 'MURATA', 0, 0, 0)
                except:
                    print(f"WARNING: FAILED TO COMMUNICATE WITH DCDC MODULE {busID}. IF A MODULE IS PRESENT CONTACT SUPPORT.")
                else:
                    print(f"INFO: Shutting down MURATA DCDC module @ busID {busID}")
            else:
                print(f"INFO: Shutting down TI DCDC module @ busID {busID}")
        print("INFO: Power is OFF.")
