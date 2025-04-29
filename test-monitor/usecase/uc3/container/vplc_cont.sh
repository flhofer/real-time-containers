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
	Usage: $0 start [runtime-name] [-v] [nic] [cpu-set]
	       $0 stop  [runtime-name]
	       $0 net   [profile] [nic]
	       $0 macvn [operation] [nic] .. [vnic]

	to control CoDeSys runtime container start and stop
	
	where:
	runtime-name     The name of the runtime instance as in '/var/opt/codesysvcontrol/instances'
	nic              The ethernet controller to pass as dedicated network card (can be internal-virtual = -v)
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
	vcard=0
	if [ "$3" = "-v" ]; then
		vcard=1
		shift
	fi
	card=${3:-"eth0"}
	affin=
	if [ -n "$4" ]; then
		affin="--cpuset-cpus=${4}"
	fi

	# run codesys vControl with additional TMP mapping for log output
	docker run --rm -td -v /tmp:/tmp -v /var/opt/codesysvcontrol/instances/${name}/conf/codesyscontrol:/conf/codesyscontrol/ -v /var/opt/codesysvcontrol/instances/${name}/data/codesyscontrol:/data/codesyscontrol/ --cap-add=IPC_LOCK --cap-add=NET_ADMIN --cap-add=NET_BROADCAST --cap-add=SETFCAP --cap-add=SYS_ADMIN --cap-add=SYS_MODULE --cap-add=SYS_NICE --cap-add=SYS_PTRACE --cap-add=SYS_RAWIO --cap-add=SYS_RESOURCE --cap-add=SYS_TIME ${affin} --hostname ${name} --name ${name} codesyscontrol_virtuallinux:4.11.0.0-b.trunk.170 -n ${card}
	#FIXME: :latest?
	if [ vcard = 0 ];then
		# Get container PID
		conp=$( docker inspect -f '{{.State.Pid}}' $name )
		# add and create network namespace for container
		sudo ip netns attach ${name}_netns_net ${conp}
		
		# set namespace for nic
		sudo ip link set $card netns ${name}_netns_net
		# set link up in new namespace 
		sudo ip netns exec ${name}_netns_net ip link set $card up
		sudo ip netns exec ${name}_netns_net ip link set $card promisc on
		
		# set IP of card in namespace 
		# sudo ip netns exec ${name}_netns_net ip address add $ip dev $card
	fi

elif [ "$cmd" = "stop" ]; then
	name=${2:-"runtime"}
	#stop container
	docker stop ${name}
	
	#delete namespace and return cards to default
	ip netns del ${name}_netns_net

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
		echo "Set profile for mac-vlan driver on ethernet card."
		nic=${3:-"eth0"}
		
		echo "Using nic : ${nic}"
		
		# let's obtain the rest from settings - some problem with gw here -maybe needed for i dev
		hostadd=$( ip -4 addr show ${nic} | grep -o "\([0-9]*\.\)*[0-9]*/[0-9]*" -m 1 )
		base=${hostadd%.*/*}
		mask=${hostadd#*/}
		subnet=${base}.0/${mask}
		clntadd=${5:-${base}.250}	# TODO:, need to find a free IP!!
		macname="vplc0"			# TODO: default name for first network, only one network per adapter possible

		echo "Using nic: ${nic}, IP local for MACvLAN: ${clntadd}  and its sub-net: ${subnet}"

		echo "Remove old network, if it exists, adding new.."
		docker network rm ${macname}
		
		#create a macvlan bridge attached to eth0 - gateway is obtained auto from spec. If no subnet is given, 172.x.0.0 is used!
		docker network create --driver=macvlan --subnet=${subnet} -o parent=${nic} --attachable ${macname}

		# Add Host MAC-VLAN adapter to allow direct communication with containers -- not needed for external
		ip link add br-${macname} link ${nic} type macvlan mode bridge
		ip addr add ${clntadd}/32 dev br-vplc0
		ip link set dev br-${macname} up
		ip route add ${subnet} dev br-${macname}
		
		if [ -n "$subnet" ]; then
			echo "User-defined sub-net set. Remember to start containers with '--ip=${base}.x' to set an IP manually"
		fi
#		echo "Stealing IP?"
#		ip addr del ${hostadd} dev ${nic}		# delete IP from nic
#		ip addr add ${hostadd} dev br-${macname}	# add ip to bridge	
#		ip link set dev ${nic} master br-${macname}	# set master of eth0 = bridge
#		ip link set dev br-${macname} up		# enable bridge
		echo "done."
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

