#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, config
from copy import deepcopy
import argparse
import configparser
import os.path
import os.path
import subprocess

parser = argparse.ArgumentParser(description='Allows to create new configuration files when changing the topology of FEB/As or FEM/s, or merge two calibration directories.')
parser.add_argument("--config1", type=str, required=True, help="Configuration file")
parser.add_argument("--topology1", type=str, required=True, help="File describing FEB/A ID serial numbers connected into each FEB/D port")
parser.add_argument("--config2", type=str, required=False, help="")
parser.add_argument("--topology2", type=str, required=True, help="")
parser.add_argument("--output_directory", type=str, required=True, help="Output directory where the new configuration files will be created.")
parser.add_argument("--action", type=str, required=True, choices=["reorder", "merge"], help="Action to be performed: reorder (topology2 map contains the present configuration after reordering some FEB/As, and topology1 the configuration map of the obtained configuration) or merge (in the )") 

args = parser.parse_args()

topology1 = config.readTopologyMap(args.topology1)
topology2 = config.readTopologyMap(args.topology2)

configParser1 = configparser.RawConfigParser()
configParser1.read(args.config1)
cdir1 = os.path.dirname(args.config1)

if args.action == "merge" and args.config2 == None:
    print("Error: config2 needs to be defined for merging. Exiting...")
    exit(1)


if args.action == "merge":
    configParser2 = configparser.RawConfigParser()
    configParser2.read(args.config2)
    cdir2 = os.path.dirname(args.config2)



if not os.path.exists(args.output_directory):
    print("Error: Output directory doesn't exit. Exiting...")
    exit(1)


subprocess.call("cp %s %s " % (args.config1,args.output_directory), shell=True)

if args.action == "reorder":
    for portID2, slaveID2, chipID2 in topology2:
        prev_position = [pos for pos,serialID in topology1.items() if serialID == topology2[(portID2, slaveID2, chipID2)] ]
        if  prev_position == []:
            print("Error: FEB/A #%s not present in previous calibration topology. Impossible to reorder. Exiting..." %  topology2[(portID2, slaveID2, chipID2)][0])
            exit(1)

    
if args.action == "merge":
    for portID1, slaveID1, chipID1 in topology1:
            if (portID1, slaveID1, chipID1) in topology2: 
                print("Error: Repeated position (%d %d %d) in both topology lists. Impossible to merge. Exiting..." % (portID1, slaveID1, chipID1)) 
                exit(1)


for config_type in ["disc_calibration_table", "tdc_calibration_table", "qdc_calibration_table"]:
    fn1 = configParser1.get("main", config_type)
    fn1 = config.replace_variables(fn1, cdir1)
    fileName = os.path.basename(fn1)
    if not os.path.exists(fn1):
        print("Warning: %d not found, skipping this calibration file...")
    file1 = open(fn1)
    file3 = open(args.output_directory + "/" + fileName, "w")
    file3.write(file1.readline())
    if args.action == "reorder":
        for portID2, slaveID2, chipID2 in topology2:
            file1 = open(fn1)
            for l in file1:
                l = config.normalizeAndSplit(l)
                if l == ['']: continue
                portID1, slaveID1, chipID1 = [ int(v) for v in l[0:3] ]
                if topology1[(portID1, slaveID1, chipID1)] == topology2[(portID2, slaveID2, chipID2)]:
                    file2.write("%d\t%d\t%d\t" % (portID2, slaveID2, chipID2))
                    w = ""
                    for v in l[3:]:
                        w += v+"\t"
                    file2.write("%s\n" % w)  
    if args.action == "merge":
        fn2 = configParser2.get("main", config_type)
        fn2 = config.replace_variables(fn2, cdir2)
      
        for portID1, slaveID1, chipID1 in topology1:
            file1 = open(fn1)
            for l in file1:
                ln = config.normalizeAndSplit(l)
                if ln == ['']: continue
                portID, slaveID, chipID = [ int(v) for v in ln[0:3] ]
                if (portID, slaveID, chipID) == (portID1, slaveID1, chipID1):
                    file3.write(l)
        for portID2, slaveID2, chipID2 in topology2:
            file2 = open(fn2)
            for l in file2:
                ln = config.normalizeAndSplit(l)
                if ln == ['']: continue
                portID, slaveID, chipID = [ int(v) for v in ln[0:3] ]
                if (portID, slaveID, chipID) == (portID2, slaveID2, chipID2):
                    file3.write(l)
                   
        
       
