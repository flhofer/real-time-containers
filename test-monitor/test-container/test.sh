#!/bin/bash 

if [[ ! "$1" == "quiet" ]]; then

cat <<EOF

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
tests=${1:-'4'}

# cleanup and prepare environment
rm log-rt-app-tst-*/log-thread1-0.log

function testc () {
	# agruments grp - test count 

	for ((i=0; i<$2; i++)); do
		./containers.sh quiet test $(seq -s " " ${1}0 $1$i )
		./containers.sh stop ${1}*
		mkdir -p log/${1}-$(($i+1))
		cp log/rt-app-tst-${1}* log/${1}-$(($i+1))
		mv log/orchestrator.txt log/${1}-$(($i+1))
	done
}

if [[ "$tests" -ge 1 ]]; then
	testc 1 10 # test batch 1, < 1ms
fi
if [[ "$tests" -ge 2 ]]; then
	testc 2 2  # test batch 2, ~50%, 2.5ms
fi
if [[ "$tests" -ge 3 ]]; then
	testc 3 3  # test batch 3, mixed period <= 10ms
fi
if [[ "$tests" -ge 4 ]]; then
	testc 4 10 # test batch 4, industry sample case
fi
