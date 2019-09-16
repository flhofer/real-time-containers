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

##################### DETERMINE CLI PARAMETERS ##########################

if [ $# -eq 1 ]; then

cat <<EOF
Not enough arguments supplied!
Usag: ./container.sh command testgrp 
 or   ./container.sh test testgrp [testgrp] .. [testgrp] 
 or   ./container.sh (for defaults)

Defaults are:
command = start         command to apply to containers in group
testgrp = *		all test groups present are applied (once per json)
EOF
        exit 1
fi

cmd=${1:-'start'}
grp=${2:-'*'}


##################### EVALUATE COMMAND OUTPUT ##########################

if [[ "$cmd" == "build" ]]; then
# BUILD CONTAINER

	eval "docker build . -t testcnt"

elif [[ "$cmd" == "run" ]] || [[ "$cmd" == "create" ]]; then
# CREATE CONTAINERS OF GRP

	eval "mkdir log"
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

	# remove old log file first 
	eval "rm log/orchestrator.txt"

	# start orchestrator and wait for termination
	eval ./schedstat -f --policy=fifo > log/orchestrator.txt &
	sleep 10
	SPID=$(ps h -o pid -C schedstat)

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
	sleep 60

	# end orchestrator
	kill -SIGINT $SPID
	sleep 1

	# move stuff 
	eval "chown -R 1000:1000 log/*"

	# give notice about end
	echo "Test finished. Stop containers manually now if needed." 

else
	echo "Unknown command"
fi

