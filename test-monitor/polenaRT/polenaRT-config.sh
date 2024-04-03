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

syscpu="/sys/devices/system/cpu"

#get number of cpu-threads
prcs=$(nproc --all)

#get number of numa nodes
numanr=$(lscpu | grep NUMA | grep 'node(s)' -m 1 | awk '{print $3}')

irq_affinity() {
	echo "Setting IRQ affinity to "$1
	for file in /proc/irq/*/; do
	   echo $1 > $file/smp_affinity;
	done
	echo $1 > /proc/irq/default_smp_affinity
	}

smt_switch () {
	################################
	# control SMT switch
	################################
	local switch=$1

	case "$switch" in
	
		on|off )
			$sudo sh -c "echo $switch > ${syscpu}/smt/control"
			;;
			
		*)
			echo "ERROR: parameter ${switch} invalid for SMT"
			;;
		esac
}

load_balancer() {
	echo "Setting load balancer to "$1
	echo $1 > /sys/fs/cgroup/cpuset/cpuset.sched_load_balance
	echo $1 > /sys/fs/cgroup/cpuset/user/cpuset.sched_load_balance
	echo 1 > /sys/fs/cgroup/cpuset/system/cpuset.sched_load_balance
	}

restartCores () {
	# verify if core 0 is disableable
	if [ -e "/sys/devices/system/cpu/cpu0/online" ]; then
		echo "Setting CPU0 offline..."
		# if yes, disable and reenable it immediately, otherwise no cpu left XD
		$(echo 0 > /sys/devices/system/cpu/cpu0/online)
		sleep 1
		echo "Putting CPU0 back online..."
		$(echo 1 > /sys/devices/system/cpu/cpu0/online)
	fi

	if false; then
	if [ $numanr -ge 2 ]; then

		#get cpus assigned to numa nodes / split there for better performance
		for i in 0..$numanr; do 
			numa[$i]=$(lscpu | grep NUMA | grep 'node'$i'' -m 1 | awk '{print $4}')
		done

	else
		echo "Single NUMA node detected, selecting all excepet cpu0 for isolation.."
		numa[0]='1-'$((prcs-1)) # string
	fi	
	fi

	#check if numa is a number or a list
	re='^[0-9]+$';
	if [[ $numanr -eq 1 ]]; then
		#simple number of cores

		#shut down cores
		local start=$(($prcs-1))
		for i in `seq $start -1 1` 
		do 
			echo "Setting CPU"$i" offline..."
			$(echo 0 > /sys/devices/system/cpu/cpu$i/online)
			sleep 1
		done

		sleep 1

		# put them back online
		local end=$start
		for i in `seq 1 $end`
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

############### Check privileges ######################
sudo=
if [ "$(id -u)" -ne 0 ]; then
	if [ -z "$(command -v sudo)" ]; then
		cat >&2 <<-EOF
		Error: this installer needs the ability to run commands as root.
		You are not running as root and we are unable to find "sudo" available.
		EOF
		exit 1
	fi
	sudo="sudo -E"
fi

local end=$(($prcs-1))
for i in `seq 0 $end`; do 
	cd=$(cat ${syscpu}/cpu$i/topology/thread_siblings); printf %X $(( 0x$cd & ~( 1<<($i-1) ) ))
done

#smt_switch off

