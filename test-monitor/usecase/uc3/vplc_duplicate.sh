#!/bin/bash

if [ ! "$1" = "quiet" ]; then
	cat <<-EOF

	######################################

	Test container duplication
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

#select origin runtime container
runtime=${1:-"runtime"}

VPLCDIR="/var/opt/codesysvcontrol/instances/"

rm -r /tmp/codesyscontrol*

rm ${VPLCDIR}${runtime}/data/codesyscontrol/*.log

for i in {1..10}; do
	rm -r ${VPLCDIR}runtime${i}
	if [ -z "$2" ]; then
	  mkdir -p ${VPLCDIR}runtime${i}/
	  mkdir -p ${VPLCDIR}runtime${i}/data/
	  cp ${VPLCDIR}${runtime}/VirtualInstance.cfg ${VPLCDIR}runtime${i}/
	  cp -r ${VPLCDIR}${runtime}/data/codesyscontrol/ /tmp/codesyscontrol${i} 
	  ln -s ${VPLCDIR}${runtime}/conf/ ${VPLCDIR}runtime${i}/conf
	  ln -s /tmp/codesyscontrol${i} ${VPLCDIR}runtime${i}/data/codesyscontrol
	  sed -i -e '/var/s/\/'${runtime}'\//\/runtime'${i}'\//g' ${VPLCDIR}runtime${i}/VirtualInstance.cfg 
	  sed -i -e '/Hostname/s/'${runtime}'/runtime'${i}'/' ${VPLCDIR}runtime${i}/VirtualInstance.cfg 
	fi
done

