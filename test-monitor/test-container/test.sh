#!/bin/sh 


print_help () {
	cat <<-EOF
	Usage: $0 [max-batch] [test-cmd] [arg1]

	Defaults are:
	max-batch = 4           all four test batches are executed by default
	test-cmd = test         the command used for test run is 'test' of 'containers.sh'
	arg1 = ''               this is the optional first (additional) argument used, e.g, for 'testsw'
	EOF
	exit 1
}


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

if [ "$1" = "help" ]; then
	print_help
fi

# by default does all tests
tests=${1:-'4'}
comm=${2:-'test'}
util=${3:-''}

# cleanup and prepare environment
rm log-rt-app-tst-*/log-thread1-0.log

testc () {
	# agruments $1 grp - $2 test count 
	
	local i=0
	while [ $i -lt $2 ]; do 
		./containers.sh quiet ${comm} ${util} $(seq -s " " ${1}0 $1$i )
		./containers.sh stop ${1}*
		mkdir -p log/${1}-$(($i+1))
		cp log/rt-app-tst-${1}* log/${1}-$(($i+1))
		mv log/orchestrator.txt log/${1}-$(($i+1))
		: $(( i+=1 ))			# loop increase
	done
}

if [ "$tests" -ge 1 ]; then
	testc 1 10 # test batch 1, < 1ms
fi
if [ "$tests" -ge 2 ]; then
	testc 2 2  # test batch 2, ~50%, 2.5ms
fi
if [ "$tests" -ge 3 ]; then
	testc 3 3  # test batch 3, mixed period <= 10ms
fi
if [ "$tests" -ge 4 ]; then
	testc 4 10 # test batch 4, industry sample case
fi
