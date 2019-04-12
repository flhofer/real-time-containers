#!/bin/bash

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

elif [[ "$cmd" == "run" ]]; then
# START ALL CONTAINERS OF GRP

	eval "mkdir log"

	for filename in rt-app-tst-${grp}*.json; do
		filen="${filename%%.*}"
		#create directory for log output and then symlink
		eval "mkdir log-${filen} && chown 1000:1000 log-${filen}"
		eval "ln -fs ../log-${filen}/log-thread1-0.log log/${filen}.log"
		# start new container
		eval "docker run -v ${PWD}/log-${filen}:/home/rtuser/log --cap-add=SYS_NICE -d --name ${filen} testcnt ${filename}"
	done

elif [[ "$cmd" == "start" ]] || [[ "$cmd" == "stop" ]] || [[ "$cmd" == "rm" ]]; then
# START ALL CONTAINERS OF GRP

	for filename in rt-app-tst-${grp}*.json; do
		filen="${filename%%.*}"
		eval "docker container ${cmd} ${filen}"
	done
elif [[ "$cmd" == "test" ]]; then # run a test procedure

	# remove old log file first 
	eval "rm log/orchestrator.txt"
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

	# start orchestrator and wait for termination
	eval ./schedstat.o -a 1 -b --policy=fifo -r 900 -nrt-app -P > log/orchestrator.txt
	eval "chown 1000:1000 log/*"
	# give notice about end
	echo "Test finished. Stop containers manually now if needed." 

else
	echo "Unknown command"
fi

