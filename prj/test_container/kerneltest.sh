#!/bin/bash

if [[ "$1" == "quiet" ]]; then

cat <<EOF

######################################

Test container execution
______     _
| ___ \   | |
| |_/ /__ | | ___ _ __   __ _ 
|  __/ _ \| |/ _ \ '_ \ / _' |
| | | (_) | |  __/ | | | (_| |
\_|  \___/|_|\___|_| |_|\__,_|

simply real-time containers

######################################

EOF
shift
fi

maxcpu=${1:-'7'}

runno=0 # changed_in_run

function update_kernel () {

	if [ "$runno" -eq 1 ]; then
		# Dyntick

		eval "sed '/LINUX_DEFAULT/s/splash.*\"/splash nohz_full=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 2 ]; then
		# backoff

		eval "sed '/LINUX_DEFAULT/s/nohz_full=1-$maxcpu/rcu_nocb=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 3 ]; then
		# isolation

		eval "sed '/LINUX_DEFAULT/s/rcu_nocb=1-$maxcpu/isocpu=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 4 ]; then
		# Dyntick + backoff

		eval "sed '/LINUX_DEFAULT/s/isocpu=1-$maxcpu/nohz_full=1-$maxcpu rcu_nocb=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 5 ]; then
		# isolation + backoff

		eval "sed '/LINUX_DEFAULT/s/nohz_full=1-$maxcpu/isocpu=1-$maxcpu/' /etc/default/grub"

	elif [ "$runno" -eq 6 ]; then
		# Dyntick + isolation

		eval "sed '/LINUX_DEFAULT/s/rcu_nocb=1-$maxcpu/splash nohz_full=1-$maxcpu rcu_nocb=1-$maxcpu/' /etc/default/grub"

	fi

	# update grub menu
	eval "update-grub2"
}

function update_runno () {
	eval "sed '/changed_in_run/s/'${runno}'/'$(($runno+1))'/' ./kerneltest.sh"
	runno=$((runno+1))
}

##################### DETERMINE CLI PARAMETERS ##########################

cmd=${1:-'cont'}

if [[ "$cmd" == "start" ]]; then

	runno=0 # reset 
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

