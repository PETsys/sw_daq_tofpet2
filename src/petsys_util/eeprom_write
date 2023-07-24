#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys, re
import argparse
from petsys import daqd, fe_eeprom, fe_power # type: ignore 

def expand_ranges(s): #Magical input parser
    lst = re.sub(r'(\d+)-(\d+)',lambda match: ','.join(str(i) for i in range(int(match.group(1)),int(match.group(2)) + 1)), s).split(",")
    for idx,x in enumerate(lst):
        try:
            lst[idx] = int(x, 10)
        except:
            lst[idx] = int(x, 16)
    return lst

def main(argv):
    parser = argparse.ArgumentParser(description='EEPROM Programming utility for Petsys Front-End-Modules')
    parser.add_argument("--fem",        type=str, required=True ,  choices=list(fe_eeprom.FEM_PARAMETERS.keys()), 
                        help="FEM Type on the system (all must be the same)")
    parser.add_argument("--serial",     type=str, required=False,                        
                        help="Comma separated list of SNs (eg: 1,7,2-5,6)")
    parser.add_argument("--cfg",        type=str, required=False , choices=list(fe_eeprom.S_CFG_OPTIONS.keys()),    
                        help="Sensor Configuration Type on the system (all must be the same)", default='default')
    parser.add_argument("--custom-cfg", type=str, required=False,                        
                        help="Comma separated list of Tsensor cfg bytes (hex or decimal accepted)")
    args = parser.parse_args()

    #FEM type 
    fem_type = args.fem
    print(f'INFO: Selected FEM type is "{fem_type}".')

    #Serial Numbers
    if args.serial:  
        print('INFO: New Serial Numbers inputed.')
        new_sn_lst = expand_ranges(args.serial)
    else:
        new_sn_lst = None
    
    #Sensor Configuration
    if args.custom_cfg:  
        print('INFO: Custom temperature sensor configuration selected.')
        new_s_cfg_lst = expand_ranges(args.custom_cfg)
        for value in new_s_cfg_lst:
            if value > 0xFF:
                print('ERROR: Invalid configuration. Value > 0xFF detected. A byte only has 8 bits!')
                exit(1)
    elif args.cfg:
        print(f'INFO: Selected sensor configuration is "{args.cfg}".')
        new_s_cfg_lst = fe_eeprom.S_CFG_OPTIONS[args.cfg]
    else:
        new_s_cfg_lst = None

    conn = daqd.Connection()
    conn.initializeSystem()
    
    fe_eeprom.program_m95080(conn,fem_type,new_sn_lst,new_s_cfg_lst)
    
    for portID, slaveID in conn.getActiveFEBDs():
        fe_power.set_fem_power(conn, portID, slaveID, 'off')

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
