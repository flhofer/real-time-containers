#!/bin/bash 

# cleanup and prepare environment
rm log-rt-app-tst-*/log-thread1-0.log

function testc () {
	# agruments grp - test count 

	for ((i=0; i<$2; i++)); do
		echo ./containers.sh test $(seq -s " " ${1}0 $1$i )
		echo ./containers.sh stop ${1}*
		mkdir -p log/${1}-$(($i+1))
		cp log/rt-app-tst-${1}* log/${1}-$(($i+1))
		mv log/orchestrator.txt log/${1}-$(($i+1))
	done
}

testc 1 10 # test batch 1, < 1ms
testc 2 2  # test batch 2, ~50%, 2.5ms
testc 3 3  # test batch 3, mixed period <= 10ms
testc 4 10 # test batch 4, industry sample case

