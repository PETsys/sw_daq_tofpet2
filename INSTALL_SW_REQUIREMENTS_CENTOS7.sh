#!/bin/sh
yum -y install epel-release

yum -y  install \
gcc \
gcc-c++ \
root \
root-gui-fitpanel \
root-spectrum \
root-spectrum-painter \
root-minuit2 \
root-physics \
root-multiproc \
python3 \
python3-devel \
python3-pip \
python3-root \
python3-devel \
boost-devel \
boost-python3-devel \
kernel kernel-devel \
cmake \
iniparser-devel \
xterm \
dkms

pip3 install pandas
pip3 install bitarray

sh INSTALL_DRIVERS.sh
