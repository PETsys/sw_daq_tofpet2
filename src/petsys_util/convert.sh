#!/bin/bash

RUN=$1
MODE=$2
REFCHANNEL=$3

if [ $MODE == "s" ]
then
    ./convert_raw_to_singles --config ../config/config.ini -i /data/TOFPET2/raw/run$RUN -o /data/TOFPET2/reco/run"$RUN"_s.root --writeRoot
fi


if [ $MODE == "c" ]
then
    ./convert_raw_to_coincidence --config ../config/config.ini -i /data/TOFPET2/raw/run$RUN -o /data/TOFPET2/reco/run"$RUN"_c.root --writeRoot
fi


if [ $MODE == "t" ]
then
    ./convert_raw_to_trigger --config ../config/config.ini -i /data/TOFPET2/raw/run$RUN -o /data/TOFPET2/reco/run"$RUN"_t.root --writeRoot --coincidence --refChannel "$REFCHANNEL"
fi
