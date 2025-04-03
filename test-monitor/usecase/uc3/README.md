# UC3 - CoDeSys Virtual Control SL

Author: [Florian Hofer](https://github.com/flhofer)

**WIP --- This page is still a work in progress**

## 1. Introduction

`Virtual Control SL` is the product name run by 3S-Software for a [`Moby`](https://mobyproject.org/)-compatible container running a CoDeSys Runtime -- a so-called _Virtual_ PLC. The differences between a standard Runtime and Virtual SL are minor. More significant is that the latter is configured to be run in a generic and shared environment rather than a cut-down dedicated Linux-based kernel. This considered, Virtual Control SL would allow, e.g., for a rugged industrial PC to control multiple hardware appliances. It would permit the same level of monitoring and redundancy we are accustomed by typical containerized applications -- an enabling factor for Industry 4.0+. 

In this use-case we are thus investigating the performance of _Industrial Control Containers_ on generic hardware. We test the limits in terms of period/timing and determinism in different configurations and analyze achievable performance. In this folder we thus find:

- A folder `PLCprj` with demo-projects based on CoDeSys 3.5.x, used to test Compute and ProfiNet/EtherCat performance, two of the most used fieldbusses in industry.
- A folder `scope` with Python code to communicate with an attached oscilloscope during the tests and grab screen and waveform information from actual I/O cards. The code runs SCPY compliant commands over a VXI11 (Ethernet) terminal for Siglent/Metrix digital oscilloscopes and can easily be adapted to other models.
- A folder `container` with scripts and descriptors for Docker to manually run, compile and change `Virtual Control SL` containers - including shared networking not possible with the default product.
- Two jupyter notebooks, `TestKernel.ipybnb` and `TestVplc.ipynb`, to plot the results for the Kernel configuration and load tests used in this series.

All the scripts, where possible, are (x)sh compatible, which means they are designed to run on `ash`, `dash`, `zsh`, `ksh`, and `bash` on various Linux distributions.

