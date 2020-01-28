#! /bin/bash

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

#logdirs based on local dir, safety
#log output dir
launchDir="logs"
mkdir -p ./$launchDir

local_resultsDir="logs" # Default for basic functions
container_resultsDir="/home/logs"

fifoDir=/tmp
fpsFile=$fifoDir/fps


######################
#chrt parameters for event-driven workers
######################
workerPolicyEvent="--fifo"
workerPriorityEvent=98

datageneratorPolicy="--fifo"
datageneratorPriority=99
datadistributorPolicy="--fifo"
datadistributorPriority=97

#TODO: FROM USECASE 1!!
#REGULAR
#For executing the regular test, sleep 30 minutes between FPS changes
#let sleepTime=30*60
#INITIAL
#For initial testing, sleep 2 minutes between FPS changes
let sleepTime=120
#TEMPORARY
#For debugging, sleep 1 minute between FPS changes
#let sleepTime=60
#Sleep 30 seconds between start of new FPS and launch of new worker
let beforeNewWorkerSleepTime=30

echo "local_resultsDir=$local_resultsDir; container_resultsDir=$container_resultsDir; fifoDir=$fifoDir"
echo

############################### Functions, generic ####################################

function printUsage() {

	cat <<EOF

Usage: $0 test [number] [noworkers/notests]
 or    $0 [prepcmd] [noworkers]

where:

number		use case numbers to execute
prepcmd		prep command to execute: [build, stopall, killall, timing, monitor]

Defaults are:
number = 1		execute use case 1 only
noworkers = 8   use case 1 max 8 workers
nooftests = 3	use case 2 number of tests 3

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

exitFunc() {
    echo
    echo "Executing exitFunc.  exitCode=$1"
    exitCode=$1
    killRemnantsFunc
    echo "Exiting with exit code $exitCode"
    echo
    exit $exitCode
}

startContainer() {
    #Argument 1 is image name
    #Argument 2 is arguments
    #Argument 3 is progname within the image
    #Argument 4 is scheduling parameters
    #Argument 5 is -dash for log file
    imageName=$1
    cmdargs="$2"
    progName=$3
    scheduling="$4"
    sch=${5:-''}

    echo "Launching $imageName (executable $progName) with argument [$cmdargs] and scheduling [$scheduling]. local_resultsDir=$local_resultsDir "

    # this should work for all users in the docker group! TODO: verify
    docker run \
	    -d \
	    -e cmdargs="$cmdargs" \
        -e scheduling="$scheduling"   \
        -e sch="$sch"  \
	    -v $fifoDir:"$fifoDir"  \
	    -v "$PWD/$local_resultsDir":"$container_resultsDir" \
	    --cap-add=sys_nice \
        --name $imageName \
	    $imageName

    sleep 1

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
############################### cmd specific func ####################################

#UC1-Timing
# same as Workerpolling +- for uc1 and timing test
startWorkerContainer() {
    #Argument 1 is instance number
    #Argument 2 is outer loops, defaults to 10
    #Argument 3 is timed loops, defaults to 500
    #Argument 4- eventual additional parameters
    i=$1
    oul=${2:-'10'}
    tdl=${3:-'500'}

    scheduling="$workerPolicyEvent $workerPriorityEvent"
    if [ $i -lt $maxworkers ] ; then
        baseworkerImage=rt-workerapp
        imageName=${baseworkerImage}$i
        #maxtests not defined for timing tests
        cmdargs="--instnum $i --innerloops 25 --outerloops $oul --maxTests 6 --timedloops $tdl --basePipeName $fifoDir/worker ${@:4}"
        startContainer $imageName "$cmdargs" workerapp$i "$scheduling"
    else
    	echo "ERROR: max number of workers reached"
    fi
}

#UC1
startNewTest() {
    #Argument 1 is new FPS
    #Argument 2 is new worker instance
    newfps=$1
    newInstance=$2
    if [ $newInstance -lt $maxworkers ] ; then
        echo "Setting FPS = $newfps"
        echo $newfps >> $fpsFile
        echo "Sleeping for $beforeNewWorkerSleepTime seconds before launching new worker"
        sleep $beforeNewWorkerSleepTime
        startWorkerContainer $newInstance
    else
    	echo "Exiting rather than starting worker $newInstance: maxworkers=$maxworkers"
        let newfps=-2
        echo "Setting FPS = $newfps"
        echo "$newfps" >> $fpsFile

        echo "Sleeping for 2 minutes "
        sleep 120

        echo "Calling killRemnantsFunc"
        killRemnantsFunc
        echo
        echo Exiting
	    exit 0
    fi
}

#UC2

startFIFOorRRContainer() {
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
    sch="$6"

    scheduling="$policy $priority"
    echo "startFIFOorRRContainer: scheduling=[$scheduling]"
    startContainer $imageName "$cmdargs" $progName "$scheduling" "$sch"
}
    
startDeadlineContainer() {
    #Argument 1 is image name
    #Argument 2 is arguments
    #Argument 3 is progname within the image
    #Argument 4 is period in nanoseconds
    imageName=$1
    cmdargs="$2"
    progName=$3
    runtime="$4"
    period=$5
    deadline=$6
    sch="$7"
    
    echo "startDeadlineContainer: imageName=[$1], progName=[$progName] runtime=$runtime, period=$period, deadline=$deadline"

    # for deadline policy, must specify priority 0 (the last paramter in scheduling)
    #  MIT specifying the sched-deadline causes chrt to fail! 
    #scheduling="--deadline --sched-runtime $runtime --sched-period $period --sched-deadline $deadline 0 "   
    scheduling="--deadline --sched-runtime $runtime --sched-period $period --sched-deadline $deadline 0 "   
    echo "                        scheduling=[ $scheduling ]"

    startContainer $imageName "$cmdargs" $progName "$scheduling" "$7"
}

startWorkerEventDriven() {
    #Argument 1 is instance number
    i=$1
    echo "startWorkerEventDriven: i=$i"
    baseworkerImage=rt-workerapp
    imageName=${baseworkerImage}$i
    progName=workerapp${i}
    cmdargs="--instnum $i --innerloops 25 --outerloops 10 --maxTests 1 --timedloops 500 --basePipeName $fifoDir/worker --endInSeconds $testTime"
    startFIFOorRRContainer $imageName "$cmdargs" workerapp$i "$workerPolicyEvent" $workerPriorityEvent "-fifo"
}
    
startWorkerPolling() {
    #Argument 1 is instance number
    #Argument 2 is polling period
    i=$1
    pollingPeriod=$2

    baseworkerImage=rt-workerapp
    imageName=${baseworkerImage}$i
    progName=workerapp${i}
    cmdargs="--instnum $i --innerloops 25 --outerloops 10 --maxTests 1 --timedloops 50 --basePipeName $fifoDir/polling --pollPeriod $pollingPeriod --dline $workerDeadlinePolling --rtime $workerRuntimePolling --endInSeconds $testTime"

    startDeadlineContainer $imageName "$cmdargs" workerapp$i $workerRuntimePolling $pollingPeriod $workerDeadlinePolling "-deadline"
}

runTest() {
    #argument 1 = test number
    #argument 2 = max workers
    
    testNum=$1
    maxWorkers=$2
    local_resultsDir=$base_resultsDir/Test${testNum}
    mkdir $local_resultsDir

    echo 
    echo "****************************************************************************************************"
    echo "*** Starting test $testNum with $maxWorkers event-driven workers and $maxWorkers polling workers ****"
    #Launch polling workers
    for (( ip=0 ; ip<maxWorkers; ip++ )); do
        workerPeriodPollingParamName="worker${ip}PeriodPolling"
        workerPeriodPolling="${!workerPeriodPollingParamName}"
        echo "Calling startWorkerPolling $ip $workerPeriodPolling "
        startWorkerPolling $ip $workerPeriodPolling
    done

    # #Launch event-driven workers
    # for (( iw=0 ; iw<maxWorkers; iw++ )); do
    #     let instance=5+$iw
    #     echo "Calling startWorkerEventDriven $instance"
    #     startWorkerEventDriven $instance 
    # done


    # #Launch Event-driver datagenerator
    # cmdargs=" --generator 2 --maxTests 1 --maxWritePipes $maxWorkers --baseWritePipeName $fifoDir/worker --threaded --endInSeconds $testTime"
    # startFIFOorRRContainer rt-datagenerator "$cmdargs" datagenerator "$datageneratorPolicy" $datageneratorPriority "-fifo"
    
    #Launch Polling-driver datagenerator
    cmdargs=" --generator 2 --maxTests 1 --maxWritePipes 1  --baseWritePipeName $fifoDir/polling --endInSeconds $testTime"
    startFIFOorRRContainer rt-datagenerator "$cmdargs" datagenerator "$datageneratorPolicy" $datageneratorPriority "-deadline"

    echo "Sleeping for $testTime seconds"
    sleep $testTime

    stopAllContainers
    echo
}

############################### cmd specific exec ####################################

if [ $# -lt 1 ]; then
	echo "Not enough arguments supplied!"
	printUsage
fi

cmd=${1:-'test'}
shift	# restore original parameters

if [[ $cmd == "build" ]] ; then

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

	rm -f ./$local_resultsDir/workerapp*.log # do not use -r!!!
	killRemnantsFunc

	echo ">>> Starting workerapps."

	for (( i=0; i<num_containers; i++ ));do
	    startWorkerContainer $i 150 10 --dbg
	done

	#Launch datadistributor
	cmdargs="--generator 0 --maxTests 6 --maxWritePipes 8 --baseWritePipeName $fifoDir/worker --readpipe $fifoDir/datadistributor_0 --dbg"
	echo ">>> Running command: datadistributor $cmdargs"
#	cmdargs="--readpipe /tmp/source_1 --num 3 --dbg"
	startContainer rt-datadistributor "$cmdargs" datadistributor "$datadistributorPolicy" $datadistributorPriority

	#Launch datagenerator
	cmdargs=" --generator 1 --maxTests 6 --maxWritePipes 1 --baseWritePipeName $fifoDir/datadistributor --dbg"
	echo ">>> Running command: datagenerator $cmdargs"
#	cmdargs="--mininterval 40000 --maxinterval 40000 --sleeptimer --writepipe /tmp/source --dbg --num 1"
	startContainer rt-datagenerator "$cmdargs" datagenerator "$datageneratorPolicy" $datageneratorPriority

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

elif [[ $cmd == "test" ]]; then
	tno=${1:-'1'}
	shift

	local_resultsDir="$launchDir/UC$tno.`date +%Y%m%d`"
	echo "UPDATE: local_resultsDir=$local_resultsDir; container_resultsDir=$container_resultsDir; fifoDir=$fifoDir"

	#stop if probability is too high that the folder might refer to root
	if [[ ${#local_resultsDir} -le 3 ]]; then
		echo "Error, log dir name too short. dangerous!"
		exit
	fi

	#Initialization
	mkdir -p ./$local_resultsDir 2>/dev/null
	rm -f ./$local_resultsDir/* 2>/dev/null

	rm -f $fifoDir/{worker,data}* 2>/dev/null

#	cd ./$launchDir
	killRemnantsFunc

	if [[ $tno == 1 ]]; then

		#Cleanup
		rm -f $fpsFile 2>/dev/null
		touch $fpsFile

		# update parameters for tests
		if [ $# -gt 0 ] ; then
			maxworkers=$1
		else
			maxworkers=8
		fi
		echo "maxworkers=$maxworkers"

		#Launch first 3 workers
		for (( i=0; i<3; i++ )); do
		    startWorkerContainer $i
		done

		#Launch datadistributor
		cmdargs="--generator 0 --maxTests 6 --maxWritePipes 8 --baseWritePipeName $fifoDir/worker --readpipe $fifoDir/datadistributor_0 "
		startContainer rt-datadistributor "$cmdargs" datadistributor "$datadistributorPolicy $datadistributorPriority"

		#Launch datagenerator
		cmdargs=" --generator 1 --maxTests 6 --maxWritePipes 1 --baseWritePipeName $fifoDir/datadistributor "
		startContainer rt-datagenerator "$cmdargs" datagenerator "$datageneratorPolicy $datageneratorPriority"

		echo "Sleeping for 30 seconds"
		sleep 30

		#Set initial FPS=24
		let fps=24
		echo "Set initial FPS = $fps"
		echo $fps >>$fpsFile

		i=3 # start with worker 4
		while [ $i -lt 8 ];
		do
		    echo "Sleeping for $sleepTime seconds"
		    sleep $sleepTime
		    let fps=${fps}+8
		    startNewTest $fps $i
		    let i++;
		done

		# closing up
		let fps=-2
		echo "Setting FPS = $fps"
		echo "$fps" >>$fpsFile

		echo "Sleeping for 2 minutes "
		sleep 120

	elif [[ $tno == 2 ]]; then
		# store base dir to allow subdirectories test change
		base_resultsDir="$local_resultsDir"
		# same config as for UC1, but test instead of sleep time. TODO To expand	
		testTime=${sleepTime}

		###################
		#chrt parameters for polling workers
		#All chrt paramters are specified in nanoseconds
		###################
		# parameters for test 2
		workerPolicyPolling="--deadline"
		# Allow 100 msecs for runtime
		workerRuntimePolling=175000000

		# Each polling task should resume within 1 msec of the start of its next period
		workerDeadlinePolling=300000000

		#MIT: This is a guess as to the polling periods desired: 1 second, 750 msecs, 500 msecs, 250 msecs, 150 msecs 
		worker0PeriodPolling=800000000
		worker1PeriodPolling=700000000
		worker2PeriodPolling=600000000
		worker3PeriodPolling=550000000
		worker4PeriodPolling=500000000

		# Test parameters
		if [ $# -gt 0 ] ; then
		    totalTests=$1
		else
		    totalTests=3
		fi
		echo "totalTests=$totalTests"

		for ((tNum=0; tNum<totalTests; tNum++)); do
		    let mxWorkers=${tNum}+3
		    runTest $tNum $mxWorkers
		    echo "Completed test $tNum"
		done
	fi

	echo "Calling killRemnantsFunc"
	killRemnantsFunc

	echo "Exiting.."

else
	echo "Unknown command!!"
	printUsage
fi
