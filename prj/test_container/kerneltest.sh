#!/bin/bash

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

cmd=${1:-'cont'}

if [ $# -ge 3 ]; then

cat <<EOF
Not enough arguments supplied!
Usag: $0 start [number]
 or   $0 cont

Defaults are:
number = *		all cpus used
EOF
        exit 1
fi

maxcpu=$((${2:-$(($(nproc --all)))}-1))
std=${3:-'4.19.50-rt22'}
fult=${4:-'4.19.50-rt22loji'}

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
		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux $fult\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 2 ]; then
		# Dyntick + backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux $fult\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu rcu_nocb=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 3 ]; then
		# Dyntick + isolation

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux $fult\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu isolcpu=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 4 ]; then
		# Dyntick + isolation + backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux $fult\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu isolcpu=1-$maxcpu rcu_nocb=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 5 ]; then
		# backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux $std\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash rcu_nocb=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 6 ]; then
		# isolation

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux $std\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash isolcpu=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 7 ]; then
		# isolation + backoff

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux $std\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash isolcpu=1-$maxcpu rcu_nocb=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 8 ]; then
		# reset

		eval "sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux $std\"/' /etc/default/grub"
		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash\"/' /etc/default/grub"

	elif [ "$runno" -eq 9 ]; then

		# remove start script
		crontab -u root -r
		rm $0
	fi

	# update grub menu
	eval /usr/sbin/update-grub
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

	eval "rm -r log/test?/"

	# add to startup 
	eval "echo '@reboot cd "$PWD" && ./kernelrun.sh' | sudo crontab -u root -"
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
eval /sbin/reboot

