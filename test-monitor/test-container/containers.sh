#!/bin/sh

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
	cat <<-EOF
	Usage: $0 command testgrp [testgrp] .. [testgrp] 
	 or    $0 build
	 or    $0 update [calibration-value]
	 or    $0 (for defaults)

	Defaults are:
	command = start         command to apply to containers in group
	testgrp = *             all test groups present are applied (once per json)

	Commands:
	help                    this screen
	build                   build the container image with rt-app (see Dockerfile)
	run                     run container specified from built image
	start                   start container after it has been stopped
	stop                    stop container after it has been stopped
	rm                      remove container
	test                    test run for container (with parallel execution of 'orchestrator')
	testsw 	                like test, with additional software to be tested, parameter 1 in quotes ""
	testcontainer           like test, with additional container to be tested, parameter 1
	update                  update container calibration value with the value specified as argument (if given) and copy to containers
	EOF
	exit 1
}

##################### DETERMINE CLI PARAMETERS ##########################

if [ ! "$1" = "build" ] && [ ! "$1" = "update" ] && [ ! "$1" = "help" ] && [ $# -eq 1 ]; then
	echo "Not enough arguments supplied!"
	print_help
fi

cmd=${1:-'help'}

##################### EVALUATE COMMAND OUTPUT ##########################

if [ "$cmd" = "help" ]; then
	print_help

elif [ "$cmd" = "build" ]; then
# BUILD CONTAINER
	echo "Build containers"

	eval "docker build . -t testcnt"

elif [ "$cmd" = "run" ] || [ "$cmd" = "create" ]; then
# CREATE CONTAINERS OF GRP
	echo "Run containers"

	eval "mkdir -p log"
	eval "chown 1000:1000 log"

	while [ "$2" != "" ]; do
		# all matching files

		for filename in rt-app-tst-${2:-'*'}*.json; do
			filen="${filename%%.*}"
			#create directory for log output and then symlink
			eval "mkdir log-${filen} && chown -R 1000:1000 log-${filen}"
			eval "ln -fs ../log-${filen}/log-thread1-0.log log/${filen}.log"
			# start new container
			eval "docker ${cmd} -v ${PWD}/log-${filen}:/home/rtuser/log --cap-add=SYS_NICE --cap-add=IPC_LOCK -d --name ${filen} testcnt ${filename}"
		done

	    # Shift all the parameters down by one
	    shift
	done

elif [ "$cmd" = "start" ] || [ "$cmd" = "stop" ] || [ "$cmd" = "rm" ]; then
# APPLY CMD TO ALL CONTAINERS OF GRP
	echo "Start containers"

	while [ "$2" != "" ]; do
		# all matching files

		for filename in rt-app-tst-${2:-'*'}*.json; do
			filen="${filename%%.*}"
			eval "docker container ${cmd} ${filen}"
		done

	    # Shift all the parameters down by one
	    shift
	done

elif [ "$cmd" = "test" ]; then # run a test procedure

	echo "Starting 15 min tests"
	# remove old log file first 
	eval "rm log/orchestrator.txt"

	# start orchestrator and wait for termination
	eval ./orchestrator -df --policy=fifo > log/orchestrator.txt &
	sleep 10
	SPID=$(ps h -o pid -C orchestrator)

	# start containers -> test group
	while [ "$2" != "" ]; do
		# start all matching files
		for filename in rt-app-tst-${2:-'*'}*.json; do
			filen="${filename%%.*}"
			# remove old log file first 
			eval "rm log-${filen}/log-thread1-0.log"
			eval "docker container start ${filen}"
		done

	    # Shift all the parameters down by one
	    shift
	done
	sleep 900

	# end orchestrator
	kill -SIGINT $SPID
	sleep 1

	# move stuff 
	eval "chown -R 1000:1000 log/*"

	# give notice about end
	echo "Test finished. Stop containers manually now if needed." 

elif [ "$cmd" = "update" ]; then
# UPDATE ALL CONTAINERS CONFIG
	if [ "$2" != "" ]; then
		eval "sed -i \"/calibration/{s/:.*,/:\ ${2},/}\" rt-app-[0-9]*-*.json"
	fi
	for filename in rt-app-tst-*.json; do 
		filen="${filename%%.*}";

		#update links 
		for i in rt-app-tst-*.json; do 
			eval "docker cp $i $filen:/home/rtuser" 
			echo $i; 
		done

		#update origs
		for i in rt-app-[0-9]*-*.json; do 
			eval "docker cp $i $filen:/home/rtuser"
			echo $i; 
		done 
	done 
elif [ "$cmd" = "testsw" ]; then
# RUN TEST COMMAND AND THEN START test		

	# $1 testsw, $2 program to run, save and remove from args
	sw=$2
	shift 2

	# set sw as $@ list and extract first item $1, then restore (posix compatible)
	mainargs="$*"
	set -- $sw 
	binary=$1
 	set -- $mainargs

	# start software and store pid
	eval $sw &
	sleep 2
	SWPID=$(ps h -o pid -C $binary )
	
	# run recursive as test
	eval $0 test $@
	
	# end test software
	kill -SIGINT $SWPID
	sleep 1
elif [ "$cmd" = "testcontainer" ]; then
# RUN TEST CONTAINER AND THEN START test		

	# $1 container, $2 container to run, save and remove from args
	cont=$2
	shift 2
	
	eval "docker container start ${cont}"
	sleep 2
	
	# run recursive as test
	eval $0 test $@

	sleep 2

	eval "docker container stop ${cont}"

	sleep 1
else
	echo "Unknown command"
fi


