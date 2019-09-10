#!/bin/sh

set -e
linux_ver="4.19.50"
linux_patch="rt22"
linux_root=$(echo "$linux_ver" | sed -n 's/\([0-9]*\.[0-9]*\).*/\1/p')
#balena_tag="17.06-rev1"
#balena_tag=$(echo "$balena_tag" | sed 's|+|.|g')

machine=$(uname -m)

case "$machine" in
	"armv5"*)
		arch="armv5"
		echo "$arch not supported yet."
		exit 1
		;;
	"armv6"*)
		arch="armv6"
		echo "$machine not supported yet."
		exit 1
		;;
	"armv7"*)
		arch="armv7"
		echo "$arch not supported yet."
		exit 1
		;;
	"armv8"*)
		arch="aarch64"
		echo "$arch not supported yet."
		exit 1
		;;
	"aarch64"*)
		arch="aarch64"
		echo "$arch not supported yet."
		exit 1
		;;
	"i386")
		arch="i386"
		echo "$arch not supported yet."
		exit 1
		;;
	"i686")
		arch="i386"
		echo "$arch not supported yet."
		exit 1
		;;
	"x86_64")
		arch="x86_64"
		;;
	*)
		echo "Unknown machine type: $machine"
		exit 1
esac

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
# Xenomai
################################
mkdir -p polena-build
cd polena-build

if [ ! -e "./linux-${linux_ver}" ]; then
	echo
	echo "## Downloading Linux Kernel"
	wget https://www.kernel.org/pub/linux/kernel/v4.x/linux-${linux_ver}.tar.gz
else
	# cleanup
	rm -r linux-${linux_ver}
fi
tar xf linux-${linux_ver}.tar.gz

if [ ! -f "patch-${linux_ver}-${linux_patch}.patch.xz" ]; then
	echo
	echo "## Downloading RT patch"
	wget https://www.kernel.org/pub/linux/kernel/projects/rt/${linux_root}/patch-${linux_ver}-${linux_patch}.patch.xz

fi

echo
echo "## Patching Linux Kernel"
cd linux-${linux_ver}
xzcat ../patch-${linux_ver}-${linux_patch}.patch.xz | patch -p1

echo
echo "## Compiling kernel"
cp ../../ubuntu-x86_64.config .config
yes "" | make oldconfig
CONCURRENCY_LEVEL=$(nproc) make-kpkg --rootcmd fakeroot --initrd kernel_image kernel_headers
cd ..

echo
echo "## Installing kernel"
sudo dpkg -i linux-headers-${linux_ver}-rt*.deb linux-image-${linux_ver}-rt*.deb

echo
echo "## Configuring GRUB"
#sudo sed -i -e 's/^/#/' /etc/default/grub # comment out previous GRUB config
sudo cp /etc/default/grub /etc/default/grub.backup
echo '
GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux '${linux_ver}'-'${linux_patch}'"
GRUB_HIDDEN_TIMEOUT_QUIET="true"
GRUB_TIMEOUT="10"
GRUB_DISTRIBUTOR="`lsb_release -i -s 2> /dev/null || echo Debian`"
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
GRUB_CMDLINE_LINUX=""
' >> grub
sudo mv grub /etc/default/grub
sudo update-grub2

#echo "## Installing Balena"
#url="https://github.com/resin-os/balena/releases/download/${balena_tag}/balena-${balena_tag}-${arch}.tar.gz"
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
