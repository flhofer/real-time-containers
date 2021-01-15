#!/bin/bash

function prepareTest() {
	no=$1
	testno=$2
	unset SPID

	if [ "$no" -eq 0 ]; then

		# do nothing, default setting TODO: reset all
		sleep 1

	elif [ "$no" -eq 1 ]; then

		# Manual settings with exclusive docker, 3 CPU dedicated + SMT
		eval 'echo -1 > /proc/sys/kernel/sched_rt_runtime_us'
		eval 'echo "1-3,5-8" > /sys/fs/cgroup/cpuset/docker/cpuset.cpus' 
		eval 'echo "1" > /sys/fs/cgroup/cpuset/docker/cpuset.cpu_exclusive' 

	elif [ "$no" -eq 2 ]; then

		# Without SMT
		for i in {4..7}; do
			eval "echo 0 > /sys/devices/system/cpu/cpu$i/online"
		done

	elif [ "$no" -eq 3 ]; then

		# Environment setup only
		eval ./orchestrator -fk > logs/out${no}.txt 2>&1 &
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 4 ]; then

		# Adaptive
		if [ "$testno" -eq 1 ]; then
			eval ./orchestrator -fk -A 0 orchUC1a.json > logs/out${no}.txt 2>&1 &
		else
			eval ./orchestrator -fk -A 0 orchUC2a.json >> logs/out${no}.txt 2>&1 &
		fi
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 5 ]; then

		# PAdaptive no info
		eval ./orchestrator -fk -A 1 > logs/out${no}.txt 2>&1 &
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 6 ]; then

		# PAdaptive info
		if [ "$testno" -eq 1 ]; then
			eval ./orchestrator -fk -S 0 orchUC1a.json > logs/out${no}.txt 2>&1 &
		else
			eval ./orchestrator -fk -S 0 orchUC2a.json >> logs/out${no}.txt 2>&1 &
		fi
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 7 ]; then

		# DSystem no info
		eval ./orchestrator -fk -S 0 > logs/out${no}.txt 2>&1 &
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 8 ]; then

		# DSystem info
		if [ "$testno" -eq 1 ]; then
			eval ./orchestrator -fk -S 0 orchUC1a.json > logs/out${no}.txt 2>&1 &
		else
			eval ./orchestrator -fk -S 0 orchUC2a.json >> logs/out${no}.txt 2>&1 &
		fi
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 9 ]; then

		# DSimple no info
		eval ./orchestrator -fk -S 1 > logs/out${no}.txt 2>&1 &
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 10 ]; then

		# Dsimple info
		if [ "$testno" -eq 1 ]; then
			eval ./orchestrator -fk -S 1 orchUC1a.json > logs/out${no}.txt 2>&1 &
		else
			eval ./orchestrator -fk -S 1 orchUC2a.json >> logs/out${no}.txt 2>&1 &
		fi
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)
	fi
}

#prepare and create orch output
eval mkdir -p logs

for k in {0..6}; do

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
