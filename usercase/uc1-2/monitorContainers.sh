#!/bin/bash
let n=0
while true; do
docker ps -a |egrep 'datagenerator|datadistributor|workerapp'|grep -v grep
echo
tail /tmp/fps
ls -ltr /tmp/{data,worker}*
echo "============== Pass $n ================"
let n=n+1
sleep 10
done

