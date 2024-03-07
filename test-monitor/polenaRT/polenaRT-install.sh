#!/bin/sh

set -e

# Long term Kernel Suggestions
#   - 22.04 LTS -> 6.1.77 - rt24
#   - newest LT 6.6.15 - rt22
# Latest Stable  6.7 - rt6 ( 6.7.4? )

linux_patch=${1:-'6.1.77-rt24'}
balena_tag=${2:-'v20.10.19'}
docker_tag=${3:-'v25.0.3'}

repo_location="flhofer/real-time-containers"
repo_branch="develop"

# Check and warn about missing required commands before doing any actual work.
abort=0
for cmd in tar; do
	if [ -z "$(command -v $cmd)" ]; then
		cat >&2 <<-EOF
		Error: unable to find required command: $cmd
		EOF
		abort=1
	fi
done
[ $abort = 1 ] && exit 1

# Find package manager
abort=1
for cmd in apt apt-get apk opkg rpm; do # TODO: update with system dependend package manager variable
	if [ ! -z "$(command -v $cmd)" ]; then
		cat <<-EOF
		Using $cmd for package management
		EOF
		case "$cmd" in
			"apk"*)
				pgkmgmt="$cmd add --no-interactive"
				;;
			*)
				pgkmgmt="$cmd install -y"
				;;
		esac
		pgkmgmt_u="$cmd update"		
		abort=0
		break;
	fi
done
if [ $abort = 1 ] ; then
	cat >&2 <<-EOF
	Error: unable to find package manager command from list: $pkg_list
	EOF
fi	

# Detect distro to select package names
packages1="autoconf automake libtool pkg-config bison flex bc rsync kmod cpio gawk dkms llvm zstd"
packages2="libssl-dev libudev-dev libpci-dev libiberty-dev libncurses5-dev libelf-dev"
packages3="curl jq"
release=$( cat /etc/*-release )
case $release in 
	*Alpine* )
		packages1=$(echo "$packages1" | sed -n 's/dkms/akms/p')
		packages1=$(echo "$packages1" | sed -n 's/pkg-config/pkgconfig/p')
		packages2="openssl-dev eudev-dev libpciaccess-dev ncurses-dev elfutils-dev"
		#could not find libiberty-dev
		;;
#	*Ubuntu* 
#	*Debian* 
#	*Fedora* 
#	*Red Hat* 
#	*Mint* 
#	*SuSe* 
#	*OpenSuse* 
	*)
		;;
esac

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

if false; then
#################################
# Required system packages 
#################################
echo
echo "## Installing dependencies..."
$sudo $pgkmgmt_u
# Tools for building
$sudo $pgkmgmt $packages1
# Dev Libraries for building
$sudo $pgkmgmt $packages2
# Tools for this script 
$sudo $pgkmgmt $packages3
fi

# Check if kernel config exists
config_file="ubuntu-${machine}-${linux_patch}.config"

if [ ! -f "$config_file" ]; then
	echo "Warning: required Kernel config file '${config_file}' does not exist!" >&2

	echo "Fetching list from repository..."
	versions=$(curl -H GET "https://github.com/${repo_location}/tree/${repo_branch}/test-monitor/polenaRT" | jq  '.payload.tree.items[] | select ( .name | contains ( ".config" )) | select ( .name | contains ( "'${machine}'" )) | .name | sub (".config";"") ' - )

	if [ -z "${versions}" ]; then
		echo "Error: Could not find valid Kernel config file!" >&2
		exit 1
	fi
	
	i=0
	for v in ${versions} "Cancel"; do i=$(( ${i}+1 )); echo "$i) $v"; done  
	sel=-1
	until [[ $sel =~ ^[0-9]+$ ]] && [ $sel -le ${i} ]  ; do
		read -p "select Version (1-${i}): " sel
	done
	
	i=0
	for v in ${versions} "Cancel"; do 
		i=$(( ${i}+1 ));
		if [ $i -eq $sel ]; then
			version=$v
			break;
		fi
	done

	if [[ -z "${version}" || "$version" == "Cancel" ]]; then
		echo "Error: No valid Kernel config selected!" >&2
		exit 1
	fi
	# strip from quotes
	version=${version#\"}
	version=${version%\"}
	
	# generate linux patch string
	linux_patch=$(echo "$version" | sed -n 's/\([a-zA-Z0-9\_]*\-\)\{2\}\(.*\)/\2/p')
	
	curl -H GET "https://raw.githubusercontent.com/${repo_location}/${repo_branch}/test-monitor/polenaRT/${version}.config" > ${version}.config
fi

# parse linux patch string to find sub-elements for wget
rt_patch=$(echo "$linux_patch" | sed -n 's/\([0-9.]*-\)*\(.*\)/\2/p')
linux_ver=$(echo "$linux_patch" | sed -n 's/\([0-9]*\(\.[0-9]*\)\{1,2\}\).*/\1/p')
linux_root=$(echo "$linux_ver" | sed -n 's/\([0-9]*\).*/\1/p')
linux_base=$(echo "$linux_ver" | sed -n 's/\([0-9]*\.[0-9]*\).*/\1/p')
balena_rev=$(echo "$balena_tag" | sed 's|+|.|g')

cat <<EOF
*** Polena installer ...

Selecting RT-patch < ${rt_patch} > for 
	*   kernel root family ${linux_root}
	**  base version ${linux_base}
	*** release ${linux_ver}
EOF
exit 0

# if xconfig...
#$sudo apt-get install -y qt5-default
# if create-pkg...
#$sudo apt-get install -y checkinstall

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

if [ ! -f "patch-${linux_patch}.patch.xz" ]; then
	echo
	echo "## Downloading RT patch"
	wget https://www.kernel.org/pub/linux/kernel/projects/rt/${linux_base}/patch-${linux_patch}.patch.xz

fi

echo
echo "## Patching Linux Kernel"
cd linux-${linux_ver}
xzcat ../patch-${linux_patch}.patch.xz | patch -p1

# Check if keyconf is present, otherwise create and generate key
if [ ! -e "./certs/default_x509.genkey" ]; then

	echo "## Create signing key"

	echo -e "[ req ]
default_bits = 4096
distinguished_name = req_distinguished_name
prompt = no
string_mask = utf8only
x509_extensions = myexts

[ req_distinguished_name ]
#O = Unspecified company
CN = Build time autogenerated kernel key
#emailAddress = unspecified.user@unspecified.company

[ myexts ]
basicConstraints=critical,CA:FALSE
keyUsage=digitalSignature
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid" > x509.genkey

	openssl req -new -nodes -utf8 -sha512 -days 36500 -batch -x509 -config certs/default_x509.genkey -outform DER -out certs/signing_key.x509 -keyout certs/signing_key.pem

fi

echo
echo "## Compiling kernel"
cp ../../${config_file} .config
yes "" | make oldconfig
make -j$(nproc) deb-pkg LOCALVERSION=
cd ..

echo "Interrupting install of kernel/container daemon - temp" 
exit 1

echo
echo "## Installing kernel" # TODO: make distribution independent 
$sudo dpkg -i linux-headers-${linux_patch}.deb linux-image-${linux_patch}.deb

echo
echo "## Configuring GRUB"
#${sudo} sed -i -e 's/^/#/' /etc/default/grub # comment out previous GRUB config
$sudo cp /etc/default/grub /etc/default/grub.backup
echo '
GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux '${linux_patch}'"
GRUB_HIDDEN_TIMEOUT_QUIET="true"
GRUB_TIMEOUT="10"
GRUB_DISTRIBUTOR="`lsb_release -i -s 2> /dev/null || echo Debian`"
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
GRUB_CMDLINE_LINUX=""
' >> grub
$sudo mv grub /etc/default/grub
$sudo update-grub2

url=
url2=
echo "Which container daemon to install ?"
select ins in "Docker ${docker_rev}" "Balena ${balena_rev}" "None"; do
    case $ins in
        Docker )	echo "## Installing Docker"
        			url="https://github.com/balena-os/balena-engine/releases/download/${balena_rev}/balena-engine-${balena_rev}-${arch}.tar.gz"; break;;
        Balena )	echo "## Installing Balena"
        			url="https://download.docker.com/linux/static/stable/${machine}/docker-${docker_rev}.tgz";
        			url2="https://download.docker.com/linux/static/stable/${machine}/docker-rootless-extras-${docker_rev}.tgz"; break;;
        None ) 		echo "## Exiting"
					exit;;
    esac
done 

if [ ! -z "$url" ] ; then
	curl -sL "$url" | $sudo tar xzv -C /usr/local/bin --strip-components=1
	if [ ! -z "$url2" ] ; then
		curl -sL "$url2" | $sudo tar xzv -C /usr/local/bin --strip-components=1
	fi
fi

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

To use balenaEngine you need to start balena-engine-daemon as a background process.
This can be done manually or using the init system scripts provided here:

    https://github.com/balena-os/balena-engine/tree/$tag/contrib/init

This requires adding a \"balena-engine\" group for the daemon to run under:

    sudo groupadd -r balena-engine

If you want to allow non-root users to run containers they can be added to this group
with something like:

    sudo usermod -aG balena-engine <user>

WARNING: Adding a user to the \"balena-engine\" group will grant the ability to run
         containers which can be used to obtain root privileges on the
         docker host.
         Refer to https://docs.docker.com/engine/security/security/#docker-daemon-attack-surface
         for more information.

EOF
