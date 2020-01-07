#!/bin/bash
#source ~user/.bashrc

#log output dir
launchDir="/opt/usecase/logs"
mkdir -p $launchDir

local_resultsDir="$launchDir/UC2.`date +%Y%m%d`"
container_resultsDir="/home/logs"

fifoDir=/tmp
fpsFile=$fifoDir/fps
 
base_resultsDir="$base_resultsDir"

###################
#chrt parameters for polling workers
#All chrt paramters are specified in nanoseconds
###################
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

######################
#chrt parameters for event-driven workers
######################
workerPolicyEvent="--fifo"
workerPriorityEvent=98

datageneratorPolicy="--fifo"
datageneratorPriority=99
datadistributorPolicy="--fifo"
datadistributorPriority=97

#REGULAR
#For executing the regular test, sleep 30 minutes between tests
#let testTime=30*60
#INITIAL
#For initial testing, sleep 2 minutes between tests
let testTime=30*60
#TEMPORARY
#For debugging, sleep 1 minute between tests
#let testTime=60

echo "local_resultsDir=$local_resultsDir; container_resultsDir=$container_resultsDir; fifoDir=$fifoDir"

# Test parameters
if [ $# -gt 0 ] ; then
    totalTests=$1
else
    totalTests=3
fi

echo "totalTests=$totalTests"

########################
# Function declarations
########################
stopAllContainers() {
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

killRemnantsFunc() {
    echo "killRemnantsFunc Current containers:"
    #docker ps -a |egrep 'datagenerator|datadistributor|workerapp' |grep -v 'grep'
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
    imageName=$1
    cmdargs="$2"
    progName=$3
    scheduling="$4"
    sch="$5"
    
    echo "Launching $imageName (executable $progName) with argument [$cmdargs] and scheduling [$scheduling]. local_resultsDir=$local_resultsDir "

    docker run \
	    -d \
	    -e cmdargs="$cmdargs" \
        -e scheduling="$scheduling" \
        -e sch="$sch"  \
	    -v $fifoDir:"$fifoDir"  \
	    -v "$local_resultsDir":"$container_resultsDir" \
	    --privileged \
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

##############################
# End of function declarations
# Start of main script commands
##############################

    #Initialization
mkdir $base_resultsDir 2>/dev/null
rm -rf $base_resultsDir/* 2>/dev/null

    #Cleanup
rm -f $fifoDir/{worker,data}* 2>/dev/null

cd $launchDir

killRemnantsFunc

for ((tNum=0; tNum<totalTests; tNum++)); do
    let mxWorkers=${tNum}+3
    runTest $tNum $mxWorkers
    echo "Completed test $tNum"
done

echo "Calling killRemnantsFunc"
killRemnantsFunc

echo Exiting
