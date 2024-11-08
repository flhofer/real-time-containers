#!/bin/bash

nocpumax=$(( $(nproc --all) - 1))
nonumand=2

function prepareTest() {
	no=$1
	testno=$2
	unset SPID

	if [ "$no" -eq 0 ]; then

		# do nothing, default setting TODO: reset all - disable second cpu?
		eval 'mkdir -p /sys/fs/cgroup/docker.slice'
		eval 'echo "member" > /sys/fs/cgroup/docker.slice/cpuset.cpus.partition'
		sleep 1

		active=$( seq -s , 0 $nonumand $nocpumax );
 		for i in $( seq 0 $nonumand $nocpumax ); do
			eval "echo 1 > /sys/devices/system/cpu/cpu$i/online"
		done
		sleep 1

		eval 'echo "'$active'" > /sys/fs/cgroup/docker.slice/cpuset.cpus'
		sleep 1

		if [ $nonumand -gt 1 ]; then
			for i in $( seq 1 $nonumand $nocpumax ); do
				eval "echo 0 > /sys/devices/system/cpu/cpu$i/online"
			done
		fi

		sleep 1
	fi
	# always write separation for >=1
	if [ "$no" -ge 1 ]; then

		# Manual settings with exclusive docker, 3 CPU dedicated + SMT
		active=$( seq -s , $nonumand $nonumand $nocpumax );
		eval 'mkdir -p /sys/fs/cgroup/docker.slice'
		eval 'echo -1 > /proc/sys/kernel/sched_rt_runtime_us'
		eval 'echo "'$active'" > /sys/fs/cgroup/docker.slice/cpuset.cpus'
		eval 'echo "root" > /sys/fs/cgroup/docker.slice/cpuset.cpus.partition'

	fi
	# always write SMT for >=2
	if [ "$no" -ge 2 ]; then

		eval 'echo "member" > /sys/fs/cgroup/docker.slice/cpuset.cpus.partition'
		sleep 1
		# Without SMT
		for i in $( seq $(( $nocpumax/2+1 )) 1 $nocpumax ) ; do
			eval "echo 0 > /sys/devices/system/cpu/cpu$i/online"
		done

		sleep 1
		active=$( seq -s , $nonumand $nonumand $(( $nocpumax/2 )) );
		eval 'echo "'$active'" > /sys/fs/cgroup/docker.slice/cpuset.cpus'
		eval 'echo "root" > /sys/fs/cgroup/docker.slice/cpuset.cpus.partition'

	fi
	if [ "$no" -eq 3 ]; then

		# Environment setup only
		eval ./orchestrator -fk -a $active >> logs/out${no}.txt 2>&1 &
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 4 ]; then

		# Adaptive
		if [ "$testno" -eq 1 ]; then
			eval ./orchestrator -fk -a $active -A 0 orchUC1a.json > logs/out${no}.txt 2>&1 &
		else
			eval ./orchestrator -fk -a $active -A 0 orchUC2a.json >> logs/out${no}.txt 2>&1 &
		fi
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 5 ]; then

		# PAdaptive no info
		eval ./orchestrator -fk -a $active -A 1 >> logs/out${no}.txt 2>&1 &
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 6 ]; then

		# PAdaptive info
		if [ "$testno" -eq 1 ]; then
			eval ./orchestrator -fk -a $active -A 1 orchUC1a.json > logs/out${no}.txt 2>&1 &
		else
			eval ./orchestrator -fk -a $active -A 1 orchUC2a.json >> logs/out${no}.txt 2>&1 &
		fi
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 7 ]; then

		# DSimple no info
		eval ./orchestrator -fk -a $active -S 0 >> logs/out${no}.txt 2>&1 &
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

	elif [ "$no" -eq 8 ]; then

		# Dsimple info
		if [ "$testno" -eq 1 ]; then
			eval ./orchestrator -fk -a $active -S 0 orchUC1a.json > logs/out${no}.txt 2>&1 &
		else
			eval ./orchestrator -fk -a $active -S 0 orchUC2a.json >> logs/out${no}.txt 2>&1 &
		fi
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)
	fi
}

#prepare and create orch output
eval mkdir -p logs

from=${1:-'0'}
to=${2:-'8'}

for (( k=$from; k<=$to; k++ )); do

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
