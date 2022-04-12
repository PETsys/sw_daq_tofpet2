#!/bin/sh
D=$(dirname $0)
(
	cd $D
	cp src/udev/52-psdaq.rules /etc/udev/rules.d
	dkms add src/kernel/
)
dkms autoinstall
modprobe psdaq


