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

function printhelp () {
cat <<EOF
Not enough arguments supplied!
Usag: ./container.sh command testgrp 
 or   ./container.sh test testgrp [testgrp] .. [testgrp] 
 or   ./container.sh (for defaults)

Defaults are:
command = start         command to apply to containers in group
testgrp = *             all test groups present are applied (once per json)

Commands:
help                    this screen
build                   build the containers for all JSON test files
run                     run container specified
start                   start container after it has been stopped
stop                    stop container after it has been stopped
rm                      remove container
test                    test run for container (parallel execution of orchestrator)
update                  update container calibration value
EOF
        exit 1
}

##################### DETERMINE CLI PARAMETERS ##########################

if [ $# -eq 1 ]; then
	printhelp
fi

cmd=${1:-'help'}
grp=${2:-'*'}

##################### EVALUATE COMMAND OUTPUT ##########################

if [[ "$cmd" == "help" ]]; then
	printhelp

elif [[ "$cmd" == "build" ]]; then
# BUILD CONTAINER
	echo "Build containers"

	eval "docker build . -t testcnt"

elif [[ "$cmd" == "run" ]] || [[ "$cmd" == "create" ]]; then
# CREATE CONTAINERS OF GRP
	echo "Run containers"

	eval "mkdir -p log"
	eval "chown 1000:1000 log"

	while [ "$2" != "" ]; do
		# all matching files

		for filename in rt-app-tst-${grp}*.json; do
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

elif [[ "$cmd" == "start" ]] || [[ "$cmd" == "stop" ]] || [[ "$cmd" == "rm" ]]; then
# APPLY CMD TO ALL CONTAINERS OF GRP
	echo "Start containers"

	while [ "$2" != "" ]; do
		# all matching files

		for filename in rt-app-tst-${grp}*.json; do
			filen="${filename%%.*}"
			eval "docker container ${cmd} ${filen}"
		done

	    # Shift all the parameters down by one
	    shift
	done

elif [[ "$cmd" == "test" ]]; then # run a test procedure

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

elif [[ "$cmd" == "update" ]]; then
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
else
	echo "Unknown command"
fi


