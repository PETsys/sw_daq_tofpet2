#!/bin/sh
cp src/udev/52-psdaq.rules /etc/udev/rules.d
dkms add src/kernel/
dkms autoinstall
modprobe psdaq

