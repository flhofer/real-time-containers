#!/bin/bash
host=$1
port=$2
vmpid=$3

function set_cmds () {
	# vary 2 or 3 cpu tests depending on isolation setting
	# usually -S = -t -a -n, instead
	cyctest='cyclictest -t '$1' -n -a -m -q -p 99 -l 100000'
	scyctest='stress -d '$1' --hdd-bytes 20M -c '$1' -i '$1' -m '$1' --vm-bytes 15M & cyclictest -t '$1' -n -a -m -q -p 99 -l 100000 && killall stress;'
	cshield='cset shield --exec -- '
}

echo "Cleaning up directory..."
$(rm Iso* NoIso*)

function build_ssh() {
	# create the sh command to remotely run stuff on virtual guest
	cmd='ssh root@'$host' -p '$port' "'"$*"'"'
}

function runtest() {
	# Run monitor, write to file but limit to 15 mins
	MPID=$(nmon -p -s 1 -r -c 900 -F $1$2.csv)

	echo $1$2": Test run..."
	echo '#'$1$2' Test result' >> $1-res.txt
	eval ${@:3} | sed -nr '/T:/p'  >> $1-res.txt

	#kill monitor
	kill -USR2 $MPID
}

function run_loop () {
	for i in {1..5}
	do
		runtest $1 $i $cmd
	done
}

function shield_host() {
	# create cpuset for vm at cpu 0-2 and move pid into it
	echo "Adding CPU shield.."
	eval cset shield -c 0-2 -k on --shield && cset shield --shield --threads --pid $vmpid
}

function unshield_host() {
	# reset cpuset
	echo "Removing CPU shield.."
	eval cset shield --reset
}

function shield_guest() {
	# create cpuset for vm at cpu 0-2 and move pid into it
	echo "Adding CPU shield to guest.."
	build_ssh cset shield -c 0-1 -k on --shield
	eval $cmd
}

function unshield_guest() {
	# reset cpuset
	echo "Removing CPU shield from guest.."
	build_ssh cset shield --reset
	eval $cmd
}

function irq_affinity() {
	echo "Setting IRQ affinity to "$1
	for file in /proc/irq/*; do
	   echo $1 > $file/smp_affinity;
	done
	}

function guest_irq_affinity() {
	echo "Setting guest IRQ affinity to "$1

	loop='for file in /proc/irq/*; do echo $1 > \$file/smp_affinity; done'
	build_ssh $loop
	eval $cmd
	}

function load_balancer() {
	echo "Setting load balancer to "$1
	echo $1 > /sys/fs/cgroup/cpuset/cpuset.sched_load_balance
	echo $1 > /sys/fs/cgroup/cpuset/user/cpuset.sched_load_balance
	echo 1 > /sys/fs/cgroup/cpuset/system/cpuset.sched_load_balance
	}

function guest_load_balancer() {
	echo "Setting guest load balancer to "$1
	loop='echo '$1' > /sys/fs/cgroup/cpuset/cpuset.sched_load_balance && echo '$1' > /sys/fs/cgroup/cpuset/user/cpuset.sched_load_balance && echo 1 > /sys/fs/cgroup/cpuset/system/cpuset.sched_load_balance'	
	build_ssh $loop
	eval $cmd
	}

function LoadNoLoad () {
	# run a noload then a load test with recording

	# run build command -> returns $cmd
	build_ssh ${@:2} $cyctest
	run_loop $1NoLoad

	build_ssh ${@:2} $scyctest
	run_loop $1Load
}

# set commands to use 3 threads, 1 per vCPU
set_cmds 3

echo "Start no isolation tests..."
LoadNoLoad NoIso

echo "Start isolation tests..."
shield_host
LoadNoLoad Iso

echo "Start isolation tests no load balancer..."
load_balancer 0
LoadNoLoad IsoNoBal

echo "Start isolation tests adding IRQ affinity..."
irq_affinity 8
LoadNoLoad IsoNoBalIRQ

# set commands to use 2 threads, 1 per real-time dedicated vCPU
set_cmds 2

echo "Start isolation tests guest..."
shield_guest
LoadNoLoad IsoG $cshield

echo "Start isolation tests no load balancer..."
guest_load_balancer 0
LoadNoLoad IsoNoBalG $cshield

echo "Start isolation tests adding IRQ affinity..."
guest_irq_affinity 4
LoadNoLoad IsoNoBalIRQG $cshield

echo "Resetting guest..."
guest_load_balancer 1
guest_irq_affinity f
unshield_guest

echo "Resetting host..."
load_balancer 1
irq_affinity f
unshield_host

echo "Resetting user permissions to 1000 (default user)..."
$(chown 1000:1000 *)
