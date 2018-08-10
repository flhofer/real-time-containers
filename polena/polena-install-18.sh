#!/bin/sh

set -e
linux_ver="4.9.90"

#################################
# Required system packages 
#################################
echo
echo "## Installing dependencies..."
# if xconfig...
sudo apt-get install -y qt5-default
# if create-pkg...
sudo apt-get install -y checkinstall
# last the main dependencies
sudo apt-get install -y kernel-package libssl-dev git autoconf libtool automake curl libncurses5 bison flex pkg-config

################################
# Xenomai
################################
mkdir -p polena-build
cd polena-build

echo
echo "## Cloning Xenomai"
git clone https://git.xenomai.org/xenomai-3.git

echo
echo "## Configuring Xenomai"
cd xenomai-3
./scripts/bootstrap
cd ..

echo
echo "## Downloading Linux Kernel"
wget https://www.kernel.org/pub/linux/kernel/v4.x/linux-${linux_ver}.tar.gz
tar xf linux-${linux_ver}.tar.gz
wget http://xenomai.org/downloads/ipipe/v4.x/x86/ipipe-core-${linux_ver}-x86-6.patch

echo
echo "## Patching Linux Kernel"
cd linux-${linux_ver}
./../xenomai-3/scripts/prepare-kernel.sh --arch=${arch} --ipipe=../ipipe-core-${linux_ver}-x86-6.patch
echo "I-Pipe patched"


echo
echo "## Compiling kernel"
cp ../../ubuntu-x86_64.config .config
yes "" | make oldconfig
CONCURRENCY_LEVEL=$(nproc) make-kpkg --rootcmd fakeroot --initrd kernel_image kernel_headers


echo
echo "## Installing kernel"
cd ..
sudo dpkg -i linux-headers-${linux_ver}-xenomai*.deb linux-image-${linux_ver}-xenomai-*.deb
cd ..

echo
echo "## Configuring group"
sudo addgroup xenomai --gid 1234
sudo addgroup root xenomai
sudo usermod -a -G xenomai $USER

echo
echo "## Configuring GRUB"
#sudo sed -i -e 's/^/#/' /etc/default/grub # comment out previous GRUB config
sudo cp /etc/default/grub /etc/default/grub.backup
echo '
GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux 4.9.51-xenomai-3.0.6"
GRUB_HIDDEN_TIMEOUT_QUIET="true"
GRUB_TIMEOUT="10"
GRUB_DISTRIBUTOR="`lsb_release -i -s 2> /dev/null || echo Debian`"
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash xenomai.allowed_group=1234"
GRUB_CMDLINE_LINUX=""
' >> grub
sudo mv grub /etc/default/grub

echo
echo "## Installing user space libraries"
pwd
cd polena-build/xenomai-3
sudo autoreconf
./configure --with-pic --with-core=cobalt --enable-smp #--disable-tls --disable-clock-monotonic-raw
make -j`nproc`
#sudo make install
sudo checkinstall -y -D --pkgname "xenomai-lib" --install=yes
cd ..

echo
echo "## Update bashrc"
echo '
### Xenomai
export XENOMAI_ROOT_DIR=/usr/xenomai
export XENOMAI_PATH=/usr/xenomai
export PATH=$PATH:$XENOMAI_PATH/bin:$XENOMAI_PATH/sbin
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:$XENOMAI_PATH/lib/pkgconfig
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$XENOMAI_PATH/lib
export OROCOS_TARGET=xenomai
' >> ~/.xenomai_rc

echo 'source ~/.xenomai_rc' >> ~/.bashrc

echo "## Installing Balena"
url="https://github.com/resin-os/balena/releases/download/${balena_tag}/balena-${balena_tag}-${arch}.tar.gz"
curl -sL "$url" | tar xzv -C /usr/local/bin --strip-components=1

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
