#!/bin/sh

set -e
linux_ver="4.18-rc8"
#balena_tag="17.06-rev1"
#balena_tag=$(echo "$balena_tag" | sed 's|+|.|g')

#################################
# Required system packages 
#################################
echo
echo "## Installing dependencies..."
sudo apt-get install -y kernel-package libssl-dev git autoconf libtool automake curl
#libncurses5-dev pkg-config bison flex 
# if xconfig...
sudo apt-get install -y qt5-default
# if create-pkg...
sudo apt-get install -y checkinstall

################################
# PREEMPT-RT
################################
mkdir -p polena-build
cd polena-build

echo
echo "## Downloading Linux Kernel"
wget https://git.kernel.org/torvalds/t/linux-${linux_ver}.tar.gz
tar xf linux-${linux_ver}.tar.gz

echo
echo "## Downloading RT patch"
wget http://cdn.kernel.org/pub/linux/kernel/projects/rt/4.18//patch-${linux_ver}-rt1.patch.xz


echo
echo "## Patching Linux Kernel"
cd linux-${linux_ver}
xzcat ../patch-${linux_ver}-rt1.patch.xz | patch -p1

echo
echo "## Compiling kernel"
cp ../../ubuntu-x86_64.config .config
yes "" | make oldconfig
CONCURRENCY_LEVEL=$(nproc) make-kpkg --rootcmd fakeroot --initrd kernel_image kernel_headers
cd ..

echo
echo "## Installing kernel"
cd ..
sudo dpkg -i linux-headers-${linux_ver}-rt*.deb linux-image-${linux_ver}-rt*.deb

echo
echo "## Configuring GRUB"
#sudo sed -i -e 's/^/#/' /etc/default/grub # comment out previous GRUB config
sudo cp /etc/default/grub /etc/default/grub.backup
echo '
GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux ${linux_ver}-RT"
GRUB_HIDDEN_TIMEOUT_QUIET="true"
GRUB_TIMEOUT="10"
GRUB_DISTRIBUTOR="`lsb_release -i -s 2> /dev/null || echo Debian`"
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
GRUB_CMDLINE_LINUX=""
' >> grub
sudo mv grub /etc/default/grub

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
