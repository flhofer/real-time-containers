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

############### Init and defaults ######################

syscpu="/sys/devices/system/cpu"
syskern="/proc/sys/kernel"
syscg="/sys/fs/cgroup"
def_map=0xffffffffffffffff

#get number of cpu-threads
prcs=$(nproc --all)
cpu_iso=1	# list of cpus to use for realtime operation, from arguments, defaults to all
cpu_map=0	# corresponing map for isolated realtime cpus -> computed, if 0 use no isolation
smt_map=0	# Map for enabled SMT threads, defaults to nothing -> detect

#get number of numa nodes
numanr=$(lscpu | grep NUMA | grep 'node(s)' -m 1 | awk '{print $3}')

############### Runtime setter functions ######################

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

smt_selective_map () {
	################################
	# SMT - get map of selective active
	################################

	# *** (WIP) *** 
	local cpu_map=${1:-$def_map}

	local dis_map=0				# cpu disable map - hex
	local i=0
	while [ $i -lt $prcs ]; do 
		local imap=$((1<<$i))	# cpu number in hex

		if [ $(( $imap & $cpu_map )) -eq 0 ]; then
			i=$(( $i+1 ))			# loop increase - cpuno
			continue
		fi

		# if not in disable map (sibling of earlier cpu) test for siblings
		if [ $(( $imap & $dis_map )) -eq 0 ]; then
			sibling=$(cat ${syscpu}/cpu$i/topology/thread_siblings 2> /dev/null) 
			if [ -n "$sibling" ]; then
				dis_map=$(( $dis_map | (0x$sibling & ~$imap) ))
			else
				echo "WARNING: disabled CPU detected.. skipping"
			fi
		fi
		i=$(( $i+1 ))			# loop increase - cpuno
	done
	smt_map=$(( ~${dis_map} ))
}

smt_selective () {
	################################
	# SMT - comtrol selective hotplug
	################################
	
	if [ $smt_map -eq 0 ]; then
		smt_selective_map $1
	fi

	local i=1	# we do not disable cpu0
	while [ $i -lt $prcs ]; do 
		local imap=$((1<<$i))	# cpu number in hex

		if [ $(( $imap & $smt_map )) -eq 0 ]; then
			# disable selected CPU
			$sudo sh -c "echo 0 > $syscpu/cpu$i/online"
		fi
		i=$(( $i+1 ))			# loop increase - cpuno
	done
}

load_balancer() {
	################################
	# Load balancer (CFS) switch
	################################
	
	#TODO: this needs a cgroup Update!
	
	echo "Setting load balancer to "$1
	$sudo sh -c "echo $1 > /sys/fs/cgroup/cpuset/cpuset.sched_load_balance"
	$sudo sh -c "echo $1 > /sys/fs/cgroup/cpuset/user/cpuset.sched_load_balance"
	$sudo sh -c "echo 1 >  /sys/fs/cgroup/cpuset/system/cpuset.sched_load_balance"
}

restartCores () {
	################################
	# Hotplug to force kthead move
	################################
	
	#TODO hotplug only on cpu_map cpus
	
	# verify if core 0 is disableable
	if [ -e "/sys/devices/system/cpu/cpu0/online" ]; then
		echo "Setting CPU0 offline..."
		# if yes, disable and reenable it immediately, otherwise no cpu left XD
		$sudo sh -c "echo 0 > /sys/devices/system/cpu/cpu0/online"
		sleep 1
		echo "Putting CPU0 back online..."
		$sudo sh -c "echo 1 > /sys/devices/system/cpu/cpu0/online"
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
	if [ $numanr -eq 1 ]; then
		#simple number of cores

		#shut down cores
		local start=$(($prcs-1))
		for i in `seq $start -1 1` 
		do 
			echo "Setting CPU"$i" offline..."
			$sudo sh -c "echo 0 > /sys/devices/system/cpu/cpu$i/online"
			sleep 1
		done

		sleep 1

		# put them back online
		local end=$start
		for i in `seq 1 $end`
		do
			echo "Putting CPU"$i" back online..."
			$sudo sh -c "echo 1 > /sys/devices/system/cpu/cpu$i/online"
			sleep 1
		done
	else
		#NAN

		#shut down cores
		for i in ${numa[0]//,/ }
		do 
			if [ $i -ne 0 ]; then
				echo "Setting CPU"$i" offline..."
				$sudo sh -c "echo 0 > /sys/devices/system/cpu/cpu$i/online"
				sleep 1
			fi
		done

		sleep 1

		# put them back online
		for i in ${numa[0]//,/ }
		do
			if [ $i -ne 0 ]; then
				echo "Putting CPU"$i" back online..."
				$sudo sh -c "echo 1 > /sys/devices/system/cpu/cpu$i/online"
				sleep 1
			fi
		done
	fi
}

performance_on () {
	################################
	# Put all cpus to performance
	################################
	
	governors=$(cat $syscpu/cpu0/cpufreq/scaling_available_governors 2> /dev/null )
	
	if [ "${governors#*performance}" = "$governors" ]; then
		echo "WARNING: no performance governor installed"
		return 1
	fi
	
	local i=0
	while [ $i -lt $prcs ]; do 
		$sudo sh -c "echo "performance" > $syscpu/cpu$i/cpufreq/scaling_governor"
		$sudo sh -c "echo "n/a" > $syscpu/cpu$i/power/pm_qos_resume_latency_us"
		i=$(( $i+1 ))			# loop increase - cpuno
	done
}

rt_kernel_set () {
	################################
	# set realtime throttle and slice
	################################
	# arg 1: max RR slice in ms
	# arg 2: max percent of RT-time per period
	
	local slice=${1:-100}
	local perc=${2:-'-1'}

	if [ $perc -ge 0 ]; then
		local period=$(cat $syskern/sched_rt_period_us 2> /dev/null)
		: ${period:='1000000'}
		local runtime=$(( $period * perc / 100 ))
		$sudo sh -c "echo $runtime > $syskern/sched_rt_runtime_us"
	else	
		if [ -z $cpu_iso ]; then
			echo "Info: no real-time CPU-range specified. Skipping RT-throttle off"
		else
			$sudo sh -c "echo -1 > $syskern/sched_rt_runtime_us"
		fi
	fi
	$sudo sh -c "echo $slice > $syskern/sched_rr_timeslice_ms"
}

irqbalance_off () {
	################################
	# IRQ balance daemon config
	################################
	# on systemd machines, use irqbalance to exlude rt-cores

	local conf="/etc/default/irqalance"
	if [ -e "$conf" ]; then
		
		if [ -z $cpu_iso ]; then
			echo "Info: no real-time CPU-range specified. Skipping IRQ-balance daemon setting"
			return 1
		fi

		#TODO: mask vs list
		# add list parameter
		$sudo sh -c "sed -i=rtconf_old -e 's/.\(IRQBALANCE_BANNED_CPULIST=\).*/\1'$cpu_iso'/' $conf"
		# remove mask parameter
		$sudo sh -c "sed -i -e 's/.\(IRQBALANCE_BANNED_CPUS=\).*/\1/' $conf"
		
		$sudo service irqbalance restart
		
		return 0
	fi

	echo "WARNING: Can not find IRQbalance config file $conf"
	echo ".. will use manual IRQ affinity"

	local sys_map=$(( ~$cpu_map & (1<<$prcs)-1 ))

	$sudo sh -c "printf %x $sys_map > /proc/irq/default_smp_affinity"

	for irq in /proc/irq/*/ ; do
		local aff=$(cat ${irq}smp_affinity 2> /dev/null)
		: ${aff:=${def_map#0x}}
		aff=$(( 0x$aff & $sys_map ))
		if [ $aff -eq 0 ]; then
			aff=$sys_map
		fi
		$sudo sh -c "printf %x $aff > ${irq}smp_affinity"
	done
	
}

detect_cgroup () {
	################################
	# Test CGroup version
	################################
	
	# only v2 has list of controllers returns 0 if v2
	[ -e $syscg/cgroup.controllers ]
	return $?
}

config_docker () {
	################################
	# docker daemon for CGropup v2
	################################
	
	# Configures the daemon to use a separate slice for docker
	
	# docker.slice
	# /etc/docker/daemon.json
	# /var/snap/docker/current/config/daemon.json 

#	{
#		"cgroup-parent":    "docker.slice",
#		"log-level":        "error"
#	}

#	systemctl restart docker or snap restart docker
	return 0
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

detect_cgroup 
echo $?

#irqbalance_off 
#rt_kernel_set 100 95
#performance_on
#restartCores
#smt_selective 0xFFF0
#smt_switch off

