#!/bin/bash
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

if [[ ! "$1" == "quiet" ]]; then

cat <<EOF

######################################

Kernel test execution
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

############### Check privileges ######################

if [ "$(id -u)" -ne 0 ]; then
	cat >&2 <<-EOF
	Error: this script needs the ability to run commands as root.
	EOF
	exit 1
fi

function print_help {

	cat <<-EOF
	Usage: $0 start [number] [kernel-ver1] [kernel-ver2]
	 or    $0 cont	[number] [kernel-ver1] [kernel-ver2]

	for start or continuation of a kernel test.
	
	Defaults are:
	number = *                      maximum number of cpu to use, default all cpus used
	kernel-ver1 = 6.6.21-rt26       The specified version is base version
	kernel-ver2 = same version      The specified version is compare version
	EOF
	
}

cmd=${1:-'help'}

if [ $# -gt 4 ]; then
	echo "Too many arguments supplied!!"
	print_help
	exit 1
fi
if [ "$cmd" = "help" ]; then
	print_help
	exit 0
fi

maxcpu=${2:-$(( $(nproc --all) - 1 ))}
ver1=${3:-'6.6.21-rt26'}
ver2=${4:-${ver1}}

if [[ ! "$cmd" == "start" ]]; then

	#shut down cores not used
	for ((i=$(($maxcpu+1));i<$(nproc --all);i++))
	do 
		echo "Setting CPU"$i" offline..."
		$(echo 0 > /sys/devices/system/cpu/cpu$i/online)
		sleep 1
	done

	# disable smt
	$(echo off > /sys/devices/system/cpu/smt/control)
fi

runno=0 # changed_in_run

function update_kernel () {
	local runver=$1 # runno for this version, 1-10 ver 1, 11-20 ver 2
	if [ $runver -gt 10 ] ; then 
		runver=$(( runver - 10 ))
		ver=$ver2
	else
		ver=$ver1
	fi

	if [ "$runver" -eq 1 ]; then
		# nosmt
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
		sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nosmt\"/' /etc/default/grub

	elif [ "$runver" -eq 2 ]; then
        # nosmt+rcu_nocbs
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
        sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nosmt rcu_nocbs=1-3\"/' /etc/default/grub

	elif [ "$runver" -eq 3 ]; then
		# nosmt+rcu_nocbs+rcu_nocb_poll
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
		sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nosmt rcu_nocbs=1-3 rcu_nocb_poll\"/' /etc/default/grub

	elif [ "$runver" -eq 4 ]; then
		# nosmt+irqaffinity
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
		sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nosmt irqaffinity=0,4-$maxcpu\"/' /etc/default/grub

	elif [ "$runver" -eq 5 ]; then
        # nosmt+irqaffinity+rcu_nocbs
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
        sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nosmt irqaffinity=0,4-$maxcpu rcu_nocbs=1-3\"/' /etc/default/grub

	elif [ "$runver" -eq 6 ]; then
        # nosmt+irqaffinity+rcu_nocb_poll+rcu_nocbs
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
        sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nosmt irqaffinity=0,4-$maxcpu rcu_nocb_poll rcu_nocbs=1-3\"/' /etc/default/grub
	
	elif [ "$runver" -eq 7 ]; then
        # nosmt+irqaffinity+rcu_nocb_poll+rcu_nocbs+skew_tick
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
        sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nosmt irqaffinity=0,4-$maxcpu rcu_nocb_poll rcu_nocbs=1-3 skew_tick=1\"/' /etc/default/grub
	
	elif [ "$runver" -eq 8 ]; then
        # nosmt+irqaffinity+rcu_nocb_poll+rcu_nocbs+nosoftlockup+tsc=nowatchdog
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
        sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nosmt irqaffinity=0,4-$maxcpu rcu_nocb_poll rcu_nocbs=1-3 nosoftlockup tsc=nowatchdog\"/' /etc/default/grub

	elif [ "$runver" -eq 9 ]; then
        # nosmt+irqaffinity+rcu_nocb_poll+rcu_nocbs+skew_tick+nosoftlockup+tsc=nowatchdog
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
        sed -i '/LINUX_DEFAULT/s/splash.*\"/splash nosmt irqaffinity=0,4-$maxcpu rcu_nocb_poll rcu_nocbs=1-3 skew_tick=1 nosoftlockup tsc=nowatchdog\"/' /etc/default/grub

	# last test! if 2 versions are specified, test 11 = test 1 with ver 2
	elif [ "$runver" -eq 10 ]; then
        # default
		if [ ! "$ver1" = "$ver2" ]; then
			sed -i '/GRUB_DEFAULT/s/Linux.*\"/Linux ${ver}\"/' /etc/default/grub
		fi
        sed -i '/LINUX_DEFAULT/s/splash.*\"/splash\"/' /etc/default/grub

		if [ "$ver1" = "$ver2" ] || [ $runno -eq 20 ] ; then
			# versions are the same, end it here, or end of run for ver2
			crontab -u root -r
			rm $0
		fi		
	fi

	# update grub menu
	update-grub
	if [ $? -ne 0 ]; then

		echo "error update grub" >> result.txt
		exit 1

	fi
}

function update_runno () {
	sed -i '0,/maxcpu=/{s/maxcpu=.*/maxcpu='${maxcpu}'/}' ./kernelrun.sh
	sed -i '0,/changed_in_run/{s/runno='${runno}'/runno='$(($runno+1))'/}' ./kernelrun.sh
	runno=$((runno+1))
}

##################### DETERMINE CLI PARAMETERS ##########################

if [[ "$cmd" == "start" ]]; then

	runno=0 # reset 
	cp $0 ./kernelrun.sh

	rm -r log/test*/

	# add to startup 
	eval "echo '@reboot cd "$PWD" && ./kernelrun.sh cont ${maxcpu} ${ver1} ${ver2} ' | sudo crontab -u root -"
else 
	echo "...waiting"
	sleep 60
	echo "... run test"
	#execute test
	./test.sh quiet 
	#move tests to their directory
	mkdir -p log/test${runno}
	mv log/?-* log/test${runno}/
fi

update_runno

update_kernel $runno

#reboot system
reboot

