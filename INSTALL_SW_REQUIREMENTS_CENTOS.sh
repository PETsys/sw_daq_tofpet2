#!/bin/sh
yum -y install epel-release

yum -y install gcc \
gcc-c++ \
boost-devel \
root \
root-gui-fitpanel \
root-spectrum \
root-spectrum-painter \
root-minuit2 \
root-physics \
root-multiproc \
python \
python-devel \
root-python \
python-pip \
kernel kernel-devel \
python-devel \
cmake \
iniparser-devel

pip install --upgrade pip
pip install bitarray
pip install crcmod
pip install importlib
pip install pyserial
pip install posix_ipc
pip install numpy
pip install argparse


