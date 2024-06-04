#!/bin/sh
card=${2:-"eth0"}
if [ "$1" = "start" ]; then
  # run codesys vControl with additional TMP mapping for log output
  docker run --rm -td -v /tmp:/tmp -v /var/opt/codesysvcontrol/instances/runtime/conf/codesyscontrol:/conf/codesyscontrol/ -v /var/opt/codesysvcontrol/instances/runtime/data/codesyscontrol:/data/codesyscontrol/ --cap-add=cap_ipc_lock --cap-add=cap_net_admin --cap-add=cap_net_broadcast --cap-add=cap_setfcap --cap-add=cap_sys_admin --cap-add=cap_sys_module --cap-add=cap_sys_nice --cap-add=cap_sys_ptrace --cap-add=cap_sys_rawio --cap-add=cap_sys_resource --cap-add=cap_sys_time --hostname runtime --name runtime codesyscontrol_virtuallinux:4.11.0.0-b.trunk.170 -n ${card}
else
  docker stop runtime
fi

