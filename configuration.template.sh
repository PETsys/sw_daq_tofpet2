#!/usr/bin/sh
CONFIG_FILE=config.ini
DATA_DIR=data

./acquire_threshold_calibration --config ${CONFIG_FILE} -o ${DATA_DIR}/disc_calibration
./process_threshold_calibration --config ${CONFIG_FILE} -i  ${DATA_DIR}/disc_calibration -o ${DATA_DIR}/disc_calibration.tsv 

./make_simple_disc_table --config ${CONFIG_FILE} --vth_t1 20 --vth_t2 20 --vth_e 15 > ${DATA_DIR}/disc_settings.tsv

./acquire_tdc_calibration --config ${CONFIG_FILE} -o ${DATA_DIR}/tdc_calibration
./acquire_qdc_calibration --config ${CONFIG_FILE} -o ${DATA_DIR}/qdc_calibration

./process_tdc_calibration --config ${CONFIG_FILE} -i ${DATA_DIR}/tdc_calibration -o ${DATA_DIR}/tdc_calibration 
./process_qdc_calibration --config ${CONFIG_FILE} -i ${DATA_DIR}/qdc_calibration -o ${DATA_DIR}/qdc_calibration 
