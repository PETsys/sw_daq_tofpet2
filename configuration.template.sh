#!/usr/bin/sh
DATA_DIR=$1
CONFIG_FILE=${DATA_DIR}/config.ini

./acquire_threshold_calibration --config ${CONFIG_FILE} -o ${DATA_DIR}/disc_calibration
./process_threshold_calibration --config ${CONFIG_FILE} -i ${DATA_DIR}/disc_calibration -o ${DATA_DIR}/disc_calibration.tsv --root-file ${DATA_DIR}/disc_calibration.root
 
./make_simple_disc_settings_table --config ${CONFIG_FILE} --vth_t1 20 --vth_t2 20 --vth_e 15 -o ${DATA_DIR}/disc_settings.tsv

./acquire_tdc_calibration --config ${CONFIG_FILE} -o ${DATA_DIR}/tdc_calibration
./acquire_qdc_calibration --config ${CONFIG_FILE} -o ${DATA_DIR}/qdc_calibration

./process_tdc_calibration --config ${CONFIG_FILE} -i ${DATA_DIR}/tdc_calibration -o ${DATA_DIR}/tdc_calibration 
./process_qdc_calibration --config ${CONFIG_FILE} -i ${DATA_DIR}/qdc_calibration -o ${DATA_DIR}/qdc_calibration 
