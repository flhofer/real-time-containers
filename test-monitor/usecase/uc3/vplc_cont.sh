#!/bin/sh
card=${2:-"eth0"}
name=${3:-"runtime"}
if [ "$1" = "start" ]; then
  # run codesys vControl with additional TMP mapping for log output
  docker run --rm -td -v /tmp:/tmp -v /var/opt/codesysvcontrol/instances/${name}/conf/codesyscontrol:/conf/codesyscontrol/ -v /var/opt/codesysvcontrol/instances/${name}/data/codesyscontrol:/data/codesyscontrol/ --cap-add=IPC_LOCK --cap-add=NET_ADMIN --cap-add=NET_BROADCAST --cap-add=SETFCAP --cap-add=SYS_ADMIN --cap-add=SYS_MODULE --cap-add=SYS_NICE --cap-add=SYS_PTRACE --cap-add=SYS_RAWIO --cap-add=SYS_RESOURCE --cap-add=SYS_TIME --hostname ${name} --name ${name} codesyscontrol_virtuallinux:4.11.0.0-b.trunk.170 -n ${card}
else
  docker stop ${name}
fi

