# kate: indent-mode: python; indent-pasted-text false; indent-width 4; replace-tabs: on;
# vim: tabstop=8 softtabstop=0 expandtab shiftwidth=4 smarttab

from datetime import datetime
from . import spi, info
from copy import deepcopy

#################################
#
# Configuration Parameters
#
#################################

DEVICE_TO_BYTE = {'asic' : 0xAA, 'sipm' : 0xBB}
SENSOR_TO_BYTE = {'LMT86': 0xAA, 'LMT87': 0xAB, 'LMT70': 0xBB, 'NA': 0x12}

FEM_PARAMETERS = {  
                    'fem256_petsys' : { 'unique_id'   : [140, 212, 190, 132, 107,  29,  77,  96, 165, 101,  77,  72, 252, 163,  63, 202] },
                    'fem128_c'      : { 'unique_id'   : [191, 203, 103, 147,  77,  48, 100, 252, 163, 223,  74, 225, 183, 251,  54,  93] },
                    'radialis'      : { 'unique_id'   : [ 95, 224, 251, 240,  53, 208,  17, 238, 190,  86,   2,  66, 172,  18,   0,   2] }
                 }

S_CFG_BYTES_PER_CH = 3                          
S_CFG_OPTIONS = {             #LOCATION,DEVICE,SENSOR TYPE ; 3 bytes per channel ; Each line is an ADC channelID
                'default' :[
                            1,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT86'], 
                            1,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT70'],
                            0,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT70'],
                            0,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT86'] 
                            ],
                'fem_256' :[
                            3,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT70'],
                            2,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT70'],
                            1,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT70'],
                            0,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT70'],
                            0,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            2,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            3,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            1,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87']
                            ], 
                'fem_256_p' :[
                            3,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT87'],
                            2,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT87'],
                            1,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT87'],
                            0,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['LMT87'],
                            0,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            2,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            3,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            1,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87']
                            ],
                'radialis' :[
                            3,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            2,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            0,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            1,DEVICE_TO_BYTE['asic'],SENSOR_TO_BYTE['LMT87'],
                            0,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['NA'],
                            1,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['NA'],
                            2,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['NA'],
                            3,DEVICE_TO_BYTE['sipm'],SENSOR_TO_BYTE['NA']
                            ] 
                }                 

########################################
#
# EEPROM Classes
# M95080 implemented
#
########################################

class m95080_eeprom:
    MEMORY_SIZE   = 1024
    HEADER_ADR    = 0x000
    HEADER_SIZE   = 16
    HEADER_BYTES  = [130, 30, 71, 37, 231, 71, 88, 7, 233, 40, 240, 101, 117, 147, 255, 215]
    PROM_TEMPLATE = {#NAME  : [ADR       , N_BYTES    , WORD        ]  #? Should this be a dict of dicts? 
                    'header': [HEADER_ADR, HEADER_SIZE, HEADER_BYTES], #EEPROM Programmed
                    'uid'   : [0x020     , 16         , [0]         ], #PN
                    'dt'    : [0x040     , 19         , [0]         ], #Date/Time
                    's_cfg' : [0x080     , 96         , [0]         ], #Sensor configuration
                    'sn'    : [0x100     ,  4         , [0]         ], #SN Number
                    'chksum': [0x3F0     ,  4         , [0]         ]  #Checksum
                    } 

    def __init__(self, conn, portID, slaveID, moduleID):
        self.__conn    = conn
        self.__portID  = portID
        self.__slaveID = slaveID
        self.__spiID   = moduleID * 256 + 7

    def write(self, adr, data):
        if (adr < self.HEADER_SIZE) & (data[adr:] != self.HEADER_BYTES[adr:]):
            raise Exception('ERROR: INVALID OPERATION. 16 BYTE HEADER CANNOT BE OVERWRITTEN.')
        return spi.m95080_write(self.__conn, self.__portID, self.__slaveID, self.__spiID, adr, data)
     
    def read(self, adr, n_bytes):
        return spi.m95080_read(self.__conn, self.__portID, self.__slaveID, self.__spiID, adr, n_bytes)
    
    def read_entry(self, entry):
        return self.read(self.PROM_TEMPLATE[entry][0], self.PROM_TEMPLATE[entry][1])
    
    def erase(self):
        print(f'INFO: Erasing EEPROM.')
        for adr in range(self.MEMORY_SIZE):
            self.write(adr, [0x00])

    def max10_erase(self):
        return spi.m95080_ll(self.__conn, self.__portID, self.__slaveID, self.__spiID, [0b00001000], 0)
    
    def rdsr(self): #Read Status Register
        return spi.m95080_ll(self.__conn, self.__portID, self.__slaveID, self.__spiID, [0b00000101], 1)
    
    def wren(self): #Write Enable
        return spi.m95080_ll(self.__conn, self.__portID, self.__slaveID, self.__spiID, [0b00000110], 0)
    
    def wrdi(self): #Write Disable
        return spi.m95080_ll(self.__conn, self.__portID, self.__slaveID, self.__spiID, [0b00000100], 0)
    
    def detect(self): 
        self.wren()         #Write Enable
        r = self.rdsr()     #Read Status Register
        if (r[1] != 0x02): return False
        self.wrdi()         #Write Disable
        r = self.rdsr()     #Read Status Register
        if (r[1] != 0x00): return False
        return True
    
    def is_programmed(self):
        header = [x for x in self.read(0x00,self.HEADER_SIZE)]
        if header == self.HEADER_BYTES: return True
        return False
    
    def read_sn(self):
        r = self.read_entry('sn')
        if r:
            return int.from_bytes(r, "big")
        else:
            return None
        
    def read_fem_type(self):
        r = [x for x in self.read_entry('uid')]
        for key, value in FEM_PARAMETERS.items():
            if r == value['unique_id']: return key
        return 'unknown'
    
    def verify_checksum(self):
        chksum, chksum_reading = 0, []
        for key in self.PROM_TEMPLATE.keys():
            r = self.read_entry(key)
            if key is 'chksum':
                chksum_reading = [x for x in r]
            else:
                chksum += sum(r)
        chksum = list(chksum.to_bytes(self.PROM_TEMPLATE['chksum'][1], byteorder ='big'))  
        return (chksum == chksum_reading)


###############################################
#
# Programing Functions
#
###############################################

def verify_checksum_m95080(conn, portID, slaveID, moduleID):
    eeprom = m95080_eeprom(conn,portID,slaveID,moduleID)
    if not eeprom.detect():
        print(f'WARNING: Verify CHECKSUM - EEPROM not Detected @ moduleID {portID},{slaveID},{moduleID}')
        return False
    else: 
        return eeprom.verify_checksum()

def program_m95080(conn,fem_type,new_sn_lst=None,new_s_cfg_lst=None):
    #Get list of modules 
    detected_lst = []
    for portID, slaveID in conn.getActiveFEBDs():
        base_pcb     = conn.read_config_register(portID, slaveID, 16, 0x0000)
        fw_variant   = conn.read_config_register(portID, slaveID, 64, 0x0008)
        prom_variant = None    

        for moduleID in range(info.fem_per_febd((base_pcb, fw_variant, prom_variant))):
            eeprom = m95080_eeprom(conn,portID,slaveID,moduleID)
            if eeprom.detect():
                print(f'EEPROM Detected @ moduleID {portID},{slaveID},{moduleID}')
                detected_lst.append([portID, slaveID, moduleID])

    #Serial Number Sanity Check
    if new_sn_lst:
        if (len(detected_lst) != len(new_sn_lst)):
            print(f'ERROR: Number of modules w/ EEPROM detected do not match SNs provided.)')    
            print(f'ERROR: EEPROMs detected        = {len(detected_lst)}')
            print(f'ERROR: Serial numbers provided = {len(new_sn_lst)}')
            for portID, slaveID in conn.getActiveFEBDs():
                conn.write_config_register(portID, slaveID, 2, 0x0213, 0)
            exit(1)
        
    #Program EEPROM
    for idx, [portID, slaveID, moduleID] in enumerate(detected_lst):
        eeprom = m95080_eeprom(conn,portID,slaveID,moduleID)
        was_programmed = eeprom.is_programmed()
        if was_programmed: 
            print(f'INFO: ({portID},{slaveID},{moduleID}) has been previously PROGRAMMED.')
        else:
            print(f'INFO: ({portID},{slaveID},{moduleID}) was NOT previously programmed.')

        prom_mapping = deepcopy(m95080_eeprom.PROM_TEMPLATE)

        #Set Unique ID
        prom_mapping['uid'][2] = FEM_PARAMETERS[fem_type]['unique_id']

        #Set DATE/TIME
        prom_mapping['dt'][2]  = list(datetime.now().strftime("%d/%m/%Y %H:%M:%S").encode('ascii'))

        #Set sensor configuration
        s_cfg_adr  = prom_mapping['s_cfg'][0]
        s_cfg_size = prom_mapping['s_cfg'][1]
        if new_s_cfg_lst:
            padded_s_cfg_lst = new_s_cfg_lst + [0xFF] * (s_cfg_size - len(new_s_cfg_lst)) #Pad until correct size
            s_cfg = padded_s_cfg_lst
        elif was_programmed:
            s_cfg = list(eeprom.read(s_cfg_adr, s_cfg_size))
        else:
            s_cfg = [0xFF for n in range(s_cfg_size)]
        prom_mapping['s_cfg'][2] = s_cfg

        #Set Serial Number
        sn_adr  = prom_mapping['sn'][0]
        sn_size = prom_mapping['sn'][1]
        if new_sn_lst:
            sn = list(new_sn_lst[idx].to_bytes( sn_size, byteorder ='big'))
        elif was_programmed:
            sn = list(eeprom.read(sn_adr, sn_size))
        else:
            sn = [0xFF for n in range(sn_size)]
        prom_mapping['sn'][2] = sn

        #Calculate checksum value
        chksum_size = prom_mapping['chksum'][1]
        chksum = sum([sum(i[2]) for i in prom_mapping.values()])
        chksum = list(chksum.to_bytes( chksum_size, byteorder ='big'))
        prom_mapping['chksum'][2] = chksum


        #SANITY CHECKS
        last_adr = -1
        for key, value in prom_mapping.items():
            start_adr    = value[0]
            expected_len = value[1]
            measured_len = len(value[2]) 
            if measured_len != expected_len:
                print(f'ERROR: Length of {key} is {measured_len}. Expected length is {expected_len}.')
                return False
            if start_adr <= last_adr:
                print(f'ERROR: Address of {key} overlaps last written block.')
                return False
            last_adr = start_adr + expected_len
            if last_adr >= m95080_eeprom.MEMORY_SIZE:
                print(f'ERROR: Trying to write outside PROM limits. Memory Size is {m95080_eeprom.MEMORY_SIZE} bytes.')
                return False

        #ERASE MAX10 EMULATED EEPROM (mandatory before every write)
        print(f'INFO: Erasing ({portID},{slaveID},{moduleID}). (only valid for MAX10 emulated EEPROM)')
        eeprom.max10_erase()

        #WRITE TO EEPROM
        print(f'INFO: Programming ({portID},{slaveID},{moduleID}).')
        for key, value in prom_mapping.items():
            #print(f'Writing {key}: {value[2]}') #*For Debug
            eeprom.write(value[0], value[2])

        #CONFIRM CHECKSUM 
        if not eeprom.verify_checksum():
            print('ERROR: WRITE FAILED! INVALID CHECKSUM!')
            input('Press ENTER to acknowledge..')
    
    return True

