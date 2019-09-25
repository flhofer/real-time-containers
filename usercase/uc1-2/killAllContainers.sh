#!/bin/bash

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

killRemnantsFunc

echo Done