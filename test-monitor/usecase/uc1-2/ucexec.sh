#/bin/bash

if [[ ! "$1" == "quiet" ]]; then

cat <<EOF

######################################

use case preparation and tests
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

############################### global variables ####################################

#Initialization

#log output dir
launchDir="./logs"
mkdir -p $launchDir

local_resultdir="./logs" # TODO: change for UCs
container_resultdir="/home/logs"

fifoDir=/tmp
fpsFile=$fifoDir/fps

workerPolicy="--fifo"
workerPriority=99
datageneratorPolicy="--fifo"
datageneratorPriority=98
datadistributorPolicy="--fifo"
datadistributorPriority=97

############################### Functions, generic ####################################

function printUsage() {

	cat <<EOF

Usage: $0 test [number]
 or    $0 [prepcmd]

where:

number		use case numbers to execute
prepcmd		prep command to execute: [build, stopall, killall, timing, monitor]

Defaults are:
number = 1		execute use case 1 only


EOF
	exit 1	
}

killRemnantsFunc() {
    echo "killRemnantsFunc Current containers:"
    echo

    if docker ps -a |egrep 'datagenerator|datadistributor|workerapp' |grep -v 'grep' ; then
        echo "Stop and Remove all existing containers:"
        docker rm $(docker stop $(docker ps -a | egrep -e 'datagenerator|datadistributor|workerapp' | awk '{print $1}'))
        echo "Still live:"
        docker ps -a |egrep 'datagenerator|datadistributor|workerapp' |egrep -v 'Exited|grep'
    else
        echo "No remnants to remove"
    fi
    echo
}

stopAllContainers() {
    echo "stopAllContainers Current containers:"
    echo

   if docker ps -a |egrep 'datagenerator|datadistributor|workerapp' | grep -v 'grep' ; then
        gracePeriod=60
        echo "stopAllContainers(): Stop all existing containers with grace period $gracePeriod seconds"
        docker stop $(docker ps -a | egrep -e 'datagenerator|datadistributor|workerapp' | awk '{print $1}') -t $gracePeriod
        let sleepSecs=${gracePeriod}+5
        echo "for $sleepSecs seconds"
        sleep $sleepSecs
        killRemnantsFunc
    else
        echo "stopAllContainers(): No running containers"
    fi
}

startContainer() {
    #Argument 1 is image name
    #Argument 2 is arguments
    #Argument 3 is progname within the image
    #Argument 4 is policy
    #Argument 5 is priority
    imageName=$1
    cmdargs="$2"
    progName=$3
    policy="$4"
    priority=$5

    echo "Launching $imageName with argument [$cmdargs] "

    scheduling="$policy $priority"
    docker run \
	    -d \
	    -e cmdargs="$cmdargs" \
        -e scheduling="$scheduling"   \
        -e sch=""  \
	    -v $fifoDir:"$fifoDir"  \
	    -v "$local_resultsDir":"$container_resultsDir" \
	    --privileged \
        --name $imageName \
	    $imageName

    while (true) ; do
      if ps -elf |grep $progName |grep -v grep; then
        echo "Launch of $imageName Succeeded"
        echo
        break;
     fi

     let tries=$tries+1
     if [ $tries -gt 15 ]; then
        # $imageName not found on try $tries
        echo "Launch of $imageName FAILED - $progName not found in ps output after $tries tries"
        echo
        exitFunc -1
     fi
   done
}

startWorkerContainer() {
    #Argument 1 is instance number
    #Argument 2 is outer loops, defaults to 10
    #Argument 3 is timed loops, defaults to 500
    #Argument 4- eventual additional parameters
    i=$1
    oul=${2:-'10'}
    tdl=${2:-'500'}
    if [ $i -lt $maxworkers ] ; then
        baseworkerImage=rt-workerapp
        imageName=${baseworkerImage}$i
        progName=workerapp${i}
        cmdargs="--instnum $i --innerloops 25 --outerloops $oul --maxTests 6 --timedloops $tdl --basePipeName $fifoDir/worker ${@:4}"
        startContainer $imageName "$cmdargs" workerapp$i "$workerPolicy" $workerPriority
    fi
}

############################### cmd specific exec ####################################

cmd=${1:-'test'}

if [ $# -ne 1 ]; then
	echo "Not enough arguments supplied!"
	printUsage
fi


if [[ $cmd == "build" ]] ; then
	shift	# restore original parameters

	srcDir=./src
	dockerDir=./docker
	cd $srcDir

	# Build binaries and copy them to the docker build directory
	make
	cp -f WorkerApp "../$dockerDir/workerapp"
	cp -f DataGenerator "../$dockerDir/datagenerator"

	cd "../$dockerDir"

	echo "executables:"
	ls -l datagenerator workerapp
	echo

	# Stop containers and cleanup
	echo "Stop all running workerapp containers"
	stopAllContainers

	for i in {0..9}
	do
	    docker image rm -f rt-workerapp$i
	done

	docker image rm -f rt-datagenerator
	docker image rm -f rt-datadistributor
	docker system prune -f

	echo "Docker images before build:"
	docker images
	echo

	# Rebuild all docker images for the use case
	for i in {0..9}
	do
	    docker build -f ./Dockerfile.wa$i -t rt-workerapp$i .
	done

	docker build -f ./Dockerfile.dg -t rt-datagenerator .
	docker build -f ./Dockerfile.dd -t rt-datadistributor .

	echo "Docker images after build:"
	docker images

	cd ..
elif [[ $cmd == "stopall" ]]; then

	stopAllContainers
	echo "Done"

elif [[ $cmd == "killall" ]]; then

	killRemnantsFunc
	echo "Done"

elif [[ $cmd == "timing" ]]; then

	#Run workerapps
	numContainers=3

	echo "Killing all existing containers:"
	rm -f $local_resultdir/workerapp*.log # do not use -r!!!
	killRemnantsFunc

	echo ">>> Starting workerapps."

	for (( i=0; i<num_containers; i++ ));do
	    startWorkerContainer $i 150 10 --dbg
	done

	#Launch datadistributor
	cmdargs="--generator 0 --maxTests 6 --maxWritePipes 8 --baseWritePipeName $fifoDir/worker --readpipe $fifoDir/datadistributor_0 "
	startContainer rt-datadistributor "$cmdargs" datadistributor "$datadistributorPolicy" $datadistributorPriority

	#Launch datagenerator
	cmdargs=" --generator 1 --maxTests 6 --maxWritePipes 1 --baseWritePipeName $fifoDir/datadistributor "
	startContainer rt-datagenerator "$cmdargs" datagenerator "$datageneratorPolicy" $datageneratorPriority

	echo "Sleeping for 30 seconds"
	sleep 30

	container_name="rt_datadistributor"
	cmdargs="--readpipe /tmp/source_1 --num 3 --dbg"
	echo ">>> Running command: workerapp  $cmdargs "
	docker run \
	    -d
	    -e cmdargs="$cmdargs" \
	    -v $local_resultdir:$container_resultdir \
	    $container_name

	container_name="rt_datagenerator"
	cmdargs="--mininterval 40000 --maxinterval 40000 --sleeptimer --writepipe /tmp/source --dbg --num 1"
	echo ">>> Running command: datagenerator $cmdargs"
	echo "docker run \
	    -d \
-e scheduling="$scheduling"   \
        -e cmdargs="$cmdargs" \
	    -v $local_resultdir:$container_resultdir \
	    $container_name"

elif [[ $cmd == "monitor" ]]; then
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
else
	echo "Unknown command!!"
	printUsage
fi
