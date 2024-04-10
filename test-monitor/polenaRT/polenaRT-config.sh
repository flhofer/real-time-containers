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
grubfile="/etc/default/grub"

#get number of cpu-threads
prcs=$(nproc --all)
cpu_iso=2-$(( $prcs -1))	# list of cpus to use for realtime operation, from arguments, defaults to all, defaults to all but first two
cpu_sys=0					# list of cpus to use for system tasks, computed from cpu_iso
cpu_map=0					# corresponing map for isolated realtime cpus -> computed, if 0 use no isolation
smt_map=0					# Map for enabled SMT threads, defaults to nothing -> detect
rr_slice=100				# default RR slice in ms
rt_throt=95					# default RT percentage

#get number of numa nodes
numanr=$(lscpu | grep NUMA | grep 'node(s)' -m 1 | awk '{print $3}')

############### command line argument parsing ######################

# arguments to parse
#selsmt --selective-smt
#nocb_poll_bypass --nocb-bypass


############### Yes/no selector for the calls ######################

yes_no () {
	################################
	# Manual imp. select (interop)
	################################
	
	# $1 function name
	# $2:@ function with parameters to execute if successful
	local name=$1
	shift 1
	
	local i=0
	for txt in ${items}; do 
		i=$(( ${i}+1 )); 
		echo "$i) $txt";
	done  
	local sel=
	until read -p "Execute '$name' (y/N) : " sel; 
	[ "$sel" = "y" ] || [ "$sel" = "n" ] || [ "$sel" = "" ] ; 	
	do
	  echo "invalid selection!"
	done
	
	if [ "$sel" = "y" ] ; then
		eval $@
		return 0
	fi
	return 1
}

_msg () {
	local color=$1
	local type=$2
	shift 2
	printf "$color$type: \033[22m${*}\033[m\\n"
}

error_msg () {
	_msg "\033[1;31m" "ERROR" ${*}
}

warning_msg () {
	_msg "\033[1;33m" "WARNING" ${*}
}

info_msg () {
	_msg "\033[0;37m" "Info" ${*}
}

success_msg () {
	_msg "\033[0;32m" "Success" ${*}
}

############### Kernel parameter parsing ######################

parse_boot_parameter () {
	################################
	# readout and parse active selected parameters
	################################
	
	#WARNING: this sets global variables directly
	cmdline=$(cat /proc/cmdline)
	
	# scan for parameters that interest us
	for par in $cmdline; do 
		case $par in
			nosmt )
				nosmt=1
				cmdline=${cmdline#*${par}}
				;;
			isolcpus=* )
				isolcpus=${par#isolcpus=}
				cmdline=${cmdline#*${par}}
				;;
			rcu_nocbs=* )
				rcu_nocbs=${par#rcu_nocbs=}
				cmdline=${cmdline#*${par}}
				;;
			rcu_nocb_poll )
				rcu_nocb_poll=1
				cmdline=${cmdline#*${par}}
				;;
			irqaffinity=* )
				irqaffinity=${par#irqaffinity=}
				cmdline=${cmdline#*${par}}
				;;
			skew_tick=* )
				skew_tick=${par#skew_tick=}
				cmdline=${cmdline#*${par}}
				;;
			intel_pstate=* )
				intel_pstate=${par#intel_pstate=}
				cmdline=${cmdline#*${par}}
				;;
			nosoftlockup )
				nosoftlockup=1
				cmdline=${cmdline#*${par}}
				;;
			tsc=* )
				tsc=${par#tsc=}
				cmdline=${cmdline#*${par}}
				;;
			timer_migration=* )
				timer_migration=${par#timer_migration=}
				cmdline=${cmdline#*${par}}
				;;
			kthread_cpus=* )
				kthread_cpus=${par#kthread_cpus=}
				cmdline=${cmdline#*${par}}
				;;
			systemd.unified_cgroup_hierarchy=* )
				systemd_cg2=${par#systemd.unified_cgroup_hierarchy=}
				cmdline=${cmdline#*${par}}
				;;
			# Delete the following, not added by config
			BOOT_IMAGE=*|root=*|ro|quiet )
				cmdline=${cmdline#*${par}}			
		esac
	done
	cmdline=${cmdline#\ } # delete heading space
	info_msg "Keeping '$cmdline' parameters"
}

set_boot_parameter () {
	
	local parameters=$cmdline
	
	if [ ! -e "$grubfile" ] ; then
		warning_msg "Can not read grub file $grubfile, skippring parameter update"
		return 1
	fi
	
	# smt disabled at boot -- allow skipping
	if [ -n "$nosmt" ] && [ $nosmt -eq 1 ]; then
		if [ -n "$selsmt" ]; then
			info_msg "'nosmt' enabled, skipping ('--selective-smt' set)"
		else
			info_msg "'nosmt' enabled but selective smt reccomended. Run with '--selective-smt' to performe change"
			cmdline="$cmdline nosmt"
		fi
	fi
	
	# CPU isolation boot parameter, not reccomended -- WIP what to do if
	if [ -n "$isolcpus" ]; then
		info_msg "The 'isolcpus' boot parameter is highhly discouraged. Please use 'isolated' CGroup partition instead"
		IFS=","
		line=""
		for par in $isolcpus; do
			case $par in
			domain )
				line="$line,domain"
				;;
			managed_irq)
				line="$line,managed_irq"
				;;
			* )
				line="$line,$par"
			esac
		done
		
		if [ -n "$line" ]; then
			cmdline="$cmdline isolcpus=${line#,}"
		fi
	fi
	
	#if [ -n rcu_nocbs ]; then # uncomment if it becomes optional
	#TODO add kernel build detection
	if [ -n "$cpu_iso" ]; then
		rcu_map=
		compute_masks "$rcu_nocbs" rcu_mask
		
		if [ ! $rcu_map = $cpu_map ]; then
			warning_msg "Resetting RCU_backoff mask from 0x$rcu_map to 0x$cpu_map"
		fi
		
		cmdline="$cmdline rcu_nocbs=${cpu_iso}"
		unset rcu_map
		rcu_nocbs=$cpu_iso
	fi
	# Set poll mode if it was set
	if [ -n "$rcu_nocbs" ] && [ -n "$rcu_nocb_poll" ] && [ $rcu_nocb_poll -eq 1 ] ; then
		if [ -n "$nocb_poll_bypass" ] && [ $nocb_poll_bypass -eq 1 ] ; then
			info_msg "Bypass with '--nocb-bypass'."
		else
			info_msg "Setting not reccomended 'rcu_nocb_poll' boot parameter. Bypass with '--nocb-bypass'."
			cmdline="$cmdline rcu_nocb_poll"
		fi
	fi
	
			irqaffinity=* )
				irqaffinity=${par#irqaffinity=}
				cmdline=${cmdline#*${par}}
				;;
			skew_tick=* )
				skew_tick=${par#skew_tick=}
				cmdline=${cmdline#*${par}}
				;;
			intel_pstate=* )
				intel_pstate=${par#intel_pstate=}
				cmdline=${cmdline#*${par}}
				;;
			nosoftlockup )
				nosoftlockup=1
				cmdline=${cmdline#*${par}}
				;;
			tsc=* )
				tsc=${par#tsc=}
				cmdline=${cmdline#*${par}}
				;;
			timer_migration=* )
				timer_migration=${par#timer_migration=}
				cmdline=${cmdline#*${par}}
				;;
			kthread_cpus=* )
				kthread_cpus=${par#kthread_cpus=}
				cmdline=${cmdline#*${par}}
				;;
			systemd.unified_cgroup_hierarchy=* )
				systemd_cg2=${par#systemd.unified_cgroup_hierarchy=}
				cmdline=${cmdline#*${par}}
				;;
			# Delete the following, not added by config
			BOOT_IMAGE=*|root=*|ro|quiet )
				cmdline=${cmdline#*${par}}			
		esac
	done
	
	
	# write
	echo "new Grub config"
	$sudo sh -c "sed '/^GRUB_CMDLINE_LINUX_DEFAULT/s/=\".*\"/=\"${parameters}\"/' ${grubfile}" 

	return $?
}

############### Runtime setter functions ######################

irq_affinity() {
	################################
	# Wrties an affinity mask to IRQ
	################################
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
			error_msg "parameter ${switch} invalid for SMT"
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
				warning_msg "disabled CPU detected.. skipping"
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
		warning_msg "no performance governor installed"
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
		if [ -z "$cpu_iso" ]; then
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
	local cpu_iso=$1
	local cpu_map=$2
	
	local conf="/etc/default/irqalance"
	if [ -e "$conf" ]; then
		
		if [ -z "$cpu_iso" ]; then
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

	warning_msg "Can not find IRQbalance config file $conf"
	warning_msg ".. will use manual IRQ affinity"

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
	local cfile="/etc/docker/daemon.json"
	local snap=0
	
	# Configures the daemon to use a separate slice for docker
	if [ ! -e /etc/docker/daemon.json ]; then	
		if [ -e /var/snap/docker/current/config/daemon.json ]; then
			cfile="/var/snap/docker/current/config/daemon.json"
			snap=1
		else
			# write new daemon config
			$sudo sh -c "cat > $cfile <<-EOF
			{
			    "cgroup-parent":    "docker.slice",
			    "log-level":        "error"
			}
			EOF"
		fi
	fi
	
	grep "docker.slice" $cfile > /dev/null
	if [ $? -ne 0 ]; then
	
		grep "cgroup-parent" $cfile > /dev/null
		if [ $? -ne 0 ]; then
			#substitute setting
			$sudo sh -c "sed -i=rtconf_old -e  's/\(\"cgroup-parent\".*\"\).*\(\"\)/\1docker.slice\2/' $cfile"
		else
			#insert setting
			$sudo sh -c "sed -i=rtconf_old -e  '2i\    \"cgroup-parent\":    \"docker.slice\",' $cfile"		
		fi
	fi
	
	# TODO: check, running containers ?
	# restart docker daemon
	if [ $snap -eq 0 ]; then
		$sudo sh -c "systemctl restart docker"
	else
		$sudo sh -c "snap restart docker"
	fi
}

timer_migration_off () {
	################################
	# Timer migration
	################################
	# We can stop timer migration between sockets at runtime with

	sudo sh -c "echo 0 > $syskern/timer_migration"
	return 0
}

cg_move_tasks () {
	################################
	# move pid from root to subgrp
	################################
	$sudo sh -c "mkdir -P $syscg/system"
	$sudo sh -c "cat $syscg/tasks $syscg/system/tasks"
}

cg_set_cpus () {
	################################
	# CG set cpus and set exclusive 
	################################
	local cpulist=$1
	local memlist=${2:-0}
	local syscpus=${3:-0}
	local sysmems=${4:-0}
	
	$sudo sh -c "echo $cpulist > $syscg/docker/cpuset.cpus"
	$sudo sh -c "echo $memlist > $syscg/docker/cpuset.mems"

	$sudo sh -c "echo $syscpus > $syscg/system/cpuset.cpus"
	$sudo sh -c "echo $sysmems > $syscg/system/cpuset.mems"
	
	$sudo sh -c "echo "root" > $syscg/docker/cpuset.cpu_exclusive"
}

cg_set_cpus2 () {
	################################
	# CG set cpus and set exclusive 
	################################
	local cpulist=$1
	local memlist=${2:-0}
	
	$sudo sh -c "echo $cpulist > $syscg/docker.slice/cpuset.cpus"
	$sudo sh -c "echo $memlist > $syscg/docker.slice/cpuset.mems"
	
	$sudo sh -c "echo "root" > $syscg/docker.slice/cpuset.cpu.partition"
}

compute_masks () {
	################################
	# compute lists and masks
	################################
	local cpulist=$1	# base (input) CPU list 
	local varmap=$2		# variable name for computed hex map
	local varinv=$3		# variable name for computed inverted CPU list

	local map=0x0
	local list=

	IFS=","
	for el in $cpulist; do
		local begin=
		IFS="-"
		for term in $el; do
			map=$(( $map | 1<<$term ))
			if [ -n "$begin" ]; then
				# fill bits from "begin" to this bit
				local i=$begin
				while [ $i -lt $term ]; do
					map=$(( $map | 1<<$i ))
					i=$(( $i+1 ))			# loop increase - cpuno
				done
			else
			 	begin=$term
			fi
		done
	done
	
	IFS=" "
	local mapinv=$(( ~$map & (1<<$prcs)-1 ))

	local i=0		# start CPU0, add to list as we go
	local high=0	# last bit was high
	local two=0		# last two bits high, =list
	while [ $i -lt $prcs ]; do
		if [ $(( $mapinv & 1<<$i )) -eq 0 ]; then
			if [ $two -ne 0 ]; then
				# close list
				inv="${inv}-$(( ${i} - 1 ))"
			fi
			two=0
			high=0
			i=$(( $i+1 ))			# loop increase - cpuno
			continue
		fi
		if [ $high -eq 1 ] && [ $two -eq 0 ]; then
			two=1				# last two flags were high
		elif [ $high -eq 0 ]; then
			# Init with first value or append to list with comma
			if [ -z "$inv" ]; then
				inv="$i"
			else
				inv="${inv},$i"
			fi
			high=1				# last flag was high
		fi
		i=$(( $i+1 ))			# loop increase - cpuno
	done
	# ending with 'two' set = begun list, close here
	if [ $two -ne 0 ]; then
		# close list
		inv="${inv}-$(( ${i} - 1 ))"
	fi
	
	# set caller variables by name
	eval ${varmap}=${map}
		if [ -n "$varinv" ]; then
		eval ${varinv}=${inv}
	fi
}

############### Check privileges ######################
sudo=
if [ "$(id -u)" -ne 0 ]; then
	if [ -z "$(command -v sudo)" ]; then
		error_msg "this installer needs the ability to run commands as root.\\n  ...You are not running as root and we are unable to find \"sudo\" available."
		exit 1
	fi
	sudo="sudo -E"
	# ask for password right away
	info_msg "The following requires root or sudo privileges" 
	$sudo echo ""
fi

############# Compute CPU masks and lists #############

compute_masks $cpu_iso cpu_map cpu_sys
info_msg $(printf "Using real-time cpu list: %s" $cpu_iso)
info_msg $(printf "Using system cpu list: %s" $cpu_sys)
info_msg $(printf "Using real-time cpu map: 0x%x" $cpu_map)

############# Get boot parameters of system #############
parse_boot_parameter

# check for required commands and files for grub update
#- and warn about missing required commands before doing any actual work.
abort=1
for cmd in update-grub update-grub2; do
	if [ -n "$(command -v $cmd)" ]; then
		abort=0
	fi
done
[ $abort -eq 1 ] && warning_msg "Unable to update grub configuration. Utils not found."

if [ $abort -eq 0 ]; then
	set_boot_parameter
fi

yes_no "Stop execution?" exit 0

############### Execute YES/NO ######################
# skip smt setting if kernel parameter is set
if [ -z "$nosmt" ] || [ $nosmt -eq 0 ]; then
	if ! yes_no "Switch off SMT on all cores" smt_switch off ; then
		yes_no "Switch off SMT only on RT-cores" smt_selective $cpu_mask
	fi
fi

yes_no "Restart CPU-cores to shift tasks" restartCores

#skip irq affinity if boot parameter is set
if [ -z "$irqaffinity" ]; then
	yes_no "Disable IRQ-balance" irqbalance_off $cpu_isp $cpu_mask
fi
# skip pstate setting if driver is disabled
if [ "${intel_pstate#disabled}" = "${intel_pstate}" ]; then
	yes_no "Enable performance settings" performance_on
fi
yes_no "Set Real-time throttling parameters" rt_kernel_set $rr_slice $rt_throt
#skip timer migration if disabled at boot
if [ -z "$timer_migration" ]; then
	yes_no "Disable timer migration" timer_migration_off
fi

if [ detect_cgroup ]; then
	# Cgroup v2
	yes_no "Setup CGroup for Docker" config_docker
	yes_no "Isolate Docker CGroup" cg_set_cpus2 $cpu_iso
else
	# Cgroup v1
	yes_no "Move System tasks into new CGroup" cg_move_tasks
	yes_no "Isolate Docker CPUs" cg_set_cpus $cpu_iso
fi

