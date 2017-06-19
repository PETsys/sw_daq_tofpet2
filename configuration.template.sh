#!/usr/bin/sh

mkdir -p data/cal

./acquire_tdc_calibration --config config.ini -o data/cal/tdca
./process_tdc_calibration -i data/cal/tdca -o data/cal/tdca

./acquire_qdc_calibration --config config.ini -o data/cal/qdca
./process_qdc_calibration --config config.ini -i data/cal/qdca -o data/cal/qdca 

./acquire_threshold_calibration --config config.ini -o data/cal/disc 
./process_threshold_calibration --config config.ini -i data/cal/disc -o data/cal/disc.tsv 
