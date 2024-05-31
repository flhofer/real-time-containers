#!/bin/sh
card=${2:"enp2s0"}
if [ "$1" = "start" ]; then
  # run codesys vControl with additional TMP mapping for log output
  docker run --rm -td -v /tmp:/tmp -v /var/opt/codesysvcontrol/instances/runtime/conf/codesyscontrol:/conf/codesyscontrol/ -v /var/opt/codesysvcontrol/instances/runtime/data/codesyscontrol:/data/codesyscontrol/ --cap-add=ALL --hostname runtime --network=host --name runtime codesyscontrol_virtuallinux:4.11.0.0-b.trunk.170 -n ${card}
else
  docker stop runtime
fi

