#!/bin/bash

usage()
{
cat <<EOF
OPTIONS := { -m[aintenanceMode] enable the maintenance mode of the CODESYS Edge Gateway
             -h[elp] 
            }
EOF
}

term() {
    echo "[INFO] $0: Shuting down codesysedge..."
    kill -15 $pid
    wait $pid
}

while getopts mh opt
do
	case "${opt}" in 
        m)
            MAINTENANCEMODE=true
            ;;
        h)
		    usage
		    exit 1
		    ;;
		?)
     		echo "Unknown option -"${OPTARG}
		    usage	
		    exit 1
		    ;;
	esac
done


if [[ -z $MAINTENANCEMODE ]]
then
    echo "[INFO] $0: To start the edge gateway in maintenance mode, use the script argument -m."
    MAINTENANCEMODE=false
fi

# At runtimestart we expect /conf/codesysedge to be present
if [ ! -d /conf/codesysedge/ ] || [ ! -d /data/codesysedge/ ]; then
    echo "[ERROR] $0: Missing /conf/codesysedge or /data/codesysedge. Call docker run with volume options:" 
    echo
    echo "-v ~/<mountingpoint>/conf/codesysedge/:/conf/codesysedge/"
    echo "For example:"
    echo "docker run -d -v ~/dockerMountEdge/conf/codesysedge/:/conf/codesysedge/ -v ~/dockerMountEdge/data/codesysedge/:/data/codesysedge/ -p 1217:1217/tcp codesysedge_edgeamd64:4.5.0.0"
    exit 1
fi 

echo "[INFO] $0: Initialize /conf directory used by docker mounts"
#check if initialization was already done. 
if [ ! -f /conf/codesysedge/.docker_initialized ]; then 
    #not yet initialized, copy original files
    echo "[INFO] $0: Initialize /conf/codesysedge with files from /etc"
    cp -f /etc/GatewayvControl.cfg /conf/codesysedge/
    #touch marker file to avoid overwriteing existing files
    touch /conf/codesysedge/.docker_initialized
fi     

if [ $MAINTENANCEMODE == true ]
then
    echo "[INFO] $0: Maintenancemode is enabled!"
    if [ ! -z $(grep MaintenanceMode=1 /conf/codesysedge/GatewayvControl.cfg) ]; then
        echo "[INFO] MaintenanceMode already enabled."
    else
        sed -i '/\[CmpEdgeGateway\]/a MaintenanceMode=1' /conf/codesysedge/GatewayvControl.cfg
    fi
else
    sed -i '/MaintenanceMode=1/d' /conf/codesysedge/GatewayvControl.cfg
fi

#check if initialization was already done. 
if [ ! -f /data/codesysedge/.docker_initialized ]; then 
    #not yet initialized, copy original files
    echo "[INFO] $0: Initialize /data/codesysedge with files from /var/opt/codesysedge"
    #copy contents including all hidden files
    cp -rT /var/opt/codesysedge/ /data/codesysedge/
    #touch marker file to avoid overwriteing existing files
    touch /data/codesysedge/.docker_initialized
fi     

cd /data/codesysedge

echo "[INFO] $0: Codesysedge starting"
/opt/codesysedge/bin/codesysedge.bin /conf/codesysedge/GatewayvControl.cfg &

pid=($!)
trap term SIGTERM
trap term SIGINT
wait "$pid"
