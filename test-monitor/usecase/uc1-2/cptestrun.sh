#!/bin/bash

function prepareTest() {
	no=$1

	eval mkdir -p logs

	if [ "$no" -eq 0 ]; then

		# do nothing
		sleep 1

	elif [ "$no" -eq 1 ]; then

		# 4 cores
		for i in {4..7}; do
			eval "echo 0 > /sys/devices/system/cpu/cpu$i/online"
		done

		for i in {12..15}; do
			eval "echo 0 > /sys/devices/system/cpu/cpu$i/online"
		done

	elif [ "$no" -eq 2 ]; then

		eval 'echo -1 > /proc/sys/kernel/sched_rt_runtime_us'
		eval 'echo "1-3,9-11" > /sys/fs/cgroup/cpuset/docker/cpuset.cpus' 

	elif [ "$no" -eq 3 ]; then

		eval 'echo "1" > /sys/fs/cgroup/cpuset/docker/cpuset.cpu_exclusive' 

	elif [ "$no" -eq 4 ]; then

		for i in {8..12}; do
			eval "echo 0 > /sys/devices/system/cpu/cpu$i/online"
		done
	elif [ "$no" -eq 5 ]; then

		if [ "$2" -eq 1 ]; then
			eval ./orchestrator -fdk -S 0 -A 2 orchUC1a.json > logs/out.txt 2>&1 &
		else
			eval ./orchestrator -fdk -S 0 -A 2 orchUC2a.json >> logs/out.txt 2>&1 &
		fi
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)
	fi
}

for k in {0..5}; do

	prepareTest $k 1
	eval ./ucexec.sh test 1
	if [ -n "$SPID" ]; then
		# end orchestrator
		kill -SIGINT $SPID
		sleep 1
	fi
	sleep 30

	prepareTest $k 2
	eval ./ucexec.sh test 2
	sleep 30
	if [ -n "$SPID" ]; then
		# end orchestrator
		kill -SIGINT $SPID
		sleep 1
	fi

	mv logs/UC1* logs/TEST${k}_1
	mv logs/UC2* logs/TEST${k}_2

done
