# UC3 - CoDeSys Virtual Control SL

Author: [Florian Hofer](https://github.com/flhofer)

**WIP --- This page is still a work in progress**

## 1. Introduction

`Virtual Control SL` is the product name run by 3S-Software for a [`Moby`](https://mobyproject.org/)-compatible container running a CoDeSys Runtime -- a so-called _Virtual_ PLC. The differences between a standard Runtime and a Virtual SL are minor. More significant is that the latter is configured to be run in a generic and shared environment rather than a cut-down, dedicated Linux-based kernel. All considered, Virtual Control SL would allow, e.g., a rugged industrial PC to control multiple hardware appliances. It would permit the same monitoring and redundancy we are accustomed to by typical containerized applications -- an enabling factor for Industry 4.0+. 

In this use case, we are thus investigating the performance of _Industrial Control Containers_ on generic hardware. We test the limits regarding period/timing and determinism in different configurations and analyze achievable performance. In this folder, we thus find:

- A folder `PLCprj` with demo-projects based on CoDeSys 3.5.x, used to test Compute and ProfiNet/EtherCat performance, two of the most used fieldbuses in the industry.
- A folder `scope` with Python code to communicate with an attached oscilloscope during the tests, and grab the screen and waveform information from actual I/O cards. The code runs SCPY-compliant commands over a VXI11 (Ethernet) terminal for Siglent/Metrix digital oscilloscopes and can easily be adapted to other models.
- A folder `container` with scripts and descriptors for Docker to manually run, compile and change `Virtual Control SL` containers - including shared networking not possible with the default product.
- Two Jupyter notebooks, `TestKernel.ipybnb` and `TestVplc.ipynb`, to plot the results for the Kernel configuration and load tests used in this series.

Where possible, all the scripts are (x)sh compatible, which means they are designed to run on `ash`, `dash`, `zsh`, `ksh`, and `bash` on various Linux distributions.

## 2. CoDeSys Containers

In this section, we quickly discuss how to control and build CoDeSys containers and introduce the scripts created to simplify these operations. All scripts and files required for the following can be found in the `container` subfolder.

### 2.1 Running CoDeSys Containers

To manually run a CoDeSys container, you need to connect with the target machine over a remote shell, i.e., `ssh`, and be able to issue `docker` commands. 
If you installed the `Virtual Control SL` images on the target machine, you should see them listed by issuing the following command.

```
docker images ls
```
The images are listed as `codesyscontrol_virtuallinux` or `codesysedge_virtuallinux`, where the tag after the colon indicates the version. Multiple versions may be installed; you can choose which one to launch when issuing the run command. If you don't see any images, you either need to install them through the CoDeSys IDE, the deb package inside the installer (it is a zip), or build your `Control SL` runtime in a container to act as VirtualControl[^1]. Start a container, for example, for the vPLC `vplc1`, by typing the following.

[^1]: It is still not clear what the real differences are. So give it a try and check out [Building Custom images](#22-building-custom-images)

```
docker run --rm -td --name vplc1 -v /var/opt/codesysvcontrol/instances/vplc1/conf/codesyscontrol:/conf/codesyscontrol/ -v /var/opt/codesysvcontrol/instances/vplc1/data/codesyscontrol:/data/codesyscontrol/ --cap-add=IPC_LOCK --cap-add=NET_ADMIN --cap-add=NET_BROADCAST --cap-add=SETFCAP --cap-add=SYS_ADMIN --cap-add=SYS_MODULE --cap-add=SYS_NICE --cap-add=SYS_PTRACE --cap-add=SYS_RAWIO --cap-add=SYS_RESOURCE --cap-add=SYS_TIME codesyscontrol_virtuallinux:4.14.0.0 -n eth0
```

This command will do the following:
* Create a new container vplc1 (`--name vplc1`) that will automatically be removed after stop (`--rm`)
* Attach an output terminal and run the container as a daemon in the background (`-td`)
* Map the local configuration and data folders for the vPLC1 inside the container for access (`-v ...`) to keep settings and program persistent after removal of the container
* Add the required capabilities (`--cap-add=...`) to interact with the system
* Use the base image `codesyscontrol_virtuallinux` with version `4.14.0.0` for the PLC runtime
* Attach eth0 as the main Ethernet controller (`-n eth0`)

While this will run the container until it is stopped (or the demo time of 2h elapses), it has limitations. The Ethernet controller `eth0` used for this run is a virtual card routed to the host's network. Thus, it will not allow the use of MAC-layer-based protocols such as EtherCat or ProfiNet. If we want to use those, we must create a pass-through configuration for a physical controller, assign the NIC exclusively, or share a controller via the MACvLAN driver. We will describe both approaches in the following sections. To run containers with different settings, please adapt the above parameters as needed.

If you would like to learn more about Docker containers and their use inside a shell, consult my [introductory tutorial on Docker](https://github.com/flhofer/docker_tutorial)

#### 2.1.1 Attaching a network card to a container exclusively

If we would like a vPLC to have exclusive access to a network card, we must follow the following steps **after** starting the container by passing the controller's name through `-n` :

Do the following for a container named `vplc1` and card `en2sp0`:
* Obtain the PID of the main process `docker inspect -f '{{.State.Pid}}' vplc1`
* Attach a namespace to the process, `ip netns attach vplc1_netns_net <PID>`. The name can be anything-
* Add our controller to this exclusive namespace `ip link set en2sp0 netns vplc1_netns_net`
* Activate controller `ip netns exec vplc1_netns_net ip link set en2sp0 up`
* Enable promiscuous mode `ip netns exec vplc1_netns_net ip link set en2sp0 promisc on`

To release the card once the container is stopped, type `ip netns del vplc1_netns_net`. If you'd like to do these steps automatically, please take a look at the helper script section.

#### 2.1.2 Using the MACvLAN driver for Docker containers

The MACvLAN driver also allows the passthrough of a network controller. In addition, it allows multiple L2s to be attached to a single physical layer. This means we can create various network cards, e.g., one per Container, using a single shared physical level.

To use MACvLAN in Docker, we must create a new Docker network configuration with that driver. For example, using `en2sp0` as a physical card again, we can create a new network, e.g., `vplc-en2sp0,` (name is free choice) like this.

```
docker network create --driver=macvlan -o parent=en2sp0 --attachable vplc-en2sp0
```
> [!Note]
> Replace `en2sp0` with your card name as found with `ip link` throughout the rest of this section to make it work for your system.

The new network will be created in `bridge` mode by default, permitting multiple containers to share the parent network card. If you would like another operating mode, e.g., passthrough, specify this with `-o macvlan_mode=passthru`. To use this new network, run vPLC containers with the `--network=<networkname>` parameter to attach the container to the created MACvLAN network. Each container will now be connected at the OSI level 2, obtaining a different MAC address, and consequently different level 3 adapters and IP addresses. The MAC addresses are accessible from the outside, and we are thus able to run multiple ProfiNET controllers and devices, as well as multiple EtherCAT masters as vPLC containers.

Without additional parameters, this command will add a new `172.X.0.0/16` network and Docker will progressively assign container IPs. For example, with `172.18.0.0/24` as the new network (you can check with `docker inspect vplc-en2sp0`), the first container will be `172.18.0.2`. This, however, is not a desired behavior when working with EtherCAT or ProfiNET, as the IPs of the containers depend on the start order, not the configuration. We must define an IP for a `user-defined` network starting with the `--ip=` parameter.

User-defined Docker networks may also define a subnet for containers. For example, if we would like to add subnet `192.168.32.0/24` as our new vPLC network, we can do so by typing the following.

```
docker network create --driver=macvlan --subnet=192.168.3.0/24 -o parent=en2sp0 --attachable vplc-en2sp0
```

If no gateway is specified with `--gateway=x.x.x.x`, the containers will use the gateway of the parent controller, if available. If no IP is set, the IPs are given progressively like before. 

The subnet specified can be of many kinds. You can pass a portion of the parent card's subnet. For example, if the parent network is `192.168.5.0/24`, with the controller `192.168.5.2`, we could assign `192.168.5.128/26` to the new MACvLAN network, thus mapping the area 192.168.5.130...192.168.5.191 for use with Docker containers. (192.168.5.129 is reserved as "gateway"). This may also work if the network is served by a DHCP server and the range 128...192 is reserved as static (essential to avoid double assignment of IP addresses). An even better solution would be to pass the complete subnet, e.g., `--subnet=192.168.5.0/24`, and then set the container range to `--ip-range=192.168.5.128/26` to define that only that subnet range is available for container allocation. The range can also be something completely different. Remember, we are working on OSI level 2 here, so we can use different subnets for our vPLCs. We are, however, limited to one MACvLAN per physical device and can thus not create a second Docker network for vPLC on the same parent controller. If you would like two or more networks for vPLCs, you need to use multiple hardware cards.

With the configuration steps above, you can now communicate freely with the outside world. However, the vPLCs are not reachable from within the host. This is due to how the MACvLAN driver works and how it is attached to the network stack. If we want to communicate with our containers, our host must be part of the pool of virtual network devices in the subnet above. Thus, we add a local link named `br-vplc-en2sp0` (arbitrary) on  our card `en2sp0`.

```
ip link add br-vplc-en2sp0 link en2sp0 type macvlan mode bridge
```
This new link also needs an IPv4 address to communicate. If we are in a DHCP-served network, we can use `dhclient br-vplc-en2sp0` to obtain one. Otherwise, we manually configure an IP 192.168.5.129 as follows.

```
ip addr add 192.168.5.129/32 dev br-vplc-en2sp0
ip link set dev br-vplc-en2sp0 up
ip route add 192.168.5.0/24 dev br-vplc-en2sp0
```

The last line is important; it tells our system that the vPLC subnet is now reachable through this link.

#### 2.1.3 Using multiple networks in a container

If you want to use multiple interfaces, e.g., for PN and EtherCAT, or a service connection in addition to the interface configured above, you must manually create further networks. This is as user-defined and automatic networks like `docker0` can not be mixed. To make a second MACvLAN network, redo the steps above. The passthrough option already adds a second interface to the existing `eth0`. Let's do a service network instead.

CoDeSys offers multiple services, such as OPC-UA, for remote control and diagnosis. To add a network to funnel all this traffic, type the following.

```
docker network create --driver=bridge --attachable -o com.docker.network.bridge.name="vplc-service" vplc-service 
```
As we are not specifying subnet, this will create a new bridged subnet in the `172.x.0.0/16` range. To now run a container with both networks attached start the `docker run` command with the following parameters.


* For our new service network `--network=vplc-service`
* For the previous vPLC network `--network=name=vplc-en2sp0,ip=192.168.5.130` we now combine the name and IP in one, as Docker would not know what to set. It may also be a good idea to set the service network IP manually, as we may use it for maintenance and lower-priority communication.

The internal controller names are `eth0` and `eth1` respectively, given (apparently) based on the alphabetical order of the Docker network names. 

#### 2.1.4 Configuring the IDE to use MACvLAN

We have multiple options to configure a container to run with a network using MACvLAN. When connected with `Delpoy SL` to the host, edit the container configuration (click Config). The parameters that are of interest are `Network`, `NIC`, and `Generic parameters`. 

The parameters can be set in multiple ways. In our example above, we have two networks and `en2sp0` as the network card.
* Use `vplc-service` as `Network` and add the IP and the second network using the Generic field with `--network=name=vplc-en2sp0`. Here, the service network has an IP in sequence, and the NIC is `eth0`.
* Use `name=vplc-en2sp0,ip=192.168.5.130` as `Network` and add the second network using the Generic field with `--network=name=vplc-service`. Here, the service network has an IP in sequence, and the NIC is `eth0`.
* Use `name=vplc-en2sp0,ip=192.168.5.130` as `Network` and add the IP and the second network using the Generic field with `--network=name=vplc-service,ip=172.18.0.2`. The NIC is `eth0`.

You notice the pattern. 

> [!Note]
> The MACvLAN network must be created using the Docker command line, as the IDE cannot perform such an operation.

### 2.2 Building "custom" images

To create a custom image, we can use a Dockerfile, for example, the file `Dockerfile_Tools_vPLC` in the `container` directory. This file takes the base image of CoDeSys and adds `iproute2` and `tcpdump`, allowing you to run a shell in the container and capture traffic directly at the endpoint of a pass-through network controller.

We can build this new image with the `build` command.
```
docker build -f ./container/Dockerfile_Tools_vPLC -t codesyscontrol_virtuallinux:4.14.0.0-tools .
```

This will create a new image that follows the `codesyscontrol_virtuallinux` format, but with a new version tag. We added `-tools` to the original tag, and if you open the text file, you can see that it is based on the `4.14.0.0` runtime container version.

Two other example files are in the directory. They build a new image containing the `CoDeSys Control for Linux` package—in short, it creates a container version of the SoftPLC runtime and the standard gateway. Edit the files to use different versions of the installers. It is unclear how much the `Virtual SL` differs from the SoftPLC version.

For more examples on image build and further instructions, e.g., how to enter a running container and run `tcpdump`, please take a look at [my tutorial](https://github.com/flhofer/docker_tutorial) and the [Docker official documentation](https://docs.docker.com/build/).

### 2.3 Using the helper scripts

The folder `container` contains a set of shell scripts that simplify and automate many of the steps above. Their function is as follows.

* `vplc_cont.sh` for generic container control. It allows you to start, stop, configure a network as a bridge or Mavclan, and more
* `vplc_duplicate.sh` is a little helper to duplicate a container for multiple identical instances, using `/tmp/` as a relay for temporary files to speed up file writes (see test descriptions)
* `vplc_test.sh` is used to automate the test execution of one or more vPLCs and scope sampling.

For more details, see the help description of the shell menu (`<command> help`).

## 3. Demo Projects

The folder `PLCprj` contains three CoDeSys projects: `Task-Read`, which performs a CPU-load test and logs the results, `TestIO` used to do the same but with additional ProfiNET I/O, and finally, `TestIO_EC` which is a variant of the previous for EtherCat networks. I've shared a quick description of the projects below.

### Task-Read

This project generates pure, configurable CPU load using Fibonacci numbers, and logs runtime and period to a file. We use the `CmpLog` library to write to a file called `tasktimes.log`. For such logging to work, however, we must add the configuration lines for `CmpLog` in the file `CODESYSControl_User.cfg.inc` to the Runtime configuration, which means the vPLC container. The container configuration file can be found in `/var/opt/codesysvcontrol/instances/<instancename>/conf/codesyscontrol/`.

A further, optional optimization we did for our performance tests is creating a symlink from the instance folders to `/tmp` and mapping `/tmp` inside the vPLC containers—this way, the logging is done to RAM and should introduce less I/O delay.

### TaskIO and TaskIO_EC

In addition to the above, this project adds a ProfiNET I/O or EtherCat coupler with digitalk I/Os. The idea is that a function generator is attached to an input, the signal inverted, and then the output is compared with the input in terms of delay and shift (jitter). With the help of the scope scripts (see below), we can automatically acquire the scope image of such tests.

## 4. Test and `scope`


