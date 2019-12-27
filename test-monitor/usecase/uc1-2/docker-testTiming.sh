#!/bin/bash

#Initialization
local_resultdir="/opt/usecase/logs"
container_resultdir="/home/logs"

#Run workerapps
container_name="rt-workerapp"
numContainers=3

basecmdline="--innerloops 25 --outerloops 150 --dbg --timedloops 8 --instnum "


echo Killing all existing containers:
rm -rf /opt/logs/workerapp*.log
docker rm -f $(docker ps -f rt-workerapp)

echo ">>> Starting workerapps."

for (( i=0; i<num_containers; i++ ));do
    cmdline="$basecmdline $i"
    
    echo ">>> Running command: workerapp  $cmdline "
	docker run \
		-d \
		-e cmdline="$cmdline" \
		-v $local_resultdir:$container_resultdir \
		$container_name
done
sleep 1

container_name="rt_datadistributor"
cmdargs="--readpipe /tmp/source_1 --num 3 --dbg"
echo ">>> Running command: workerapp  $cmdargs "
docker run \
    -d
    -e cmdline="$cmdargs" \
    -v $local_resultdir:$container_resultdir \
    $container_name

container_name="rt_datagenerator"
cmdargs="--mininterval 40000 --maxinterval 40000 --sleeptimer --writepipe /tmp/source --dbg --num 1"
echo ">>> Running command: datagenerator $cmdargs"
docker run \
    -d
    -e cmdline="$cmdargs" \
    -v $local_resultdir:$container_resultdir \
    $container_name


