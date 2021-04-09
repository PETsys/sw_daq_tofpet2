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
python36-cairo \
python36-gobject.x86_64 \
boost-devel \
boost-python36-devel \
kernel kernel-devel \
cmake3 \
iniparser-devel \
xterm \
dkms

pip3 install pandas
pip3 install bitarray
pip3 install matplotlib
yum -y install cairo-devel
pip3 install pycairo

sh INSTALL_DRIVERS.sh
