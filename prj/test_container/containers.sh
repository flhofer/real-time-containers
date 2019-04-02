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

if [ $# -gt 2 ] && [ $# -ne 0 ]; then

cat <<EOF
Not enough arguments supplied!
Usag: ./container.sh command testgrp 
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

	for filename in rt-app-tst-${grp}*.json; do
		filen="${filename%%.*}"
		eval "docker run --cap-add=SYS_NICE -d --name ${filen} testcnt ${filename}"
	done

elif [[ "$cmd" == "start" ]] || [[ "$cmd" == "stop" ]] || [[ "$cmd" == "rm" ]]; then
# START ALL CONTAINERS OF GRP

	for filename in rt-app-tst-${grp}*.json; do
		filen="${filename%%.*}"
		eval "docker container ${cmd} ${filen}"
	done
else
	echo "Unknown command"
fi

