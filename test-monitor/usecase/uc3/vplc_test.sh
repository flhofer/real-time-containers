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

# by default does all tests
tests=${1:-'10'}
afty=${2:-''}

testc () {
	# agruments $1 test count $2 cpu affinity

	local i=1
	local j=1
	while [ $i -le $1 ]; do 

		j=1
		while [ $j -le $i ]; do 
				./vplc_cont.sh start runtime${j} eth0 $2
				: $(( j+=1 ))                   # loop increase
		done

		sleep 900

		j=1
		while [ $j -le $i ]; do 
				./vplc_cont.sh stop runtime${j}
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

		mv log/orchestrator.txt log/${i}

		: $(( i+=1 ))                   # loop increase
	done
}

testc $tests $afty

