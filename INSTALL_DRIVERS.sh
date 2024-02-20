#!/bin/sh
find /lib/modules -name psdaq.ko -or -name psdaq.ko.xz | xargs rm
rm -rf /var/lib/dkms/psdaq
modprobe -r psdaq

D=$(dirname $0)
(
	cd $D
	cp src/udev/52-psdaq.rules /etc/udev/rules.d
	dkms add src/kernel/
)
dkms autoinstall
modprobe psdaq


