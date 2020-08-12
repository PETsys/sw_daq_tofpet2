#!/bin/sh
cp src/udev/52-psdaq.rules /etc/udev/rules.d
dkms add src/kernel/
dkms install psdaq/1.1
modprobe psdaq

