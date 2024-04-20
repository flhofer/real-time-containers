# Real-Time test containers based on RT-app

This folder contains scripts and confiiguration files to test an environment and its determinism for real-time operations. The provided scripts will automatically run a series of tests with alered run-time parameters and test and log the behaviour of the system under load.

## Test scripts and configuration files

The directory contains the following files:

- _rt-app_ the binary provided by scheduler tools to simulate tasks and system load. We will use this to create different situations of stress and scheduling periodicity. Attention, use the customized version at [flhofer-Github](https://github.com/flhofer/rt-app/tree/fix-calib-res) to operate in higher and more stable calibration resolutions.
- _containers.sh_ operates the test containers such as builing the test image including `rt-app`, starting and stopping containers and so on. This script is also used by the others for container maipulation.
- _test.sh_ executes the test batches. It relies on filenames for rt-app configurations to start and execute the test containers. (see below) 
- _kerneltest.sh_ is the main script that provides for the modification of boot parameters, system reboot and test run or resume (through cron). WARNING: it is configured for kernel `4.19` and has not been updated yet.
- _Dockerfile_ is the file containing the build script for the test containers. At the moment of writing, this script will *copy* all needed files into the container and are thus not updated later on.
- _rt-app-*.json_ task configuration files for rt-app.s
- _rt-app-tst-*.json_ rt-app configuration files/links for the specified container. E.g. `rt-app-tst-30.json` refers to test batch 3 container 1. 
- _config-*.json_ Orchestrator configurations to use with this setup if orchestration is desired, for the T3, C5 or Bare metal instances of the test set

## Usage

The scripts in the present are to be used as follows:

### Full test

To run a full test, i.e., start a kernel boot parameter test, we input the following

```
./kerneltest.sh start
```

This will set kernel parameters and reboot when needed, and then run all 4 test batches each time using rt-app containers with test threads, and store the detailed logs for each test batch and container in the `log` directory.  At the time of writing there are 12 parameter combinations in the kernel test set.

### Test batch run

The kernel test above will use `test.sh` to run container batch tests. It is however possible to run the test batch one time only. If and additional argument is given, only the test batches upto x are run. For example, `./test.sh 3` will run test batches 1,2 and 3.

In its actual configuration the test batches are composed as follows.

- batch 1: 10x 0.9ms runtime, 10ms period containers
- batch 2: 2x 2ms runtime, 5ms period
- batch 3: 1x 2ms runtime, 5ms period, 1x 3ms runtime, 9ms period, 1x 0.9ms runtime, 10ms period
- batch 4: 10x 10ms runtime, 100ms period

### Container operation

The `containers.sh` script allows to create and update the test container image, as well as start, stop or kill a(ll) running test containers. Using command `test` instead of `start`, the script will also run the `orchestrator` app for container monitoring. It will thus require a correctly placed `config.json` file to run properly.

Run `./containers.sh help` for help on usage.

