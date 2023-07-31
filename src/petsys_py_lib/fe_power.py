# kate: indent-mode: python; indent-pasted-text false; indent-width 4; replace-tabs: on;
# vim: tabstop=8 softtabstop=0 expandtab shiftwidth=4 smarttab

from . import fe_power_8k
from time import sleep

FEM_POWER_EN_REG     = 0x0213
FEM_POWER_EN_REG_LEN = 2

FEM_POWER_FB_REG     = 0x021C
FEM_POWER_FB_REG_LEN = 8

class PowerGoodError(Exception): 
    def __init__(self, portID, slaveID):
        self.portID, self.slaveID = portID, slaveID
        self.message = f"ERROR: Failed FEM power good check! @ (portID, slaveID) = ({portID}, {slaveID})."
        super().__init__(self.message)

#BIAS POWER
def get_bias_power_status(conn, portID, slaveID):
    power_en = conn.read_config_register(portID, slaveID, FEM_POWER_EN_REG_LEN, FEM_POWER_EN_REG)
    bias_en  = (power_en >> 1) & 0b1 # fe_power_fb(1) <= bias_en
    return bias_en

def set_bias_power(conn, portID, slaveID, power):
    fem_en = get_fem_power_status(conn, portID, slaveID)
    if power == 'on': # Guarantee FEM power is ON when turning BIAS on
        if not fem_en: set_fem_power(conn, portID, slaveID, 'on')
        power_state = 0b11
    else:
        hvdac_cfg = dict.fromkeys(conn.get_hvdac_config(), 0) # New cfg with DACs at 0
        conn.set_hvdac_config(hvdac_cfg, forceAccess=False)   # Apply config
        power_state = 0b00 | fem_en # Keep FEM_EN state
    print(f'INFO: Setting BIAS power {power.upper():>3} @ (portID, slaveID) = ({portID},{slaveID})')
    conn.write_config_register(portID, slaveID, FEM_POWER_EN_REG_LEN, FEM_POWER_EN_REG, power_state)

#FEM POWER FEB\D-1K 
def chk_power_good_original(conn, portID, slaveID):
    power_fb = conn.read_config_register(portID, slaveID, FEM_POWER_FB_REG_LEN, FEM_POWER_FB_REG)
    bot_pg   = (power_fb >> 2) & 0b1 # feba_power_fb(2)	<= POWER_GD_VDD_B
    top_pg   = (power_fb >> 3) & 0b1 # feba_power_fb(3)	<= POWER_GD_VDD_T
    return bool(bot_pg & top_pg)

def set_fem_power_original(conn, portID, slaveID, power):
    print(f'INFO: Setting FEM  power {power.upper():>3} @ (portID, slaveID) = ({portID},{slaveID})')
    bias_en = get_bias_power_status(conn, portID, slaveID)
    if power == 'on':
        power_state = 0b01 | (bias_en << 1) # Keep BIAS_EN state
        conn.write_config_register(portID, slaveID, FEM_POWER_EN_REG_LEN, FEM_POWER_EN_REG, power_state)
        sleep(0.02) # Stabilization time
        if not chk_power_good_original(conn, portID, slaveID): 
            set_fem_power(conn, portID, slaveID, "off")
            raise PowerGoodError(portID, slaveID)
    else: 
        set_bias_power(conn, portID, slaveID, 'off') # To reset DACs
        power_state = 0b00
        conn.write_config_register(portID, slaveID, FEM_POWER_EN_REG_LEN, FEM_POWER_EN_REG, power_state)
    
#FEM POWER
def get_fem_power_status(conn, portID, slaveID):
    power_en = conn.read_config_register(portID, slaveID, FEM_POWER_EN_REG_LEN, FEM_POWER_EN_REG)
    fem_en   = power_en & 0b1
    return fem_en

def set_fem_power(conn, portID, slaveID, power):
    base_pcb = conn.read_config_register(portID, slaveID, 16, 0x0000)

    # Enable ASIC reset so that ASICs will be immediately go to a known state
    conn.write_config_register(portID, slaveID, 1, 0x300, 0b1)

    if base_pcb in [ 0x0005 ]: # FEB/D-8K
        fe_power_8k.set_fem_power(conn, portID, slaveID, power)
    else:
        set_fem_power_original(conn, portID, slaveID, power)

    # Disable ASIC reset
    conn.write_config_register(portID, slaveID, 1, 0x300, 0b0)

    if power == "on":
        conn.set_legacy_fem_mode(portID, slaveID)

