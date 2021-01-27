#!/bin/bash 
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/snap/bin"

from=${1:-'0'}
to=${2:-'8'}

eval rm -r artifacts

# download latest version
eval ./dllatest.sh
if [ $? -ne 0 ]; then
	echo "No new version"
	exit
fi

eval chown -R 1000:1000 artifacts

for file in {'orchestrator','WorkerApp','DataGenerator'}; do
	diff artifacts/$file $file &>/dev/null
	es=$?
	tot=$(( tot + es ))
	if [ $es -ne 0 ]; then 
	    echo "Updating $file.."
	    eval mv artifacts/$file $file
	    eval chmod +x $file
	fi
done
# ipdate images
if [ $tot -ne 0 ]; then
	eval ./ucexec.sh build
fi

diff artifacts/orchestrator orchestrator &>/dev/null
es=$?
tot=$tot+$es
if [ $es -ne 0 ]; then 
    echo "Updating orchestrator.."
    eval mv artifacts/$file $file
    eval chmod +x artifacts/$file
fi


# run usecase
eval ./cptestrun.sh $from $to

# run usecase evaluation
eval Rscript usecasedata.r 

# move prints to www latest and rename logs
eval chown -R 1000:1000 logs/
eval mv logs/*.png www/latest
dates=$(date +'%b%d')
eval mv logs logs_test_${dates}

daten=$(date +'%b %d')
version=$(./orchestrator --version | grep DC | cut -dV  -f2)
eval echo "${daten}${version}" > www/latest/date.txt
