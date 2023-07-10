# kate: indent-mode: python; indent-pasted-text false; indent-width 4; replace-tabs: on;
# vim: tabstop=8 softtabstop=0 expandtab shiftwidth=4 smarttab

import fe_power_8k

def fem_power_original(conn, portID, slaveID, power):
    reg_value = 0
    if power == 'on':
        reg_value = 0b01

    conn.write_config_register(portID, slaveID, 2, 0x0213, reg_value) 

def set_fem_power(conn, portID, slaveID, power):
    base_pcb = conn.read_config_register(portID, slaveID, 16, 0x0000)

    # Enable ASIC reset so that ASICs will be immediately go to a known state
    conn.write_config_register(portID, slaveID, 1, 0x300, 0b1)

    if base_pcb in [ 0x0005 ]:
        fe_power_8k.set_fem_power(conn, portID, slaveID, power)
    else:
        fem_power_original(conn, portID, slaveID, power)

    conn.write_config_register(portID, slaveID, 1, 0x300, 0b0)
    if power == "on":
        conn.set_legacy_fem_mode(portID, slaveID)

