#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from petsys import daqd, config
from copy import deepcopy
import argparse
import math
import time
import os.path

parser = argparse.ArgumentParser(description='Acquire data for TDC calibration')
parser.add_argument("--config", type=str, required=True, help="Configuration file")
parser.add_argument("-o", type=str, dest="fileNamePrefix", required=True, help="Data filename (prefix)")
args = parser.parse_args()

systemConfig = config.ConfigFromFile(args.config, loadMask=0)

daqd = daqd.Connection()
daqd.initializeSystem()
systemConfig.loadToHardware(daqd, bias_enable=config.APPLY_BIAS_OFF, qdc_mode="tot")
daqd.openRawAcquisition(args.fileNamePrefix, calMode=True)

## Calibration parameters
## Phase range: 0 to 8 clocks in 129 steps
phaseMin = 0.0
phaseMax = 8.0
nBins = 165

if not os.path.exists(args.fileNamePrefix) or not os.path.samefile(args.fileNamePrefix, "/dev/null"):
	binParameterFile = open(args.fileNamePrefix + ".bins", "w")
	binParameterFile.write("%d\t%f\t%f\n" % (nBins, phaseMin, phaseMax))
	binParameterFile.close()


asicsConfig = daqd.getAsicsConfig()
# Prepare all channels for TDCA but disable actual triggering...
for ac in list(asicsConfig.values()):
	for cc in ac.channelConfig:
		## Set simplest trigger_mode_2_* setting
		cc.setValue("trigger_mode_2_t", 0b00)
		cc.setValue("trigger_mode_2_e", 0b000)
		cc.setValue("trigger_mode_2_q", 0b00)
		cc.setValue("trigger_mode_2_b", 0b000)

		# Disable channel from triggering.
		# Will selectively enable channels below
		cc.setValue("trigger_mode_1", 0b11)
	


simultaneousChannels = 64

# Clamp down simulatenous channels due to system limitations
# 126/16 -- GbE interface: 126 events/frame with FEB/D, 16 ASICs per FEB/D
# 1024/80 -- ASIC TX: 1024 clock/frame (x1 SDR), 80 bits per event
simultaneousChannels = min([simultaneousChannels, 126/16, 1024/80])
simultaneousChannels = 1

channelStep = int(math.ceil(64.0/simultaneousChannels))

for firstChannel in range(0, channelStep):
	activeChannels = [ channel for channel in range(firstChannel, 64, channelStep) ]
	activeChannels_string = (", ").join([ "%d" % channel for channel in activeChannels ])
	# Enable triggering for active channels
	cfg = deepcopy(asicsConfig)
	for ac in list(cfg.values()):
		for channel in activeChannels:
			ac.channelConfig[channel].setValue("trigger_mode_1", 0b01)
	daqd.setAsicsConfig(cfg)
        
	for i in range(0, nBins):
		t_start = time.time()
		binSize = (phaseMax - phaseMin) / nBins
		finePhase = phaseMin + (i+0.5) * binSize
		daqd.set_test_pulse_febds(100, 1024, finePhase, False)
		daqd.acquire(0.02, finePhase, 0)
		t_finish = time.time()
		print("Channel(s): %s Phase: %4.3f clk in %3.2f seconds " % (activeChannels_string, finePhase, t_finish - t_start))


systemConfig.loadToHardware(daqd, bias_enable=config.APPLY_BIAS_OFF)
