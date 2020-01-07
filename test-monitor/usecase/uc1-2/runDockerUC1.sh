#!/bin/bash
#source ~user/.bashrc

#log output dir
launchDir="/opt/usecase/logs"
mkdir -p $launchDir

local_resultsDir="$launchDir/UC1.`date +%Y%m%d`"
container_resultsDir="/home/logs"

fifoDir=/tmp
fpsFile=$fifoDir/fps

workerPolicy="--fifo"
workerPriority=99
datageneratorPolicy="--fifo"
datageneratorPriority=98
datadistributorPolicy="--fifo"
datadistributorPriority=97

#REGULAR
#For executing the regular test, sleep 30 minutes between FPS changes
#let sleepTime=30*60
#INITIAL
#For initial testing, sleep 2 minutes between FPS changes
let sleepTime=30*60
#TEMPORARY
#For debugging, sleep 1 minute between FPS changes
#let sleepTime=60
#Sleep 30 seconds between start of new FPS and launch of new worker
let beforeNewWorkerSleepTime=30

echo "local_resultsDir=$local_resultsDir; container_resultsDir=$container_resultsDir; fifoDir=$fifoDir"

# Worker parameters
if [ $# -gt 0 ] ; then
	maxworkers=$1
else
	maxworkers=8
fi
echo "maxworkers=$maxworkers"

########################
# Function declarations
########################
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
    i=$1
    if [ $i -lt $maxworkers ] ; then
        baseworkerImage=rt-workerapp
        imageName=${baseworkerImage}$i
        progName=workerapp${i}
        cmdargs="--instnum $i --innerloops 25 --outerloops 10 --maxTests 6 --timedloops 500 --basePipeName $fifoDir/worker "
        startContainer $imageName "$cmdargs" workerapp$i "$workerPolicy" $workerPriority
    fi
}

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



##############################
# End of function declarations
# Start of main script commands
##############################

    #Initialization
mkdir $local_resultsDir 2>/dev/null
rm -f $local_resultsDir/* 2>/dev/null

    #Cleanup
rm -f $fpsFile 2>/dev/null
touch $fpsFile

rm -f $fifoDir/{worker,data}* 2>/dev/null

cd $launchDir

killRemnantsFunc

    #Launch first 3 workers
for (( i=0; i<3; i++ )); do
    startWorkerContainer $i
done

    #Launch datadistributor
cmdargs="--generator 0 --maxTests 6 --maxWritePipes 8 --baseWritePipeName $fifoDir/worker --readpipe $fifoDir/datadistributor_0 "
startContainer rt-datadistributor "$cmdargs" datadistributor "$datadistributorPolicy" $datadistributorPriority

    #Launch datagenerator
cmdargs=" --generator 1 --maxTests 6 --maxWritePipes 1 --baseWritePipeName $fifoDir/datadistributor "
startContainer rt-datagenerator "$cmdargs" datagenerator "$datageneratorPolicy" $datageneratorPriority

echo "Sleeping for 30 seconds"
sleep 30

    #Set initial FPS=24
let fps=24
echo "Set initial FPS = $fps"
echo $fps >>$fpsFile


echo "Sleeping for $sleepTime seconds"
sleep $sleepTime

i=3
let fps=${fps}+8
startNewTest $fps $i

echo "Sleeping for $sleepTime seconds"
sleep $sleepTime

i=4
let fps=${fps}+8
startNewTest $fps $i

echo "Sleeping for $sleepTime seconds"
sleep $sleepTime

i=5
let fps=${fps}+8
startNewTest $fps $i

echo "Sleeping for $sleepTime seconds"
sleep $sleepTime

i=6
let fps=${fps}+8
startNewTest $fps $i

echo "Sleeping for $sleepTime seconds"
sleep $sleepTime

i=7
let fps=${fps}+8
startNewTest $fps $i

echo "Sleeping for $sleepTime seconds"
sleep $sleepTime

let fps=-2
echo "Setting FPS = $fps"
echo "$fps" >>$fpsFile

echo "Sleeping for 2 minutes "
sleep 120

echo "Calling killRemnantsFunc"
killRemnantsFunc

echo Exiting
