#!/bin/bash

VPLCDIR="/var/opt/codesysvcontrol/instances/"

rm -r /tmp/codesyscontrol*

for i in {1..10}; do
	rm -r ${VPLCDIR}runtime${i}
	if [ -z "$1" ]; then
	  cp -r ${VPLCDIR}runtime/ ${VPLCDIR}runtime${i}/
	  cp -r ${VPLCDIR}runtime/data/codesyscontrol/ /tmp/codesyscontrol${i} 
	  rm -r ${VPLCDIR}runtime${i}/data/codesyscontrol
	  ln -s /tmp/codesyscontrol${i} ${VPLCDIR}runtime${i}/data/codesyscontrol
	fi
done

