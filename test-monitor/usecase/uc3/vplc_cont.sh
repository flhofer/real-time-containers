#!/bin/sh

if [ ! "$1" = "quiet" ]; then
	cat <<-EOF

	######################################

	Virtual container management
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

print_help () {

	cat<<-EOF
	Usage: $0 start [runtime-name] [nic] [cpu-set]
	       $0 stop  [runtime-name]
	       $0 net   [profile] [nic]
	       $0 macvn [operation] [nic] .. [vnic]

	to control CoDeSys runtime container start and stop
	
	where:
	runtime-name     The name of the runtime instance as in '/var/opt/codesysvcontrol/instances'
	nic              The ethernet controller to pass as dedicated network card (can be internal-virtual)
	cpu-set          Whether to apply a CPU-set pinning to the container and sub-tasks, i.e., a CPU-list
	profile          Reconfigure network 'default', 'bridge' with added internal brige, 'macvlan' 
	                 with additional 1 macvlan, or 'print' configuration
	operation        create, delete, add NIC or VNIC to MACVLAN network
	EOF
	
}

cmd=${1:-'help'}

if [ "$cmd" = "help" ]; then
	print_help
	exit 0
fi

if [ "$cmd" = "start" ]; then
	name=${2:-"runtime"}
	card=${3:-"eth0"}
	affin=
	if [ -n "$4" ]; then
		affin="--cpuset-cpus=${4}"
	fi

	# run codesys vControl with additional TMP mapping for log output
	docker run --rm -td -v /tmp:/tmp -v /var/opt/codesysvcontrol/instances/${name}/conf/codesyscontrol:/conf/codesyscontrol/ -v /var/opt/codesysvcontrol/instances/${name}/data/codesyscontrol:/data/codesyscontrol/ --cap-add=IPC_LOCK --cap-add=NET_ADMIN --cap-add=NET_BROADCAST --cap-add=SETFCAP --cap-add=SYS_ADMIN --cap-add=SYS_MODULE --cap-add=SYS_NICE --cap-add=SYS_PTRACE --cap-add=SYS_RAWIO --cap-add=SYS_RESOURCE --cap-add=SYS_TIME ${affin} --hostname ${name} --name ${name} codesyscontrol_virtuallinux:4.11.0.0-b.trunk.170 -n ${card}
	#FIXME: :latest?

elif [ "$cmd" = "stop" ]; then
	name=${2:-"runtime"}
	#stop container
	docker stop ${name}

elif [ "$cmd" = "net" ]; then
	prof=${2:-"print"}
	if [ "$prof" = "print" ]; then
		docker network list --format table
		ip address
		
	elif [ "$prof" = "default" ]; then
		echo "Not implemented"
	elif [ "$prof" = "bridge" ]; then
		echo "Not implemented"
	elif [ "$prof" = "macvlan" ]; then
		echo "Not implemented"
	else
		echo "Unknown operation '$prof'"
		print_help
		exit 1
	fi
	
elif [ "$cmd" = "macvn" ]; then
	echo "Not implemented"
else
	echo "Unknown command '$1'"
	print_help
	exit 1
fi

