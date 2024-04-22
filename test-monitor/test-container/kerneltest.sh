#!/bin/bash
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

if [[ ! "$1" == "quiet" ]]; then

cat <<EOF

######################################

Kernel test execution
______     _
| ___ \   | |
| |_/ /__ | | ___ _ __   __ _ 
|  __/ _ \| |/ _ \ '_ \ / _' |
| | | (_) | |  __/ | | | (_| |
\_|  \___/|_|\___|_| |_|\__,_|

simply real-time containers

######################################

EOF
else 
	shift
fi

function print_help {

	cat <<-EOF
	Usage: $0 start [number] [kernel-ver1] [kernel-ver2]
	 or    $0 cont	[number] [kernel-ver1] [kernel-ver2]

	for start or continuation of a kernel test.
	
	Defaults are:
	number = *                      maximum number of cpu to use, default all cpus used
	kernel-ver1 = 4.19.50-rt24      The specified version is base version
	kernel-ver2 = 4.19.50-rt24loji  The specified version is compare version
	EOF
	
}

cmd=${1:-'help'}

if [ $# -gt 4 ]; then
	echo "Too many arguments supplied!!"
	print_help
	exit 1
fi
if [ "$cmd" = "help" ]; then
	print_help
	exit 0
fi

maxcpu=${2:-$(( $(nproc --all) - 1 ))}
ver1=${3:-'4.19.50-rt22'}
ver2=${4:-'4.19.50-rt22loji'}

if [[ ! "$cmd" == "start" ]]; then

	#shut down cores not used
	for ((i=$(($maxcpu+1));i<$(nproc --all);i++))
	do 
		echo "Setting CPU"$i" offline..."
		$(echo 0 > /sys/devices/system/cpu/cpu$i/online)
		sleep 1
	done

	# disable smt
	$(echo off > /sys/devices/system/cpu/smt/control)
fi

runno=0 # changed_in_run

function update_kernel () {

	if [ "$runno" -eq 1 ]; then
		# Dyntick
		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver2}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 2 ]; then
		# Dyntick + backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver2}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu rcu_nocbs=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 3 ]; then
		# Dyntick + isolation

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver2}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu isolcpus=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 4 ]; then
		# Dyntick + isolation + backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver2}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu isolcpus=1-$maxcpu rcu_nocbs=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 5 ]; then
		# backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver1}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash rcu_nocbs=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 6 ]; then
		# isolation

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver1}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash isolcpus=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 7 ]; then
		# isolation + backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver1}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash isolcpus=1-$maxcpu rcu_nocbs=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 8 ]; then
		# reset

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver1}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash\"/' /etc/default/grub"


	elif [ "$runno" -eq 9 ]; then
		# fixtick 

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver1}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz=off\"/' /etc/default/grub"

	elif [ "$runno" -eq 10 ]; then
		# fixtick + backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver1}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz=off rcu_nocbs=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 11 ]; then
		# fixtick + isolation

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver1}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz=off isolcpus=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 12 ]; then
		# fixtick + isolation + backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver1}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz=off isolcpus=1-$maxcpu rcu_nocbs=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 13 ]; then

		# TEMP
		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver1}\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash\"/' /etc/default/grub"

		# remove start script
		crontab -u root -r
		rm $0
	fi

	# update grub menu
	eval update-grub
	if [ $? -ne 0 ]; then

		echo "error update grub" >> result.txt
		exit 1

	fi
}

function update_runno () {
	eval "sed -i '0,/maxcpu=/{s/maxcpu=.*/maxcpu='${maxcpu}'/}' ./kernelrun.sh"
	eval "sed -i '0,/changed_in_run/{s/runno='${runno}'/runno='$(($runno+1))'/}' ./kernelrun.sh"
	runno=$((runno+1))
}

##################### DETERMINE CLI PARAMETERS ##########################

if [[ "$cmd" == "start" ]]; then

	runno=0 # reset 
	cp $0 ./kernelrun.sh

	eval "rm -r log/test*/"

	# add to startup 
	eval "echo '@reboot cd "$PWD" && ./kernelrun.sh cont ${maxcpu} ${ver1} ${ver2} ' | sudo crontab -u root -"
else 
	echo "...waiting"
	sleep 60
	echo "... run test"
	#execute test
	./test.sh quiet 
	#move tests to their directory
	mkdir -p log/test${runno}
	mv log/?-* log/test${runno}/
fi

update_runno

update_kernel

#reboot system
eval reboot

