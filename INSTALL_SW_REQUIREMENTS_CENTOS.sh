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
iniparser-devel \
python2-bitarray.x86_64 \
python3-root \
xterm \
python-pandas \
dkms

sh INSTALL_DRIVERS.sh
