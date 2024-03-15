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



## Kernel boot parameters

**WIP**

Kernel boot parameters are parameters that are passed at system boot to the process reading the kernel image. Such parameters are typically specified in the `grub` boot configuration and can be changed as follows:[^1]

The default `grub` configuration is located at `/etc/default/grub`. Open the file with your favorite editor, e.g. `nano` and change the parameters required by adding or removing from `GRUB_CMDLINE_LINUX` and/or `GRUB_CMDLINE_LINUX_DEFAULT`, in quotes, and separated by spaces. The former always effective, the latter is added to the former in case of a normal -automatic- boot. For Ubuntu systems, the only two parameters set for `*DEFAULT` are `quiet` and `splash`.

To apply the changes and rebuild the `grub` boot menu, run `update-grub`. You can check the resulting generated boot parameters with `cat /boot/grub/grub.cfg | grep /boot`.

Please note that the contents of `grub.d` may be added and even overwrite your changes. If you wish, you can also add a file with only the `GRUB_CMDLINE_LINUX*` parameter lines in this directory and the changes are kept between kernel upgrades.

The description of parameters follows below.

[^1] for Ubuntu, other systems please read online

### Disable SMT

Simultaneous multi-threading (SMT) is a very useful feature for day-to-day operations, increasing CPU speed due to reduced flushing and filling of CPU-registers up to 30%. This, however holds only true as long as we don't need a guarantee on reaction and processing times. 
In Linux, threads that are served by the same CPU core are enumerated as separate CPU. Thus, a CPU with a computing power of 1 would be addressed as if it had a computing power of 2 or more. In reality though, not cores but only CPU-registrers are typically doubled creating therefore unforeseen delay and jitter while competing for the processing unit.

We can disable SMT at runtime by the following.

```
echo 0 > /sys/devices/system/cpu/smt/control
```

The required kernel boot parameter to make this change permanent is `nosmt`.

### Scheduler isolation

(see also CGroup `isolated` partitions)


### RCU back-off


## System runtime settings

**WIP**

### Disable SMT

See kernel boot parameters.

### Restricting power saving modes

CPUs, in addition to SMT capabilities, are able to change power and sleep states, `P-*` and `C-*` states. This means, the operating system can change speed and performance of CPUs in order to reduce power consumption. Although generally acceptable, this is a cause for jitter and unpredictability we would like to remove.

How these states are controlled depends on a component called `governor` while the actual controlling depends on a `driver`. For each CPU, the Linux VFS allows inspection and configuration of such in `/sys/devices/system/cpu`. Lets see how to detect and change governors.

We can query available governors with

```
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors
```

where `cpu0` can be changed with any CPU number. A typical output here is
```
performance powersave
```

As we desire less latency, `performance` is our choice. If we want to change this, we echo the desired setting for CPU0 as follows

```
echo "performance" > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
```

or for all at once 

```
for i in /sys/devices/system/cpu/cpu[0-9]* ; do  echo "performance" > $i/cpufreq/scaling_governor; done
```

Once done, let's check a newly introduced parameter `power/pm_qos_resume_latency_us`, which, if set, reduces the reactivity of the system. Now, true, we never should require a resume being in constant "wake" state, but just to be sure, lets verify.

```
cat /sys/devices/system/cpu/cpu0/power/pm_qos_resume_latency_us
```

The value displayed should either be `n/a` or `0`. We can change the value the same way as above.

### RT throttling, or not?

**WIP**

### IRQs and affinity

**WIP**

## Other provisions

**WIP**

### Create separate CGroup trees {#cgroup2-tree}

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

### Setting restrictions with CGroup {#cgroup-res}

There esist tools such as `taskset` to set affinity and perform cpu-pinning. Nonetheless, I believe the easiest and most structured way of managing resources is through CGroup trees. Depending on the version, the files and usage changes a little, but in short it's all about  echoing to virtual files.

For example, enter the `system` or `system.slice` folder in `/sys/fs/cgroup` and type:

```
echo "0-3" > cpuset.cpus
```

This sets the number of CPUs assigned to the system slice, and thus all tasks running in it, to CPUs 0,1,2 and 3. Note that this works on CGroups v1 only if you performed the migration steps above. In case you're not `root` but have `sudo` privileges, run `sudo sh -c "<command listed>"`.

Similarly, if your memory controller exposes multiple nodes, (see NUMA), you can limit the reserved memory nodes.

```
echo "0" > cpuset.mems
```

Please pay attention, though, that on some systems, the architecture either prohibits or limits the access of nodes among CPUs.

Once you configured the resources of your subtrees -- or groups --, you can further restrict access on the system. The change above restricts system processes to CPUs 0-3, but does not prevent other tasks to use them, too. On v1 systems you can restrict this by simply setting the `exclusive` flag of the subgroup.

```
echo "1" > cpuset.cpu_exclusive
```
Of course, this only works if no other subgroup "uses" the mentioned resources. Uses here is understood as "has been specified explicitly in the `cpuset` settings".

For v2 systems instead, we reach exclusivity by changing the partition status from `member` (..of the parent resources) to `root` (..of a new partition). Thus, once set the CPUs, we change the partition setting of our subgroup as follows:

```
echo "root" > cpuset.cpu.partition
```
We therefore remove the listed CPUs from the parent group and create a new control group `root` which handles these exclusively. They can thus only be used by the processes in this group and their children. Effective allocation can always be checked through `cat cpuset.*.*` for memory and CPU.

Please note: setting a `root` partition removes the set cpus from the availability list from the rest of the groups. If you remove or rewrite the subgroup (docker does that), it does not restore them automatically. You have to recreate the steps above and echo `member` again into a correctly configured subgroup for the resources to return.

### Switching to CGroup v2 

On `systemd`-based systems, if they still stick to CGroup v1, it is possible to manually switch to CGroup v2 using the following Kernel boot parameter `systemd.unified_cgroup_hierarchy=1`. Use the procedure described above to perform the change.

Further information can be found in the Kernel Wiki for [CGroups v1](https://docs.kernel.org/admin-guide/cgroup-v1/index.html) and [CGroups v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)

