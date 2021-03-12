#!/bin/sh

dnf -y update
dnf -y install epel-release

dnf config-manager --set-enabled powertools

dnf -y  install \
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
python3-pandas \
python3-matplotlib-gtk3 \
python3-devel \
boost-devel \
boost-python3-devel \
kernel kernel-devel \
cmake \
iniparser-devel \
xterm \
dkms

pip3 install bitarray

sh INSTALL_DRIVERS.sh
