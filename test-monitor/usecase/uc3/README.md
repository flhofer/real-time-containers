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

[^1]: It is still not clear what the real differences are. So give it a try and check out [Building Custom Containers](#22-building-custom-containers)

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

While this will run the container until it is stopped (or the demo time of 2h elapses), it has limitations. The Ethernet controller eth0 used for this run is a virtual card routed to the host's network. Thus, it will not allow the use of MAC-layer-based protocols such as EtherCat or ProfiNet. If we want to use those, we must create a pass-through configuration for a physical controller, assign the NIC exclusively, or share a controller via the MACvLAN driver. We will describe both approaches in the following sections. To run containers with different settings, please adapt the above parameters as needed.

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

### 2.2 Building "custom" containers


### 2.3 Using the helper scripts


## 3. Demo Projects

## 4. Test and `scope`


