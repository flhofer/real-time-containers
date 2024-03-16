#!/bin/sh

cat <<EOF

######################################

System configurator
______     _                  
| ___ \   | |                 
| |_/ /__ | | ___ _ __   __ _ 
|  __/ _ \| |/ _ \ '_ \ / _' |
| | | (_) | |  __/ | | | (_| |
\_|  \___/|_|\___|_| |_|\__,_|                              
                              
simply real-time containers

######################################

EOF

echo "*** WIP *** -- Not completed yet"
exit 1

function irq_affinity() {
	echo "Setting IRQ affinity to "$1
	for file in /proc/irq/*/; do
	   echo $1 > $file/smp_affinity;
	done
	echo $1 > /proc/irq/default_smp_affinity
	}

function load_balancer() {
	echo "Setting load balancer to "$1
	echo $1 > /sys/fs/cgroup/cpuset/cpuset.sched_load_balance
	echo $1 > /sys/fs/cgroup/cpuset/user/cpuset.sched_load_balance
	echo 1 > /sys/fs/cgroup/cpuset/system/cpuset.sched_load_balance
	}

function restartCores () {
	# verify if core 0 is disableable
	if [ -e "/sys/devices/system/cpu/cpu0/online" ]; then
		echo "Setting CPU0 offline..."
		# if yes, disable and reenable it immediately, otherwise no cpu left XD
		$(echo 0 > /sys/devices/system/cpu/cpu0/online)
		sleep 1
		echo "Putting CPU0 back online..."
		$(echo 1 > /sys/devices/system/cpu/cpu0/online)
	fi

	#check if numa is a number or a list
	re='^[0-9]+$';
	if [[ $numanr -eq 1 ]]; then
		#simple number of cores

		#shut down cores
		for ((i=1;i<$prcs;i++))
		do 
			echo "Setting CPU"$i" offline..."
			$(echo 0 > /sys/devices/system/cpu/cpu$i/online)
			sleep 1
		done

		sleep 1

		# put them back online
		for ((i=1;i<$prcs;i++))
		do
			echo "Putting CPU"$i" back online..."
			$(echo 1 > /sys/devices/system/cpu/cpu$i/online)
			sleep 1
		done
	else
		#NAN

		#shut down cores
		for i in ${numa[0]//,/ }
		do 
			if [ $i -ne 0 ]; then
				echo "Setting CPU"$i" offline..."
				$(echo 0 > /sys/devices/system/cpu/cpu$i/online)
				sleep 1
			fi
		done

		sleep 1

		# put them back online
		for i in ${numa[0]//,/ }
		do
			if [ $i -ne 0 ]; then
				echo "Putting CPU"$i" back online..."
				$(echo 1 > /sys/devices/system/cpu/cpu$i/online)
				sleep 1
			fi
		done
	fi
}


