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
osPrettyName=`cat /etc/os-release | grep PRETTY_NAME | sed 's/.*="\(.*\)"/\1/'`;
if [[ $(echo $osPrettyName | grep CentOS) ]]; then
	centosVersion=`cat /etc/centos-release | sed 's/[^0-9.]*\([0-9.]*\).*/\1/'`;
fi;
osKernelVer=`uname -r`;

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
if [[ $(echo $osPrettyName | grep buntu) ]]; then
	hostOS="ubuntu";
	echo "INFO: Running on Ubuntu";
elif [[ $(echo $osPrettyName | grep CentOS) ]] && [[ $(echo $centosVersion | grep 8) ]]; then
	hostOS="centos8";
	echo "INFO: Running on CentOS version $centosVersion";
elif [[ $(echo $osPrettyName | grep CentOS) ]] && [[ $(echo $centosVersion | grep 7) ]]; then
	hostOS="centos7";
	echo "INFO: Running on CentOS version $centosVersion";
elif [[ $(echo $osPrettyName | grep "Red Hat") ]]; then
	hostOS="rhel";
	echo "INFO: Running on Red Hat";
else
	echo "ERROR: Cannot determine host operating system!"
	echo "WARNING: This script is only supported on Ubuntu, CentOS 7-8, and RHEL 8.5 Linux distribution!"
	exit 1;	
fi;

#update shell on UBUNTU only
#place this portion near the start of the script so that it runs before sudo expires if package installation takes a long time
if [ $hostOS == "ubuntu" ]; then
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

#check if dpkg repo is set up for 32-bit packages on UBUNTU only
if [ $hostOS == "ubuntu" ]; then
	echo -n "NOTE: Check for x86 package access..."
	foreignArchitecture=`dpkg --print-foreign-architectures | grep i386`;
	if [ $foreignArchitecture == "i386" ]; then 
		echo "FOUND!"; 
	else 
		echo "NOT FOUND! Now adding i386 foreign archiecture to dpkg";
		sudo dpkg --add-architexture i386; 
	fi;
fi;


#declare the package lists
#may need to add libblas-dev liblapack-dev for ubuntu 20.04
debPackages=(cmake gcc g++ libboost-dev libboost-python-dev libboost-regex-dev libiniparser-dev dpkg-dev cmake binutils libx11-dev libxpm-dev libxft-dev libxext-dev python3 libssl-dev python3-bitarray python3-matplotlib python3-pandas stow dkms xterm git);
centos8Packages=(gcc gcc-c++ root root-gui-fitpanel root-spectrum root-spectrum-painter root-minuit2 root-physics root-multiproc python3 python3-devel python3-pip python3-root python3-pandas python3-matplotlib-gtk3 python3-devel boost-devel boost-python3-devel kernel kernel-devel cmake iniparser-devel xterm dkms);
centos7Packages=(gcc gcc-c++ root root-gui-fitpanel root-spectrum root-spectrum-painter root-minuit2 root-physics root-multiproc python3 python3-devel python3-pip python3-root python36-cairo python36-gobject.x86_64 boost-devel boost-python36-devel kernel kernel-devel cmake3 iniparser-devel xterm dkms cairo-devel redhat-lsb libjpeg-turbo-devel);

if [ $hostOS == "ubuntu" ]; then
	packageList=(${debPackages[@]});
elif [ $hostOS == "rhel" ]; then
	packageList=(${centos8Packages[@]});
elif [ $hostOS == "centos8" ]; then
	packageList=(${centos8Packages[@]});
elif [ $hostOS == "centos7" ]; then
	packageList=(${centos7Packages[@]});
fi;

echo "INFO: Selected packages for detected distribution:"
echo "${packageList[*]}"

#start building the package installation command line
if [ $hostOS == "ubuntu" ]; then
	packageCommand="apt";
elif [ $hostOS == "centos7" ]; then
	packageCommand="yum";	
elif [ $hostOS == "centos8" ]; then
	packageCommand="dnf";	
elif [ $hostOS == "rhel" ]; then
	packageCommand="dnf";
fi;


#make sure the package lists are up-to-date
echo "INFO: Enabling repositories and Updating the package lists...";
if [ $hostOS == "ubuntu" ]; then
	sudo $packageCommand update;
elif [ $hostOS == "centos7" ]; then
	sudo $packageCommand install epel-release;
	sudo $packageCommand makecache;
elif [ $hostOS == "centos8" ]; then
	sudo $packageCommand install epel-release;
	sudo $packageCommand config-manager --set-enabled powertools;
	sudo $packageCommand makecache;
elif [ $hostOS == "rhel" ]; then
	sudo $packageCommand install epel-release;
	sudo $packageCommand config-manager --set-enabled powertools;
	sudo $packageCommand makecache;
fi;


#install the packages
for package in "${packageList[@]}"; do
	echo "######";
	echo "NOTE: Processing package: "$package;
	echo -n "NOTE: Checking to see if package is already installed..."
	installPackage=0;
	
	if [ $hostOS == "ubuntu" ]; then
		if [[ $($packageCommand -qq list $package | grep installed) ]]; then
			echo "INSTALLED! Skipping."
		else
			echo "NOT INSTALLED!";
			echo "Starting installation of package $package";
			packageInstallCommand="$packageCommand install";
			if [ $debug -eq 1 ]; then 
				packageInstallCommand+=" --dry-run";
			else
				packageInstallCommand+=" -y";
			fi;
			sudo $packageInstallCommand $package;
		fi;
	elif [ $hostOS == "rhel" ] || [ $hostOS == "centos7" ] || [ $hostOS == "centos8" ]; then
		if [[ $($packageCommand list installed | grep $package) ]]; then
			echo "INSTALLED! Skipping."
		else
			echo "NOT INSTALLED!";
			echo "Starting installation of package $package";
			packageInstallCommand="$packageCommand install";
			if [ $debug -eq 1 ]; then 
				packageInstallCommand+=" --assumeno";
			else
				packageInstallCommand+=" -y";
			fi;
			sudo $packageInstallCommand $package;
		fi;
		
	fi;	
	echo "Package installation complete for package $package";
	echo -e "######\n";

done;


#install some python libraries using pip3 commands
if   [ $hostOS == "centos7" ] && [ $debug -eq 0 ]; then
	echo "INFO: Installing pandas bitarray matplotlib pycairo using pip3 commands";
	pip3 install pandas bitarray matplotlib pycairo;
elif ([ $hostOS == "rhel" ] || [ $hostOS == "centos8" ]) && [ $debug -eq 0 ]; then
	echo "INFO: Installing bitarray using pip3 commands";
	pip3 install bitarray;
elif [ $debug -eq 1 ]; then
	echo "DEBUG: Skipping pip3 commands because debug mode is on.";
fi;


#install ROOT from source -- for UBUNTU only
installROOT=1;
rootVersion=`root-config --version | sed 's/\(^[0-9]\).*/\1/'`;
if [ $rootVersion -ge 6 ] && [ $hostOS == "ubuntu" ]; then
	echo "WARNING: Found ROOT $rootVersion installation. Do you wish to install ROOT again from source?"
 	select yn in "Yes" "No"; do
		case $yn in
			Yes ) installROOT=1; break;;
			No ) installROOT=0; break;;
		esac
	done
fi

if [ $hostOS == "ubuntu" ] && [ $debug -eq 0 ] && [ $installROOT -eq 1 ]; then
	echo "INFO: Building ROOT from source";
	cd /tmp;
	wget https://root.cern/download/root_v6.28.02.source.tar.gz;
	tar xvzf root_v6.28.02.source.tar.gz;
	cd root-6.28.02/;
	mkdir build;
	cd build;
	cmake -DCMAKE_INSTALL_PREFIX=/usr/local/stow/root-v6.28.02 -DCMAKE_INSTALL_MANDIR=share/man ..;
	make -j9;
	sudo make install;
	cd /usr/local/stow;
	sudo stow root-v6.28.02;
	sudo ldconfig;
elif [ $debug -eq 1 ]; then
	echo "DEBUG: Skipping ROOT installation commands because debug mode is on.";
fi;


#install DAQ card drivers
if [ $debug -eq 1 ]; then 
	echo "DEBUG: Would now install DAQ card drivers.";
else
	cd "$originalDir";
	sudo sh INSTALL_DRIVERS.sh;
fi;

echo "INFO: Environment setup complete!";
echo "INFO: You may now build the TOFPET 2 data acquisition software from Source. Check the Software User Guide for detailed instructions."

#Generic Software build instructions:
#mkdir build
#cd build
#cmake -DCMAKE_BUILD_TYPE=Release ..