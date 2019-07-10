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

maxcpu=${2:-$(($(nproc --all)-1))}

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

		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu\"/' /etc/default/grub"

	elif [ "$runno" -eq 2 ]; then
		# backoff

		eval "sed -i '/LINUX_DEFAULT/s/nohz_full=1-$maxcpu/rcu_nocb=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 3 ]; then
		# isolation

		eval "sed -i '/LINUX_DEFAULT/s/rcu_nocb=1-$maxcpu/isocpu=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 4 ]; then
		# Dyntick + backoff

		eval "sed -i '/LINUX_DEFAULT/s/isocpu=1-$maxcpu/nohz_full=1-$maxcpu rcu_nocb=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 5 ]; then
		# isolation + backoff

		eval "sed -i '/LINUX_DEFAULT/s/nohz_full=1-$maxcpu/isocpu=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 6 ]; then
		# Dyntick + isolation

		eval "sed -i '/LINUX_DEFAULT/s/rcu_nocb=1-$maxcpu/nohz_full=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 7 ]; then
		# reset

		eval "sed -i '/LINUX_DEFAULT/s/splash.*\"/splash\"/' /etc/default/grub"

		# remove start script
		crontab -u root -r
		rm $0
	fi

	# update grub menu
	res=$(update-grub2)
	if [[ ! "$res" -eq 0 ]]; then
		exit 1
	fi
}

function update_runno () {
	eval "sed -i '/maxcpu=/s/=.*/='${maxcpu}'/' ./kernelrun.sh"
	eval "sed -i '/changed_in_run/s/'${runno}'/'$(($runno+1))'/' ./kernelrun.sh"
	runno=$((runno+1))
}

##################### DETERMINE CLI PARAMETERS ##########################

if [[ "$cmd" == "start" ]]; then

	runno=0 # reset 
	cp $0 ./kernelrun.sh

	# add to startup 
	eval "echo '@reboot cd "$PWD" && ./kernelrun.sh' | sudo crontab -u root -"
else 
	echo "...waiting"
	sleep 60
	echo "...run test"
	#execute test
	./test.sh quiet 1
	#move tests to their directory
	mkdir -p log/test${runno}
	mv log/?-* log/test${runno}/
fi

update_runno

update_kernel

#reboot system
reboot

