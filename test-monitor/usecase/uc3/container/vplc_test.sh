#!/bin/bash 

if [ ! "$1" = "quiet" ]; then
	cat <<-EOF

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
else 
	shift
fi

print_help () {

	cat<<-EOF
	Usage: $0 cont [no-tests] [cpu-set]
	       $0 wave [nic] [wave-ip]

	to control container load test or wave scope test
	
	where:
	cpu-set          Whether to apply a CPU-set pinning to the container and sub-tasks, i.e., a CPU-list
	nic              The ethernet controller to pass as dedicated network card (can be internal-virtual)
	wave-ip          IPv4 address of the scope
	EOF
	
}

testc () {
	# agruments $1 test count $2 cpu affinity

	local i=1
	local j=1
	while [ $i -le $1 ]; do 

		./orchestrator -df --policy=SCHED_FIFO > log/orchestrator.txt 2>&1 &
		sleep 10
		SPID=$(ps h -o pid -C orchestrator)

		j=1
		while [ $j -le $i ]; do 
				./vplc_cont.sh quiet start runtime${j} -v eth0 $2
				: $(( j+=1 ))                   # loop increase
		done

		sleep 900

		j=1
		while [ $j -le $i ]; do 
				./vplc_cont.sh quiet stop runtime${j}
				: $(( j+=1 ))                   # loop increase
		done

		mkdir -p log/${i}

		j=1
		while [ $j -le $i ]; do 

				for f in /tmp/codesyscontrol${j}/*.log; do
					fn=${f##*/}
					tgt=${fn/_/Con${j}_}
					if [ "$fn" = "$tgt" ] ; then
						tgt=${fn/.log/Con${j}.log}
					fi
					mv ${f} log/${i}/${tgt}
				done

				: $(( j+=1 ))                   # loop increase
		done

		# end orchestrator
		kill -s INT $SPID
		sleep 1

		# move stuff 
		chown -R 1000:1000 log/*

		mv log/orchestrator.txt log/${i}

		: $(( i+=1 ))                   # loop increase
	done
}

if [ "$cmd" = "help" ]; then
	print_help
	exit 0
fi

if [ "$1" = "cont" ]; then

	shift
	
	# by default does all tests
	tests=${1:-'10'}
	afty=${2:-''}

	testc $tests $afty
	
elif [ "$1" = "wave" ]; then

	shift
	card=${1:-'enp2s0'}
	waveip=${2:-'192.168.105.128'}
	
	./vplc_cont.sh quiet start testio $card
	
	eval python3 main.py -v -t 180 $waveip
	
	./vplc_cont.sh quiet stop testio
else
	print_help 
fi