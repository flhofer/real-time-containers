# polenaRT - a real-time environment for PREEMPT-RT

Author: [Florian Hofer](https://github.com/flhofer)

**WIP**

## 1. Introduction

`polenaRT` is the name of the environment for running `Moby`-compatible containers and container engines in real-time with the (optional) help of a userspace orchestrator. This environment foresees the following components and tasks found in the present folder:

- An install script `polenaRT-installer.sh` is used to build and configure custom kernel builds starting from Linux vanilla. If the target kernel is for a different distribution, it sets up a docker container for the required environment and builds the kernel there. It furthermore installs the selected container engine and, if of interest, the kernel with optimized parameters. (**TBD**)
- a configuration script to reconfigure the runtime environment and kernel boot parameters to state-of-the-art latency and jitter reduction settings - `polenaRT-configure.sh`. The list and a description of possible system optimizations are described below. (**TBD**)
- several optimized kernel configurations for selected operating systems and kernels.

All the scripts are (x)sh compatible, meaning they are designed to run on `ash`, `dash`, `zsh`, `ksh`, and `bash` on various Linux distributions.

Further information on the use and parameters for the mentioned scripts can be found in the `README`.

## 2. Environment optimization overview

Parameter optimization can occur at several levels. As we have to compile a kernel from the source to apply the `PREEMPT-RT` patch, we can also use this opportunity to fiddle with some levers and switches to optimize RT behavior for our use case. Once compiled, boot parameters and runtime configurations exist that further influence the system's responsiveness. We will, therefore, consider the following topics for optimization:

- Kernel configuration parameters at build time
- Kernel configuration parameters at runtime (boot)
- System parameters at runtime
- Other application provisions to improve runtime

Please note that all but the first can be changed on an existing machine without further ado. Refer to the instructions below to manually apply those changes. For automation, use `polenaRT-configure.sh`

## Kernel build parameters 

**WIP**

### Kernel Tick Rate


### RCU and Spinlocks



## Kernel runtime parameters

**WIP**

### Scheduler isolation

(see also CGroup `isolated` partitions)


### RCU back-off


## System runtime settings

**WIP**

### IRQs and affinity


## Other provisions

**WIP**

### Create separate CGroup trees {#cgroup2}

Recently, Kernel and Docker configuration switched to CGroup v2 with `systemd` driver. As a result, while for Cgroup v1 the daemon created a dedicated `docker` tree, now all containers will appear as part of the `system.slice` tree together with all other system tasks. This hinders the possibility of assigning dedicated CPUsets or memory to the container daemon and system tasks -- unless you do it manually -- for all containers running and starting.

To create a separate `slice`, proceed as follows.
Edit or create a text-file named `daemon.json` located in most cases in `/etc/docker/`. If you use the Ubuntu snap version of docker, you may need to edit the file in `/var/snap/docker/current/config`. It will access and modify the mounted file for the user running the snap. Add `"cgroup-parent": "docker.slice"`, which with default settings should result as:

```json
{
    "cgroup-parent":    "docker.slice",
    "log-level":        "error"
}
```

Restart the docker daemon for changes to take effect.
`systemctl restart docker` or `snap restart docker` -- sudo where needed.
With this change, new and running containers will depend on the restrictions for the `docker.slice`, "inheriting" also its resource assignment, e.g. cpu(set) affinity.

### Moving system tasks Cgroup {#cgroup-system}

A futher step to better control latency and jitter is to isolate tasks performing system duties from tasks running inside our containers. Typically, as the docker case, this is done with control groups. Once the group is established, it is possible to reduce the resources assigned to system tasks and exclusively assign some to the running of containers.

As said in the previous section, recent system configurations moved to Cgroup v2. This new setup automatically creates a `system.slice` and `user.slice` containing all system and user tasks, respectively. However, for those still using the older VFS, here are some steps you can perform to move all non container-related tasks in a separate control group.

Inside the `/sys/fs/cgroup/` directory, enter one of the controller trees, e.g. `cpuset`, and do the following (with privileges):
```
mkdir system
cat tasks > system/tasks
```
Tasks contains a list of PIDs that are running and assigned to the present control group, by default the `root` control group. by creating a directory, we basically create a subgroup. Echoing the PID numbers into its tasks file will thus move the assignment from `cpuset`-cgroup `root` to `system`. 

N.B. Docker will automatically create a CGroup v1 subgroup `docker`, thus allowing us, by default, to control resources assigned to  daemon and containers.
