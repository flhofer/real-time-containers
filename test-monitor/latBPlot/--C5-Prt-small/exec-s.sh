#!/bin/bash

echo "WARN: Test mode active. Edit script to run normally"

cat <<EOF

######################################

Test bench execution
______     _                  
| ___ \   | |                 
| |_/ /__ | | ___ _ __   __ _ 
|  __/ _ \| |/ _ \ '_ \ / _' |
| | | (_) | |  __/ | | | (_| |
\_|  \___/|_|\___|_| |_|\__,_|                              
                              
simply real-time containers

######################################

EOF

##################### DETERMINE CLI PARAMETERS ##########################

if [ $# -lt 2 ] && [ $# -ne 0 ]; then

cat <<EOF
Not enough arguments supplied!
Usag: ./exec.sh [NRvCPU] [montime] [testcnt] [tthreads]
 or   ./exec.sh (for defaults)

Defaults are:
NRvCPU = 3              number of virtual CPUs assigned to the VM
montime = 3600 sec       max monitoring time of system
testcnt = 5				number of times tests are repeated
tthreads = auto         testing threads, use available vCPUs by default
EOF

	exit 1
fi

novcpu=${1:-'3'}
montime=${2:-'3600'}
testcnt=${3:-'5'}
# if tthread 0, default (RT vCPUs) are used
tthreads=${4:-'0'}

##################### DETERMINE HARDWARE PARAMETERS ##########################

#here we need to switch for OS type
if [[ "$OSTYPE" == "linux-gnu" ]]; then
        #...

        #get number of cpu-threads
		prcs=$(nproc --all)

		#get number of numa nodes
		numanr=$(lscpu | grep NUMA | grep 'node(s)' -m 1 | awk '{print $3}')

		if [ $numanr -ge 2 ]; then

			#get cpus assigned to numa nodes / split there for better performance
			for ((i=0;i<$numanr;i++)); do 
				numa[$i]=$(lscpu | grep NUMA | grep 'node'$i'' -m 1 | awk '{print $4}')
			done

			prcsrun=$((prcs/numanr))
		else
			echo "Single NUMA node detected, selecting all excepet cpu0 for isolation.."
			numa[0]='1-'$((prcs-1)) # string
			prcsrun=$((prcs-1))
		fi	

elif [[ "$OSTYPE" == "darwin"* ]]; then
        # Mac OSX
        
        #get number of cpu-threads
		prcs=$(sysctl -n hw.logicalcpu)

		numanr=1

		#Macs have single NUMA only
		echo "Single NUMA node Mac OSX detected, selecting all excepet cpu0 for isolation.."
		numa[0]='1-'$((prcs-1)) # string
		prcsrun=$((prcs-1))

# elif [[ "$OSTYPE" == "cygwin" ]]; then
#         # POSIX compatibility layer and Linux environment emulation for Windows
#         echo "OS not supported."
#         exit 1

# elif [[ "$OSTYPE" == "msys" ]]; then
#         # Lightweight shell and GNU utilities compiled for Windows (part of MinGW)
#         echo "OS not supported."
#         exit 1

# elif [[ "$OSTYPE" == "win32" ]]; then
#         # I'm not sure this can happen.
#         echo "OS not supported."
#         exit 1

# elif [[ "$OSTYPE" == "freebsd"* ]]; then
#         # ...
#         echo "OS not supported."
#         exit 1

else
        # Unknown.
        echo "OS not supported."
        exit 1
fi

echo "Configuration: "$prcs" threads, "$prcsrun" selected for isolation on "$numanr" NUMA node(s)"
echo "We will assume "$novcpu" virtual CPUs in the VM and use "$tthreads" testing thread(s)"
if [ $numanr -ge 2 ]; then
	echo "The present Numa configurations are:"
	echo ${numa[*]}
fi

#exit 0


##################### FUNCTION DECLARATION ##########################

function set_cmds () {
	# vary cpu tests depending on isolation setting
	# usually -S = -t -a -n, instead, but this way we can have less threads than vCPUs
	cyctest='cyclictest -t '$2' -n -a -m -q -p 99 -l 1000000 -h 10000 > stats.txt'
	scyctest='stress -d '$1' --hdd-bytes 20M -c '$1' -i '$1' -m '$1' --vm-bytes 15M'
	scyctestend='killall stress'
	cshield='cset shield --exec --threads -- '
}

function build_ssh() {
	# create the sh command to remotely run stuff on virtual guest
	cmd='ssh root@'$host' -p '$port' "'"$*"'"'
}

function runtest() {
	# Run monitor, write to file but limit to 15 mins (default)
	MPID=$(nmon -p -s 1 -r -c $montime -F $1$2.csv)

	echo $1$2": Test run..."
	echo '#'$1$2' Test result' >> $1-res.txt
	eval ${@:3} | sed -nr '/T:/p'  >> $1-res.txt

	#kill monitor
	kill -USR2 $MPID
}

function run_loop () {
	for ((i=1;i<=$testcnt;i++))
	do
		runtest $1 $i $cmd
	done
}

function shield_host() {
	# create cpuset for vm at cpus of numa0 and move pid into it
	echo "Adding CPU shield.."
	eval cset shield -c ${numa[0]} -k on --shield
}

function unshield_host() {
	# reset cpuset
	echo "Removing CPU shield.."
	eval cset shield --reset
}

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

function loadNoLoad () {
	# run a noload then a load test with recording

	# run build command -> returns $cmd
	cmd=${@:2}" "$cyctest
#	run_loop $1NoLoad

	eval ${@:2}" "$scyctest" &"
	run_loop $1Load
	eval $scyctestend
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

##################### TEST EXECUTION CODE ##########################

echo "Cleaning up directory..."
$(rm Iso* NoIso*)

#echo "Resetting host..."
#load_balancer 1
#irq_affinity $( echo "obase=16;" $((((1<<prcs))-1)) | bc )
#unshield_host

# set commands to use [tthread] or [novcpu] threads on [novcpus] vCPUs
if [ "$tthreads" -eq 0 ]; then
	set_cmds $prcsrun $prcsrun
else
	set_cmds $prcsrun $tthreads
fi

#echo "Start no isolation tests..."
#loadNoLoad NoIso

echo "Start isolation tests..."
shield_host
loadNoLoad Iso $cshield

#echo "Start isolation tests no load balancer..."
#load_balancer 0
#loadNoLoad IsoNoBal $cshield

#echo "Start isolation tests adding IRQ affinity..."
#WARNING! this value must be checked! risk of crash!
#irq_affinity 1
#restartCores
# perform shielding again
#shield_host
#loadNoLoad IsoNoBalIRQ $cshield

echo "Resetting host..."
#load_balancer 1
#irq_affinity $( echo "obase=16;" $((((1<<prcs))-1)) | bc )
unshield_host

echo "Resetting user permissions to 1000 (default user)..."
$(chown 1000:1000 *)

