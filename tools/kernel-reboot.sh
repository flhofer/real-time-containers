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

function print_help {

	cat <<-EOF
	Usage: $0 test  [kernel-ver1] [kernel-ver2]
	       $0 reset [kernel-ver1]
	       $0 list

	for a test-reboot to a specified kernel with timeout in cron
	
	where:
	kernel-ver1      The kernel to test booting - kernel to set for reboot (reset)
	kernel-ver2      The kernel we want to return to in case of failure, defaults to running
	EOF
	
}

cmd=${1:-'help'}

if [ $# -gt 3 ]; then
	echo "Too many arguments supplied!!"
	print_help
	exit 1
fi
if [ "$cmd" = "help" ]; then
	print_help
	exit 0
fi

############### Check privileges ######################
sudo=
if [ "$(id -u)" -ne 0 ]; then
	if [ -z "$(command -v sudo)" ]; then
		cat >&2 <<-EOF
		Error: this script needs the ability to run commands as root.
		You are not running as root and we are unable to find "sudo" available.
		EOF
		exit 1
	fi
	sudo="sudo -E"
fi

# find penultimate kernel config installed, use as default for reboot version
# else, it must be one of the installed, so break
verold=
found=0
resdef=0
for ver in /boot/config-* ; do
	ver=${ver#/boot/config-}
	if [ "$ver" = "$2" ]; then
		ver1=$2
		found=1
	fi
	if [ $found = 0 ]; then
		ver1=$verold
		verold=$ver
	fi
done
# version is newest
if [ "$ver" = "$ver1" ]; then
	resdef=1
fi

[ -n "$2" ] && [ ! "$2" = "$ver1" ] && echo "Error: Kernel version '$2' specified not found !" && exit 1;

ver2=${3:-$(uname -r)}

verold=
abort=1
for ver in /boot/config-* ; do
	ver=${ver#/boot/config-}
	if [ "$ver" = "$ver2" ]; then
		abort=0
		break;
	fi
done
[ $abort = 1 ] && echo "Error: Kernel version '$3' specified not found !" && exit 1;

##################### Update grub and restart ##########################

if [ "$cmd" = "list" ]; then

	echo "Kernel versions available to select from"
	for ver in /boot/config-* ; do
		ver=${ver#/boot/config-}
		echo "* $ver"
	done

elif [ "$cmd" = "test" ]; then
	# get the full name of the menu entry
	submen=$(grep "submenu.*Advanced" /boot/grub/grub.cfg | cut -d \' -f 2)
	if [ -n "$submen" ]; then
		submen="${submen}>"
	fi
	entry=$(grep "menuentry.*${ver1}'" /boot/grub/grub.cfg | cut -d \' -f 2)
	
	# set new bootkernel
	$sudo sh -c "sed -i '/#GRUB_DEFAULT=/s/#GRUB_DEFAULT=/GRUB_DEFAULT=/' /etc/default/grub"
	$sudo sh -c "sed -i '/GRUB_DEFAULT/s/=.*/=\"${submen}${entry}\"/' /etc/default/grub"

	# minutes now
	min=$(date +'%M')
	# add to cron a timeout 1 hr, restart with reset
	$sudo sh -c "echo '${min} */1 * * * cd "${PWD}" && ${0} reset ${ver2} ' | crontab -u root -"

	$sudo update-grub

	$sudo reboot

elif [ "$cmd" = "reset" ]; then
	# get the full name of the menu entry
	submen=$(grep "submenu.*Advanced" /boot/grub/grub.cfg | cut -d \' -f 2)
	if [ -n "$submen" ]; then
		submen="${submen}>"
	fi
	entry=$(grep "menuentry.*${ver1}'" /boot/grub/grub.cfg | cut -d \' -f 2)

	# on reset first ver is ver to use
	$sudo sh -c "sed -i '/GRUB_DEFAULT/s/=.*/=\"${submen}${entry}\"/' /etc/default/grub"
	if [ $resdef = 1 ] ; then
		$sudo sh -c "sed -i '/^GRUB_DEFAULT=/s/GRUB_DEFAULT=/#GRUB_DEFAULT=/' /etc/default/grub"
	fi 
	
	# remove start script
	$sudo crontab -u root -r

	$sudo update-grub

	$sudo reboot
else
	echo "Unknown command '$1'"
	print_help
	exit 1
fi

