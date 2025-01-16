#!/bin/bash

#PETSys Software dependencies setup script
#based on PetaLinux environment setup script
#original version by Tony McDowell (tmcdowe@xilinx.com)
#updated version by Sandeep Gundlupet Raju (sandeep.gundlupet-raju@xilinx.com)
#repurposed version by Tiago Coutinho (tcoutinho@petsyselectronics.com)

#enable  debug=1 -- this disables actual changes on the host machine using dry-run options.
#disable debug=0 -- to proceed with installation.
debug=0;

#print the debug message header
if [ $debug -eq 1 ]; then echo "***DEBUG MODE ON!***"; fi; 
echo " "

#get current directory
originalDir=$(pwd)

#get OS pretty name
osPrettyName=$( source /etc/os-release ; echo $ID);
osVersion=$( source /etc/os-release ; echo ${VERSION_ID});
osKernelVer=`uname -r`;

# Treat RHEL clones as RHEL
if [[ $osPrettyName == "centos" || $osPrettyName == "almalinux" ]]; then
	osPrettyName="rhel"
fi

if [[ $osPrettyName == "rhel" ]]; then
	# Cut minor OS version of present
	osVersion=${osVersion%.*}
fi

echo "***************************************************************";
echo "PETSys Software Setup Tool";
echo "Running on $osPrettyName ($osKernelVer)";
echo "***************************************************************";
echo " ";


#check for su privileges
echo "INFO: Checking for superuser..."
#get the actual user
if [ $SUDO_USER ]; then actualUser=$SUDO_USER; else actualUser=`whoami`; fi
#get the effective user
currentUser=`whoami`
if [ $currentUser != "root" ]; then echo "ERROR: Running as "$actualUser". Please re-run this script as sudo"; exit 1; else echo "INFO: SUCCESS! Running as "$actualUser""; fi;


#determine the host OS from the pretty_name
if [[ $osPrettyName == "ubuntu" && $osVersion == "20.04" ]]; then
 	true
elif [[ $osPrettyName == "ubuntu" && $osVersion == "22.04" ]]; then
 	true
elif [[ $osPrettyName == "rhel" && $osVersion == "7" ]]; then
 	true
elif [[ $osPrettyName == "rhel" && $osVersion == "8" ]]; then
 	true
elif [[ $osPrettyName == "rhel" && $osVersion == "9" ]]; then
 	true
else
	echo "ERROR: Cannot determine host operating system!"
	echo "WARNING: This script is only supported on Ubuntu 20.04 and 22.04 and RHEL/CentOS 7-9 Linux distribution!"
	exit 1;
fi;

# Update shell on UBUNTU only
# Place this portion near the start of the script so that it runs before sudo expires if package installation takes a long time
if [ $osPrettyName == "ubuntu" ]; then
	echo -n "NOTE: Checking for DASH shell as default (Ubuntu only)...";
	if echo `echo $0` | grep 'dash'; then
		echo "FOUND!";
		echo -n "NOTE: Changing default shell to from DASH to BASH...";
		export DEBIAN_FRONTEND=noninteractive;
		export DEBCONF_NONINTERACTIVE_SEEN=true;

		echo "dash dash/sh boolean false" | debconf-set-selections;
		dpkg-reconfigure dash;

		unset DEBIAN_FRONTEND;
		unset DEBCONF_NONINTERACTIVE_SEEN;
		echo "DONE!";
		echo "INFO: You must log out of this shell and back in for change to take effect";
	else
		echo "NOT FOUND!";
	fi;
fi;

# Make sure the package lists are up-to-date
# and install extra repositories
echo "INFO: Enabling repositories and Updating the package lists...";
if [[ $osPrettyName == "ubuntu" ]]; then
	sudo apt update;
	apt -y install cmake gcc g++ libboost-dev libboost-python-dev libboost-regex-dev libiniparser-dev dpkg-dev cmake binutils libx11-dev libxpm-dev libxft-dev libxext-dev python3 libssl-dev python3-bitarray python3-matplotlib python3-pandas stow dkms xterm git libaio1 libaio-dev;

elif [[ $osPrettyName == "rhel" && $osVersion == 7 ]]; then
	sudo yum -y install epel-release;
	sudo yum -y makecache;
	yum -y install gcc gcc-c++ root root-gui-fitpanel root-spectrum root-spectrum-painter root-minuit2 root-physics root-multiproc python3 python3-devel python3-pip python3-root python36-cairo python36-gobject boost-devel boost-python36-devel kernel kernel-devel cmake3 iniparser-devel xterm dkms cairo-devel redhat-lsb libjpeg-turbo-devel libaio libaio-devel;
	echo "INFO: Installing bitarray using pip3 commands";
	pip3 install bitarray;

elif [[ $osPrettyName == "rhel" && $osVersion == 8 ]]; then
	sudo dnf -y install epel-release;
	sudo dnf -y config-manager --set-enabled powertools;
	sudo dnf -y makecache;
	dnf -y install gcc gcc-c++ root root-gui-fitpanel root-spectrum root-spectrum-painter root-minuit2 root-physics root-multiproc python3 python3-devel python3-pip python3-root python3-pandas python3-matplotlib-gtk3 python3-devel boost-devel boost-python3-devel kernel kernel-devel cmake iniparser-devel xterm dkms libaio libaio-devel redhat-lsb;
	echo "INFO: Installing bitarray using pip3 commands";
	pip3 install bitarray;


elif [[ $osPrettyName == "rhel" && $osVersion == 9 ]]; then
	sudo dnf -y install epel-release;
	sudo dnf -y config-manager --set-enabled crb
	sudo dnf -y makecache;
	dnf -y install gcc gcc-c++ root root-gui-fitpanel root-spectrum root-spectrum-painter root-minuit2 root-physics root-multiproc python3 python3-devel python3-pip python3-root python3-matplotlib-gtk3 python3-devel boost-devel boost-python3 kernel kernel-devel cmake iniparser-devel xterm dkms libaio libaio-devel lsb_release;
        echo "INFO: Installing pandas bitarray using pip3 commands";
        pip3 install pandas bitarray;
fi;

# Install ROOT from source -- for UBUNTU only
installROOT=1;
rootVersion=`root-config --version | sed 's/\(^[0-9]\).*/\1/'`;
if [ $rootVersion -ge 6 ] && [ $osPrettyName == "ubuntu" ]; then
	echo "WARNING: Found ROOT $rootVersion installation. Do you wish to install ROOT again from source?"
 	select yn in "Yes" "No"; do
		case $yn in
			Yes ) installROOT=1; break;;
			No ) installROOT=0; break;;
		esac
	done
fi

if [ $osPrettyName == "ubuntu" ] && [ $debug -eq 0 ] && [ $installROOT -eq 1 ]; then
	echo "INFO: Building ROOT from source";
	cd /tmp;
	wget https://root.cern/download/root_v6.28.02.source.tar.gz;
	tar xvzf root_v6.28.02.source.tar.gz;
	cd root-6.28.02/;
	mkdir build;
	cd build;
	cmake -DCMAKE_INSTALL_PREFIX=/usr/local/stow/root-v6.28.02 -DCMAKE_INSTALL_MANDIR=share/man ..;
	# Two step building as parallel building sometimes failes due to lack of RAM
	make -k -j$(nproc)
	make
	sudo make install;
	cd /usr/local/stow;
	sudo stow root-v6.28.02;
	sudo ldconfig;
elif [ $debug -eq 1 ]; then
	echo "DEBUG: Skipping ROOT installation commands because debug mode is on.";
fi;


# Install DAQ card drivers
if [ $debug -eq 1 ]; then 
	echo "DEBUG: Would now install DAQ card drivers.";
else
	cd "$originalDir";
	sudo sh INSTALL_DRIVERS.sh;
fi;

echo "INFO: Environment setup complete!";
echo "INFO: You may now build the TOFPET 2 data acquisition software from Source. Check the Software User Guide for detailed instructions."

# Generic Software build instructions:
# mkdir build
# cd build
# cmake -DCMAKE_BUILD_TYPE=Release ..
