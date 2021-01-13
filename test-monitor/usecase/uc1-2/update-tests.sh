#!/bin/bash 
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/snap/bin"

eval rm -r artifacts

# download latest version
eval ./dllatest.sh
if [ $? -ne 0 ]; then
	echo "No new version"
	exit
fi

eval mv artifacts/orchestrator .
eval chmod +x orchestrator

# run usecase
eval ./cptestrun.sh

# run usecase evaluation
eval Rscript usecasedata.r 

# move prints to www latest and rename logs
eval mv logs/*.png www/latest
dates=$(date +'%b%d')
eval mv logs logs_test_${dates}

daten=$(date +'%b %d')
version=$(./orchestrator --version | grep DC | cut -dV  -f2)
eval echo "${daten}${version}" > www/latest/date.txt
