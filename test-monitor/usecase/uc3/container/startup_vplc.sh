#!/bin/bash

usage()
{
cat <<EOF
OPTIONS := { -s[erver] Add license-server by IP-address
                       Multiple license-servers are specified seperately "... -s <IPa> -s<IPb> ..."
             -n[ic]    Wait for network interface (nic) to be mapped into container namespace.
                       Multiple nics are specified seperately "... -n <NICa> -n<NICb> ..."
             -h[elp] 
            }
EOF
}

term() {
    echo "[INFO] $0: Shuting down codesyscontrol..."
    kill -15 $pid
    wait $pid
}

while getopts s:hn: opt
do
	case "${opt}" in 
        s)
            LICENSESERVERLIST+=("${OPTARG}")
            ;;
        h)
		    usage
		    exit 1
		    ;;
        n)
            NICLIST+=("${OPTARG}")
            ;;
		?)
     		echo "Unknown option -"${OPTARG}
		    usage	
		    exit 1
		    ;;
	esac
done
shift $((OPTIND -1))

# At runtimestart we expect /conf/codesyscontrol and /data/codesyscontrol to be present
if [ ! -d /conf/codesyscontrol/ ] || [ ! -d /data/codesyscontrol/ ]; then
    echo "[ERROR] $0: Missing /conf/codesyscontrol or /data/codesyscontrol. Call docker run with volume options:" 
    echo
    echo "-v ~/<mountingpoint>/conf/codesyscontrol/:/conf/codesyscontrol/ -v ~/<mountingpoint>/data/codesyscontrol/:/data/codesyscontrol/"
    echo "For example:"
    echo "docker run -v ~/dockerMount/conf/codesyscontrol/:/conf/codesyscontrol/ -v ~/dockerMount/data/codesyscontrol/:/data/codesyscontrol/ -p 11740:11740/tcp -p 443:443/tcp -p 8080:8080/tcp codesyscontrol_linux:4.5.0.0"
    exit 1
fi 


echo "[INFO] $0: Initialize /conf and /data directories used by docker mounts"

#check if initialization of /conf was already done. 
if [ ! -f /conf/codesyscontrol/.docker_initialized ]; then 
    #not yet initialized, copy original files
    echo "[INFO] $0: Initialize /conf/codesyscontrol with files from /etc"
    cp -f /etc/CODESYSControl.cfg /conf/codesyscontrol/
    cp -f /etc/CODESYSControl_User.cfg /conf/codesyscontrol/
    #touch marker file to avoid overwriteing existing files
    touch /conf/codesyscontrol/.docker_initialized
fi     

# add license-server to config-file
if [[ -z $LICENSESERVER ]]
then
    echo "[INFO] $0: To add a license-server start the container with -s <IP> at the end"
    echo "[INFO] $0: For example: docker run -v ~/dockerMount/conf/codesyscontrol/:/conf/codesyscontrol/ -v ~/dockerMount/data/codesyscontrol/:/data/codesyscontrol/ -p 11740:11740/tcp -p 443:443/tcp -p 8080:8080/tcp codesyscontrol_linux:4.5.0.0 -s 192.168.99.1"
fi

CFGPATH="/conf/codesyscontrol/CODESYSControl.cfg"

# Cleanup all old LicenseServer entries
sed -i '/LicenseServer.*/d' $CFGPATH

if [ ! -z $LICENSESERVERLIST ]
then
    NUM=1
    # Write first entry below EnableNetLicenses=1
    sed -i "/EnableNetLicenses=1/a LicenseServer.$NUM=${LICENSESERVERLIST[0]}" $CFGPATH
    echo "[INFO] $0: Licenseserver ${LICENSESERVERLIST[0]} written to config"

    # Write all next entries below last one
    for LICENSESERVER in  "${LICENSESERVERLIST[@]:1}" ; do
        ((NUM=NUM+1))
        sed -i "/LicenseServer.$((NUM-1))=*/a LicenseServer.$NUM=${LICENSESERVER}" $CFGPATH
        echo "[INFO] $0: Licenseserver ${LICENSESERVER} written to config"
    done
fi

#check if initialization of /data was already done. 
if [ ! -f /data/codesyscontrol/.docker_initialized ]; then 
    #not yet initialized, copy original files
    echo "[INFO] $0: Initialize /data/codesyscontrol with files from /var/opt/codesys"
    #copy contents including all hidden files
    cp -rT /var/opt/codesys/ /data/codesyscontrol/
    #touch marker file to avoid overwriteing existing files
    touch /data/codesyscontrol/.docker_initialized
fi     

cd /data/codesyscontrol/


echo "[INFO] $0: Check needed capabilities"

NEEDEDCAPS=("cap_chown" "cap_ipc_lock" "cap_kill" "cap_net_admin" "cap_net_bind_service" "cap_net_broadcast" \
    "cap_net_raw" "cap_setfcap" "cap_setpcap" "cap_sys_admin" "cap_sys_module" "cap_sys_nice" "cap_sys_ptrace" \
    "cap_sys_rawio" "cap_sys_resource" "cap_sys_time")
CAPMISSING=""
for i in ${NEEDEDCAPS[@]}; do
    echo -n "[INFO] $0: Testing $i: "
    if /sbin/capsh --has-p=$i 2>/dev/null ; then
        echo -e "[OK]"
    else
        echo -e "[NOK]"
        CAPMISSING="TRUE"
    fi
done

if [ ! -z $CAPMISSING ] ; then
    echo "[WARNING] $0: Not all needed capabilities found. No realtime behaviour can be achived!"
fi

# check if needed network adapter (NIC) is specified and wait until it got mapped to our namespace
INTERVAL=2
TIMEOUT=10

if [ ! -z "$NICLIST" ]
then
    echo "[INFO] $0: Specified network adapters:"
    for NIC in "${NICLIST[@]}"
    do
        echo "[INFO] $0: - $NIC"
    done
    for NIC in "${NICLIST[@]}"
    do
        echo "[INFO] $0: Waiting for network adapter $NIC"
        COUNTER=0

        while ! grep "up" "/sys/class/net/$NIC/operstate" 1>/dev/null 2>/dev/null  && [ $COUNTER -lt $TIMEOUT ]
        do 
            echo "[INFO] $0: sleeping..."
            sleep $INTERVAL
            ((COUNTER+=$INTERVAL))
        done

        # No nic mapped after timeout
        if ! grep "up" "/sys/class/net/$NIC/operstate" 1>/dev/null 2>/dev/null ; then
            echo "[ERROR] $0: Specified NIC $NIC not mapped to container, operstate not up. Aborting startup..."
            echo "[ERROR] $0: Check if cable is plugged in and the adapter is up."
            exit 1

        else
            echo "[INFO] $0: Specified NIC $NIC mapped to container."
            sleep 1
        fi
    done
fi


# copy Server.ini to mountfolder if it doesn't exist there yet. If it does copy from mountfolder to actual.
if [ ! -f /conf/codesyscontrol/Server.ini ]; then
    CODEMETERINI=/etc/wibu/CodeMeter/Server.ini
    sed -i 's,LogCmActDiag=1,LogCmActDiag=0,g' ${CODEMETERINI}
    if grep -q -m 1 UseBroadcast=1 ${CODEMETERINI}; then
      echo "CodeMeter: License server broadcast already disabled"
    else
      echo "CodeMeter: Disable license server broadcast"
      if grep -q -m 1 UseBroadcast=0 ${CODEMETERINI}; then
        sed -i 's,UseBroadcast=0,UseBroadcast=1,g' ${CODEMETERINI}
      elif grep -q -m 1 '\[ServerSearchList\]' ${CODEMETERINI}; then
        sed -i '/\[ServerSearchList\]/a UseBroadcast=1' ${CODEMETERINI}
      else
        sed -i '$a[ServerSearchList]' ${CODEMETERINI}
        sed -i '$aUseBroadcast=1' ${CODEMETERINI}
      fi
    fi
    cp /etc/wibu/CodeMeter/Server.ini /conf/codesyscontrol/
else
    cp /conf/codesyscontrol/Server.ini /etc/wibu/CodeMeter/Server.ini
fi
# start codemeter runtime
start-stop-daemon --start --quiet --chuid daemon --exec /usr/sbin/CodeMeterLin -- -l+

echo "[INFO] $0: Codesyscontrol starting"
/opt/codesys/bin/codesyscontrol.bin $DEBUG /conf/codesyscontrol/CODESYSControl.cfg &

pid=($!)
trap term SIGTERM
trap term SIGINT
wait "$pid"
