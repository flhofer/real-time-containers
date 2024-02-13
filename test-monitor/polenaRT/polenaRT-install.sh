#!/bin/bash

set -e

# Long term Kernel Suggestions
#   - 22.04 LTS -> 6.1.77 - rt24
#   - newest LT 6.6.15 - rt22
# Latest Stable  6.7 - rt6 ( 6.7.4? )

linux_patch=${1:-'6.1.77-rt24'}
balena_tag=${2:-'v20.10.19'}

rt_patch=$(echo "$linux_patch" | sed -n 's/[0-9.]*-\(.*\)/\1/p')
linux_ver=$(echo "$linux_patch" | sed -n 's/\([0-9]*\(\.[0-9]*\)\{1,2\}\).*/\1/p')
linux_root=$(echo "$linux_ver" | sed -n 's/\([0-9]*\).*/\1/p')
linux_base=$(echo "$linux_ver" | sed -n 's/\([0-9]*\.[0-9]*\).*/\1/p')
balena_rev=$(echo "$balena_tag" | sed 's|+|.|g')

# Check and warn about missing required commands before doing any actual work.
abort=0
for cmd in wget curl tar; do
	if [ -z "$(command -v $cmd)" ]; then
		cat >&2 <<-EOF
		Error: unable to find required command: $cmd
		EOF
		abort=1
	fi
done
[ $abort = 1 ] && exit 1

sudo=
if [ "$(id -u)" -ne 0 ]; then
	if [ -z "$(command -v sudo)" ]; then
		cat >&2 <<-EOF
		Error: this installer needs the ability to run commands as root.
		You are not running as root and we are unable to find "sudo" available.
		EOF
		exit 1
	fi
	sudo="sudo -E"
fi

#echo "Selecting RT-patch ${rt_patch} for kernel root family ${linux_root}, base version ${linux_base}, release ${linux_ver}"

machine=$(uname -m)

# Detect the system architecture
case "$machine" in
	"armv5"*)
		arch="armv5e"
		;;
	"armv6"*)
		arch="armv6l"
		;;
	"armv7"*)
		arch="armv7hf"
		;;
	"armv8"*)
		arch="arm64"
		;;
	"aarch64"*)
		arch="arm64"
		;;
	"x86_64")
		arch="amd64"
		;;
	*)
		echo "Unknown machine type: $machine" >&2
		exit 1
esac

# Check if kernel config exists
config_file="linux-${machine}-${linux_patch}.config"

if [ ! -f "$config_file" ]; then
	echo "Error: required Kernel config file '${config_file}' does not exist!" >&2
	exit 1
fi

#################################
# Required system packages 
#################################
echo
echo "## Installing dependencies..."
sudo apt-get update
sudo apt-get install -y kernel-package libssl-dev git autoconf libtool automake curl libncurses5-dev pkg-config bison flex libelf-dev
# if xconfig...
sudo apt-get install -y qt5-default
# if create-pkg...
sudo apt-get install -y checkinstall

################################
# Preempt RT
################################
mkdir -p polena-build
cd polena-build

if [ ! -e "./linux-${linux_ver}" ]; then
	echo
	echo "## Downloading Linux Kernel"
	wget https://www.kernel.org/pub/linux/kernel/v${linux_root}.x/linux-${linux_ver}.tar.gz
else
	# cleanup
	rm -r linux-${linux_ver}
fi
tar xf linux-${linux_ver}.tar.gz

if [ ! -f "patch-${linux_ver}-${linux_patch}.patch.xz" ]; then
	echo
	echo "## Downloading RT patch"
	wget https://www.kernel.org/pub/linux/kernel/projects/rt/${linux_base}/patch-${linux_patch}.patch.xz

fi

echo
echo "## Patching Linux Kernel"
cd linux-${linux_ver}
xzcat ../patch-${linux_patch}.patch.xz | patch -p1

echo
echo "## Compiling kernel"
cp ../../${config_file} .config
yes "" | make oldconfig
CONCURRENCY_LEVEL=$(nproc) make-kpkg --rootcmd fakeroot --initrd kernel_image kernel_headers
cd ..

echo
echo "## Installing kernel"
sudo dpkg -i linux-headers-${linux_patch}.deb linux-image-${linux_patch}.deb

echo
echo "## Configuring GRUB"
#sudo sed -i -e 's/^/#/' /etc/default/grub # comment out previous GRUB config
sudo cp /etc/default/grub /etc/default/grub.backup
echo '
GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux '${linux_patch}'"
GRUB_HIDDEN_TIMEOUT_QUIET="true"
GRUB_TIMEOUT="10"
GRUB_DISTRIBUTOR="`lsb_release -i -s 2> /dev/null || echo Debian`"
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
GRUB_CMDLINE_LINUX=""
' >> grub
sudo mv grub /etc/default/grub
sudo update-grub2

#echo "## Installing Balena"
#url="https://github.com/resin-os/balena/releases/download/${balena_rev}/balena-${balena_rev}-${arch}.tar.gz"
#curl -sL "$url" | tar xzv -C /usr/local/bin --strip-components=1

cat <<EOF

######################################

Installation successful!
______     _                  
| ___ \   | |                 
| |_/ /__ | | ___ _ __   __ _ 
|  __/ _ \| |/ _ \ '_ \ / _' |
| | | (_) | |  __/ | | | (_| |
\_|  \___/|_|\___|_| |_|\__,_|                              
                              
simply real-time containers

######################################

EOF
