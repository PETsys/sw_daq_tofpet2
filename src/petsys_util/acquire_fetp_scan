#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, config
from copy import deepcopy
import argparse

parser = argparse.ArgumentParser(description='Acquire data for TDC calibration')
parser.add_argument("--config", type=str, required=True, help="Configuration file")
parser.add_argument("-o", type=str, dest="fileNamePrefix", required=True, help="Data filename (prefix)")
parser.add_argument("--time", type=float, required=True, help="Acquisition time (in seconds)")
parser.add_argument("--mode", type=str, required=True, choices=["tot", "qdc", "mixed"], help="Acquisition mode (tot, qdc or mixed)")
parser.add_argument("--enable-hw-trigger", dest="hwTrigger", action="store_true", help="Enable the hardware coincidence filter")
args = parser.parse_args()

mask = config.LOAD_ALL
if args.mode != "mixed":
        mask ^= config.LOAD_QDCMODE_MAP
systemConfig = config.ConfigFromFile(args.config, loadMask=mask)

daqd = daqd.Connection()
daqd.initializeSystem()
systemConfig.loadToHardware(daqd, bias_enable=config.APPLY_BIAS_ON, hw_trigger_enable=args.hwTrigger, qdc_mode = args.mode)

asicsConfig = daqd.getAsicsConfig()
for ac in list(asicsConfig.values()):
	gc = ac.globalConfig
	gc.setValue("fetp_enable", 0b1)			# Enable FETP for ASIC
	for cc in ac.channelConfig:
		cc.setValue("trigger_mode_1", 0b11)	# Disable channel from triggering
		cc.setValue("fe_tp_en", 0b01)		# Disable FETP for channel but enable channel's capacitance
			
daqd.setAsicsConfig(asicsConfig)

daqd.set_test_pulse_febd(200, 400*1024, 0, False)
daqd.openRawAcquisition(args.fileNamePrefix)

for channel in range(0,64):
	for step1 in range(16,32):
		asicsConfig2 = deepcopy(asicsConfig)
		for ac in list(asicsConfig2.values()):
			gc = ac.globalConfig
			gc.setValue("v_cal_ref_ig", step1)	# Set FETP amplitude
			cc = ac.channelConfig[channel]
			cc.setValue("fe_tp_en", 0b11)		# Enable FETP for channel
			cc.setValue("trigger_mode_1", 0b00)	# Set channel to normal trigger mode
		
		daqd.setAsicsConfig(asicsConfig2)
		print("Acquiring channel %2d DAC %2d" % (channel, step1))
		daqd.acquire(args.time, step1, 0);

systemConfig.loadToHardware(daqd, bias_enable=config.APPLY_BIAS_OFF)
daqd.setTestPulseNone()

