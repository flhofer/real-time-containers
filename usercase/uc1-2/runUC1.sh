#!/bin/bash
source ~user/.bashrc

if [ $# -gt 0 ] ; then
	maxWorkers=$1
else
	maxWorkers=8
fi
echo "maxWorkers=$maxWorkers"

########################
# Function declarations
########################
killRemnantsFunc() {
    #echo "Length of pidList is ${#pidList}"

    pidList=` ps -e -o pid,cmd |egrep -e 'Worker|DataGenerator|DataDistributor' |grep -v grep`

    if [ ${#pidList} -gt 0 ] ; then
        echo "Kill these remnants: "
        echo "$pidList"
    
        pidList=` ps -e -o pid,cmd |egrep -e 'Worker|DataGenerator|DataDistributor' |grep -v grep|cut -d ' ' -f 2`
    
        kill -9 $pidList 2>/dev/null
        echo "Still live:"
    
        pidList=` ps -e -o pid,cmd |egrep -e 'Worker|DataGenerator|DataDistributor' |grep -v grep`
    
        echo "$pidList"
    else
        echo "No remnants to kill"
    fi
    echo
}

exitFunc() {
    echo
    echo "Executing exitFunc.  exitCode=$1"
    exitCode=$1
    killRemnantsFunc
    jobList=`jobs -p`
    if [ ${#jobList} > 0 ] ; then
      for i in $jobList ;do
        echo "Killing background job - pid is $i"
        kill -9 $i
      done
    fi
    echo "After kill:"
    ps -e -o pid,cmd |egrep -e 'Worker|DataGenerator|DataDistributor' |grep -v grep
    echo "Exiting with exit code $exitCode"
    echo
    exit $exitCode
}

startExe() {
    #Argument 1 is program name
    #Argument 2 is arguments
    #Argument 3 is priority
    progName=$1
    cmdline="$2"
    priority=$3
    echo "Launching $progName with argument [$cmdline] >$resultsDir/$progName.log 2>&1 &"
    ./$progName $cmdline >$resultsDir/$progName.log 2>&1 &
    pid=$!
    echo "Rescheduling $progName (pid $pid) with priority $priority"
    chrt -p -f $priority $pid
    let tries=0
    while (true) ; do     
      if ps -elf |grep $progName |grep -v grep; then
        echo "Launch of $progName Succeeded"
        echo
        break;
     fi

     let tries=$tries+1
     if [ $tries -gt 15 ]; then
        # $progName not found on try $tries
        echo "Launch of $progName FAILED - not found in ps output after $tries tries"
        echo
        exitFunc -1
     fi
   done
}

startWorkerFunc() {
    #Argument 1 is instance number
    i=$1
    if [ $i -lt $maxWorkers ] ; then
        baseWorkerProg=WorkerApp
        progName=${baseWorkerProg}_$i
        cp -f ./$baseWorkerProg ./$progName 2>/dev/null
        cmdline="--instnum $i --innerloops 25 --outerloops 150 --maxTests 6 --timedloops 500  "
        priority=98
        startExe $progName "$cmdline" $priority
    else
    	echo "Exiting rather than starting Worker $i: maxWorkers=$maxWorkers"
        let fps=-2
        echo "Setting FPS = $fps"
        echo "$fps" >>$fpsFile
        
        echo "Sleeping for 2 minutes "
        sleep 120 
        
        echo "Calling killRemnantsFunc"
        killRemnantsFunc
        pidList=` ps -e -o pid,cmd |egrep -e 'Worker|DataGenerator|DataDistributor' |grep -v grep`
        echo "Still live: "
        echo "$pidList"
        
        echo Exiting
	exit 0
    fi
}
    
##############################
# End of function declarations
# Start of main script commands
##############################
cd $usecaseDir
echo "Current directory is `pwd`"
resultsDir="./`date +%Y%m%d`"
mkdir $resultsDir 2>/dev/null

killRemnantsFunc

fpsFile=/tmp/fps
rm -f $fpsFile 2>&1
touch $fpsFile

rm -f /tmp/Worker* /tmp/Data* 2>/dev/null

for (( i=0; i<3; i++ )); do
    startWorkerFunc $i 
done

distributorProg=DataDistributor
generatorProg=DataGenerator
cp -f $generatorProg $distributorProg 2> /dev/null

progName=$distributorProg
cmdline="--generator 0 --maxTests 6 --maxWritePipes 8 --baseWritePipeName /tmp/Worker --readpipe /tmp/${distributorProg}_0 "
priority=97
startExe $progName "$cmdline" $priority

progName=$generatorProg
cmdline=" --generator 1 --maxTests 6 --maxWritePipes 1 --baseWritePipeName /tmp/$distributorProg "
priority=99
startExe $progName "$cmdline" $priority

echo "Sleeping for 30 seconds"
sleep 30

#Initial FPS=24
let fps=24
echo "Set initial FPS = $fps"
echo $fps >>$fpsFile

#For initial testing, sleep 2 minutes between FPS changes
let sleepTime=2*60
#Sleep 30 seconds between start of new FPS and launch of new worker
let beforeNewWorkerSleepTime=30

echo "Sleeping for $sleepTime seconds"
sleep $sleepTime

# i=3
# let fps=${fps}+8
# echo "Setting FPS = $fps"
# echo $fps >>$fpsFile
# echo "Sleeping for $beforeNewWorkerSleepTime seconds before launching new worker"
# sleep $beforeNewWorkerSleepTime
# startWorkerFunc $i 

# echo "Sleeping for $sleepTime seconds"
# sleep $sleepTime

# i=4
# let fps=${fps}+8
# echo "Setting FPS = $fps"
# echo $fps >>$fpsFile
# echo "Sleeping for $beforeNewWorkerSleepTime seconds before launching new worker"
# sleep $beforeNewWorkerSleepTime
# startWorkerFunc $i 

# echo "Sleeping for $sleepTime seconds"
# sleep $sleepTime

# i=5
# let fps=${fps}+8
# echo "Setting FPS = $fps"
# echo $fps >>$fpsFile
# echo "Sleeping for $beforeNewWorkerSleepTime seconds before launching new worker"
# sleep $beforeNewWorkerSleepTime
# startWorkerFunc $i 

# echo "Sleeping for $sleepTime seconds"
# sleep $sleepTime

# i=6
# let fps=${fps}+8
# echo "Setting FPS = $fps"
# echo $fps >>$fpsFile
# echo "Sleeping for $beforeNewWorkerSleepTime seconds before launching new worker"
# sleep $beforeNewWorkerSleepTime
# startWorkerFunc $i 

# echo "Sleeping for $sleepTime seconds"
# sleep $sleepTime

# i=7
# let fps=${fps}+8
# echo "Setting FPS = $fps"
# echo $fps >>$fpsFile
# echo "Sleeping for $beforeNewWorkerSleepTime seconds before launching new worker"
# sleep $beforeNewWorkerSleepTime
# startWorkerFunc $i 

# echo "Sleeping for $sleepTime seconds"
# sleep $sleepTime

let fps=-2
echo "Setting FPS = $fps"
echo "$fps" >>$fpsFile

echo "Sleeping for 2 minutes "
sleep 120 

# echo "Calling killRemnantsFunc"
# killRemnantsFunc
# pidList=` ps -e -o pid,cmd |egrep -e 'Worker|DataGenerator|DataDistributor' |grep -v grep`
# echo "Still live: "
# echo "$pidList"

echo Exiting
