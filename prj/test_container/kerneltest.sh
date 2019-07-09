#!/bin/bash

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

maxcpu=${1:-'7'}

runno=1 # changed_in_run

function update_kernel () {

	if [ "$runno" -eq 1 ]; then

		eval sed '/LINUX_DEFAULT/s/splash/splash nohz_full=1-$maxcpu/' /etc/default/grub

	elif [ "$runno" -eq 2 ]; then

		eval sed '/LINUX_DEFAULT/s/splash nohz_full=1-$maxcpu/splash rcu_nocb=1-$maxcpu/' /etc/default/grub

	fi

	eval update-grub2
}

function update_runno () {
	eval sed '/changed_in_run/s/$runno/runno+1/' ./kernetest.sh
	runno++
}

##################### DETERMINE CLI PARAMETERS ##########################

cmd=${1:-'cont'}

if [[ "$cmd" == "start" ]]; then

	runno=1 # reset 
fi

#execute test
#./test.sh
#move tests to their directory

###

update_runno

update_kernel

#reboot system
#eval reboot


