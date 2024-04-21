# polenaRT - a real-time environment for PREEMPT-RT

Author: [Florian Hofer](https://github.com/flhofer)

**WIP --- This page is still a work in progress**

## 1. Introduction

`polenaRT` is the name of the environment for running [`Moby`](https://mobyproject.org/)-compatible containers and container engines in real-time with the (optional) help of a userspace orchestrator. This environment foresees the following components and tasks found in the present folder:

- An install script `polenaRT-installer.sh` is used to build and configure custom kernel builds starting from Linux vanilla. If the target kernel is for a different distribution, it sets up a docker container for the required environment and builds the kernel there. It installs the selected container engine and, if of interest, the kernel with optimized parameters. (**TBD**)
- a configuration script to reconfigure the runtime environment and kernel boot parameters to state-of-the-art latency and jitter reduction settings - `polenaRT-configure.sh`. The list and a description of possible system optimizations are described below.
- several optimized kernel configurations for selected operating systems and kernels.

All the scripts are (x)sh compatible, which means they are designed to run on `ash`, `dash`, `zsh`, `ksh`, and `bash` on various Linux distributions.

Further information on the use and parameters for the mentioned scripts can be found in the `README`.

## 2. Environment optimization overview

Parameter optimization can occur at several levels. As we have to compile a kernel from the source to apply the `PREEMPT-RT` patch, we can also use this opportunity to fiddle with some levers and switches to optimize RT behavior for our use case. Once compiled, boot parameters and runtime configurations exist that further influence the system's responsiveness. We will, therefore, consider the following topics for optimization:

- Kernel configuration parameters at build time
- Kernel configuration parameters at runtime (boot)
- System parameters at runtime
- Other application provisions to improve runtime

All but the first can be changed on an existing machine without any further ado. Refer to the instructions below to manually apply those changes. For automation, use `polenaRT-configure.sh`. It will automatically configure your system with the best-known configuration (if the kernel build parameters allow it).

Further reads: [Ubuntu PRO - real-time kernel technical](https://ubuntu.com/blog/real-time-kernel-technical), 
[Ubuntu PRO real-time - kernel tuning](https://ubuntu.com/blog/real-time-kernel-tuning)

## Kernel build parameters 

Before compiling a kernel, the user can flip one of the hundreds of switches available for kernel configuration. These flags include real-time relevant settings such as the level of allowed preemption.

The easiest way to configure such parameters is through the built-in menu, shown via `make menuconfig`, which runs inside the root of the kernel source. If no configuration exists yet (not loaded from the file or copied into the source folder as `.config`, all settings are set to default. It is worth noting that the default here are the settings for the vanilla kernel, not your distribution flavor. To check the kernel configuration of the running system, check for a `config-*` file in either `/boot` or `/etc`. To keep the settings of your actual system, you can copy the file to the source root as `oldconfig` and run `yes "" | make oldconfig` to apply the same settings to your new source code.

As said, the probably most important setting here is the preemption level which can be found in the `General setup` menu as `Preemption Model`. The default here is `Voluntary preemption`, which is best for most standard servers and desktop computers. `No forced preemption` would put the system towards a batch-programmed server, in which any interruption would be additional overhead and delay as the processes are planned ahead of time. A `Preemptible kernel` is a solution that adds some further preemption points, a merge mostly of the `PREEMPT-RT` effort into the mainline kernel to reduce the overall latency of a system that might need to be more responsive and switch tasks more often. Finally, the `Fully preemptible kernel` enables all preemption points available (only visible with the `PREEMPT-RT` patch), reducing the added latency until preemption and thus our choice[^1]. 

The kernel `.config` file will thus be changed as follows 

```
# CONFIG_PREEMPT_VOLUNTARY is un-set
# CONFIG_PREEMPT_BUILD is un-set
# CONFIG_PREEMPT_VOLUNTARY is un-set
# CONFIG_PREEMPT_DYNAMIC is un-set
CONFIG_PREEMPT_LAZY=y
CONFIG_PREEMPT_RT=y
CONFIG_ARCH_SUPPORTS_RT=y
```

Something that might not be clear is that enabling the fully preemptive model, `CONFIG_PREEMPT_RT` — or `CONFIG_PREEMPT_FULL` on older kernels — will also change other parameters. Most notably, it has a big influence on locks and memory.

```
# CONFIG_UNINLINE_SPIN_UNLOCK is un-set
# CONFIG_QUEUED_RWLOCKS is un-set
CONFIG_RCU_BOOST=y
CONFIG_RCU_BOOST_DELAY=500
```

Spinning locks in RT kernels are ["sleep-able"](https://wiki.linuxfoundation.org/realtime/documentation/technical_details/sleeping_spinlocks), meaning that the replacing implementation now allows spinning locks to be preempted (note—this could affect drivers!). Read-Copy-update foresees now a priority boost function as real-time tasks may otherwise defer their execution indefinitely. NUMA balancing and transparent huge pages (THP) are also disabled.

```
# CONFIG_NUMA_BALANCING is un-set
# CONFIG_NUMA_BALANCING_DEFAULT_ENABLED is un-set

# CONFIG_TRANSPARENT_HUGEPAGE is un-set
# CONFIG_ARCH_ENABLE_THP_MIGRATION is un-set
# CONFIG_TRANSPARENT_HUGEPAGE_MADVISE is un-set
# CONFIG_THP_SWAP is un-set
```

Other affected parameters, for completeness only.

```
# CONFIG_SOFTIRQ_ON_OWN_STACK is un-set
CONFIG_PAHOLE_VERSION=0 # parameter is set to 0
CONFIG_HAVE_ATOMIC_CONSOLE=y

CONFIG_COMPACT_UNEVICTABLE_DEFAULT=0 # set to 0 from 1

# CONFIG_NET_RX_BUSY_POLL is un-set
```

You can search for configuration symbols inside the `menuconfig` using the `/` key. 

[^1]: If the last entry is not available, despite applying the patch, you will need to enable expert mode first, see below.

### Enabling of expert mode

Newer kernels require Expert mode to be enabled to make certain features visible. In some cases, entering `General setup` and selecting `Embedded system` might be enough. If this is unavailable, open the resulting `.config` file in the root folder and find/add `CONFIG_EXPERT=y` to enable the menu entries.

### Changing kernel Tick Rate

On a system with a task scheduler, a tick rate defines how frequently and at which interval the execution is interrupted (see interrupts below) to verify if the scheduled task still holds priority. Therefore, we are not waiting for a task to yield but rather preempting it at regular intervals.

The correct value for this setting depends widely on your application. A typical RTOS embedded system, Bachmann's M200 PLC-family, runs tick rates between 2000 and 5000Hz. This means the scheduler interrupts the running task up to 5000x per second, i.e., every 0.2 milliseconds. At interrupt, the waiting tasks are surveyed, and the one in front of the queue with the highest priority is run next[^3]. If, instead, you have EDF-scheduled tasks only, you ideally do not interrupt at all. Their priority won't change unless new tasks arrive or old ones leave. How do we determine the best tick rate, then? (see `Processor type and features->Timer frequency`)

```
# CONFIG_HZ_100 is not set
CONFIG_HZ_250=y
# CONFIG_HZ_300 is not set
# CONFIG_HZ_1000 is not set
CONFIG_HZ=250
```

The Linux kernel default is 250Hz, while the maximum is 1000Hz and the minimum is 0Hz ("no_hz"). Select the tick rate that better suits your system. If you have any doubt, err on the higher value. The scheduler interrupts for EDF tasks add interruptions, but they are predictable. The pace and duration of a scheduler interrupt, especially if the schedule remains the same, is constant. For EDF, thus, higher rates equal some constant extension of the worst-case execution time (WCET). For traditional FIFO and RR tasks, instead, they might radically improve the reactivity of the system[^4].

A note on 0Hz, or disabled scheduler tick `CONFIG_NO_HZ_FULL`. No interruption at all may only be viable in very limited scenarios. This might be your choice if you have CPUs with only one task running. Maybe even with pure EDF scheduled tasks, it may be worth a try, as the schedule is constant and a yield returns to the scheduler (tested with Kernel 4.16, was not working then - see wiki[^nohz], it's a **WIP**). However, it seems non-viable in shared resources where multiple containers are scheduled to share CPU time. If you wish to use this option, you must use `nohz=on` and `nohz_full=<list-cpus>` boot parameters. This setting automatically offloads RCU callbacks on the listed CPUs (see below). It is a good practice always to have some CPUs (more than one) that do not use dynamic or adaptive ticks, nor any of the other changes below. If you use adaptive ticks, you still need to select a tick rate for the excluded CPUs, e.g. 250Hz.

Finally, a note on dynamic ticks. Disabling the tick as above is possible only for idle CPUs with `CONFIG_NO_HZ_IDLE`. This is the default build parameter for desktop systems or any other kernel unless specified otherwise. Although it seems a good compromise between the two above, it has some implications. The specified CPUs, for example, can not handle RCU callbacks. If enabled, you must thus off-load RCU handling to threads (see below). POSIX timers may further prevent these idle CPUs from ever entering the dyntick mode, requiring changes in your RT applications. If you would like to use dyntick, use the `nohz=on`boot parameter and the flag to enable it. The setting can be found in `General setup->Timer subsystem`. If you use dynamic ticks, you still need to select a tick rate for the excluded CPUs, e.g., 250Hz.

[^nohz]: Further reading [Kernel Wiki](https://www.kernel.org/doc/html/latest/timers/no_hz.html)

[^3]: It is worth noting that on such a system, almost all tasks run as FIFO or RR-scheduled real-time tasks, including the repetitive, cyclically scheduled PLC tasks and drivers. Instead of running an EDF schedule with a regular period, the tasks are implemented for the remainder of the period, once running is done, as sleep and yield to the scheduler.

[^4]: CoDeSys tasks are scheduled either in `SCHED_OTHER` or `SCHED_FIFO`, depending on the IEC Priority setting, [see CoDeSys Linux Optimization](https://content.helpme-codesys.com/en/CODESYS%20Control/_rtsl_performance_optimization_linux.html)

### RCU call-backs

[Read-copy-update](https://www.kernel.org/doc/html/latest/RCU/whatisRCU.html) call-backs are performed by the kernel as soft-irqs to keep memory between concurring threads up to date. To reduce interference with real-time tasks, they can be delayed in execution or even limited to running on certain CPUs through offloading into separate threads. The latter is strictly necessary for adaptive or dynamic ticks (`CONFIG_NO_HZ*`) to exclude callbacks on the schedule-tick-reduced CPUs. 

To activate the off-loading of these call-backs into kernel threads, go to `General setup->RCU-Subsystem` and select expert mode if necessary `RCU_EXPERT`, then `Offload RCU callback processing..` which sets the parameter `CONFIG_RCU_NOCB_CPU` to `y`. You will need to specify the boot parameter `rcu_nocbs=<list-cpus>` to tell the kernel which CPUs to offload the call-backs into threads. On newer kernels, there (may) exist the `CONFIG_RCU_NOCB_CPU_ALL` flag, introduced by Ubuntu Pro Real-time kernel.

Once the parameter is set, you may proceed further by changing these generated threads' affinity and thus pinning them only to a certain CPU range (see Kernel thread affinity below).

### Disable kernel debug features

Ubuntu distributions enable more than 50 Debug flags by default. Many, if not all, may be disabled for most Real-Time use cases. Thus, we might as well disable them. This might, however, be overkill. Recent kernels introduce a [dynamic debug](https://www.kernel.org/doc/html/latest/admin-guide/dynamic-debug-howto.html) feature, which allows to enable and disable debug code dynamically with only little overhead. 

If you are building an older kernel, or you still want to disable debugging, you can set all debug features to false with this command

```
sed -i "s/\(DEBUG.*=\)y\(.*\)/\1n\2/g" .config
```

Please note that a few features can not be disabled, e.g., `DEBUG_FS`, which is required by the kernel tracer `ftrace` (* Please note, we need kernel tracer functions, part of kernel debug_fs, for the orchestration tool in this repository to work *). Rerun `make menuconfig` to restore those flags through its dependencies. You should see something like this with `grep "DEBUG.*=y" .conf`

```
CONFIG_ARCH_SUPPORTS_DEBUG_PAGEALLOC=y
CONFIG_X86_DEBUGCTLMSR=y
CONFIG_CB710_DEBUG_ASSUMPTIONS=y
CONFIG_DEBUG_FS=y
CONFIG_DEBUG_KERNEL=y
CONFIG_HAVE_DEBUG_KMEMLEAK=y
CONFIG_ARCH_HAS_DEBUG_VIRTUAL=y
CONFIG_LOCK_DEBUGGING_SUPPORT=y
```

Here is more info on [dynamic debuging](https://www.kernel.org/doc/html/latest/admin-guide/dynamic-debug-howto.html)

## Kernel boot parameters

Kernel boot parameters are parameters that are passed at system boot to the process reading the kernel image. Such parameters are typically specified in the `grub` boot configuration and can be changed as follows[^2].

The default `grub` configuration is located at `/etc/default/grub`. Open the file with your favorite editor, e.g. `nano` and change the parameters required by adding or removing from `GRUB_CMDLINE_LINUX` and/or `GRUB_CMDLINE_LINUX_DEFAULT`, in quotes, and separated by spaces. The former is always effective, and the latter is added to the former in the case of a normal-automatic- boot. For Ubuntu systems, the only two parameters set for `*DEFAULT` are `quiet` and `splash`.

Run `update-grub` to apply the changes and rebuild the `grub` boot menu. You can check the resulting generated boot parameters with `cat /boot/grub/grub.cfg | grep /boot`.

Please note that the contents of `grub.d` may be added and even overwritten your changes. If you wish, you can also add a file with only the `GRUB_CMDLINE_LINUX*` parameter lines in this directory, and the changes are kept between kernel upgrades.

The description of parameters follows below.

[^2]: for Ubuntu, other systems please research online

### Disable SMT

Simultaneous multi-threading (SMT) is a very useful feature for day-to-day operations. It increases CPU speed by up to 30% due to reduced flushing and filling of CPU registers. However, this holds only true as long as we don't need a guarantee on reaction and processing times. 

In Linux, threads served by the same CPU core are enumerated as separate CPUs. Thus, a CPU with a computing power of 1 would be addressed as if it had a computing power of 2 or more. In reality, though, not cores but only CPU registers are typically doubled, creating unforeseen delay and jitter while competing for the processing unit.

We can disable SMT at runtime by the following.

```
echo 0 > /sys/devices/system/cpu/smt/control
```

The required kernel boot parameter to make this change permanent is `nosmt`. For notes on selective disabling, see System runtime settings, Disable SMT.

### Scheduler (and) isolation

On older kernels, a boot parameter called `isolcpus` allowed to isolate CPU ranges from the system scheduler, IRQs, and other kernel-related tasks and threads. Newer kernels still export the feature, but it is now widely seen as deprecated. Since the introduction of CGroup v2, administrators are advised to use `isolated` control groups instead (see also "Setting restrictions with CGroup" below).

Nonetheless, for the sake of completeness, here are the suggested parameters for this boot entry on older kernels.

`isolcpus` allows for multiple values. Ideally, we specify here 3 parameters: a CPU range `<list-cpus>`, which is the range dedicated to our real-time tasks to be isolated; `domain` to isolate from balancing and scheduling algorithms; and `managed_irq` to isolate the range from being the target of managed IRQs. The resulting boot parameter is thus `isolcpus=<list-cpus>,domain,managed_irq`.

Please note that these settings are on a best-effort basis and do not guarantee "perfect" isolation. Use CGroups v2 instead for better isolation. For help on CPU listing, see [Kernel parameter Wiki](https://docs.kernel.org/admin-guide/kernel-parameters.html).

### RCU call-backs

As discussed in the kernel build parameter section, once we configured to off-load kernel call-backs for RCU operations into threads, we also have to decide for which CPUs we want that to happen. This is done at boot time with the `rcu_nocbs=<list-cpus>` parameter. If the '=' sign and the cpulist arguments are omitted, no CPU will be set to no-callback mode from boot but the mode may be toggled at runtime via cpusets.

The kernel foresees another parameter regarding RCU call-backs, `rcu_nocb_poll`. If set, Rather than requiring that off-loaded CPUs awaken the corresponding RCU-kthreads, it makes kthreads poll for callbacks. This improves the real-time response for the off-loaded CPUs by relieving them of the need to wake up the corresponding kthread. However, the kthreads periodically wake up to do the polling and must thus be pinned ideally to other CPUs (see "Other" and "Runtime settings" below).

### IRQs and affinity

The boot parameter `irqaffinity=<list-cpus>` determines which CPU should, by default, be in charge of handling interrupts. The listed CPUs should thus NOT include any CPU required for real-time applications. Furthermore, the setting is best-effort and is thus not guaranteed. As a warning: try not to limit IRQ computations on too few CPUs. The amount of work, in addition to all other system tasks, may render the system unresponsive[^mig]. [Source](https://wiki.linuxfoundation.org/realtime/documentation/howto/applications/cpuidle)

### other

- `skew_tick=`, if set to 1, offsets the periodic timer tick per CPU to mitigate RCU lock contention on all systems with CONFIG_MAXSMP set (usually true)

- `intel_pstate=disable` disables the Intel P-state driver. However, this does not prevent the eventual BIOS of Hardware from enforcing P-states otherwise. Unless proven otherwise, it is thus advised to use the Intel governor with profile `performance` instead, running the CPU always at max power.

- `nosoftlockup` and `tsc=nowatchdog` disable some kernel internal watchdogs, removing thus a further source of jitter. Of course, we are also loosing the watchdog's protection this way.

- `timer_migration=0` disables the migration of timer between CPU sockets (if multiple are present) and mitigates resulting jitter.

- Some kernels expose the flag `kthread_cpus=<list-cpus>`, which allows the setting of the CPUs dedicated for kernel threads, and preempts the requirement to manually pin them to specific resources. If present, you can use this parameter instead. (Unused kernel parameters trigger a warning)

## System runtime settings

In the remainder of this section, we describe features and steps that may be performed at runtime to optimize isolation and response and reduce interference. Unlike above, these may be undone at runtime, too, and are thus a good place to start.

### Disable SMT

We can disable SMT at runtime by the following (See kernel boot parameters, "Disable SMT")

```
echo 0 > /sys/devices/system/cpu/smt/control
```

This said we must also note another point. If we aim for a jitter-free real-time partition and thus want to disable the SMT feature on the cores, it does not mean that we have to disable it too on the cores we keep for best-effort and system tasks. 

The above setting indeed disables hyper-threading on all cores. To selectively disable hyper-threading, we could instead take note of the point discussed above: every thread on Linux is seen as a separate CPU. Thus, instead of disabling SMT overall, we just use "hot-plug" to disable the CPUs, or threads, that are siblings of a running real-time core.

Performing this, however, is a little more difficult as the layout and numbering of CPUs depend on the system architecture. What we can do is explore the system's VFS and detect the siblings of the CPUs we deem for real-time use only. The following prints the CPU mask for the siblings of each thread. You can use this information to e.g. disable only one of the threads for each real-time core.

```
prcs=$(nproc --all) #get number of cpu-threads
for ((i=0;i<$prcs;i++)); do 
	cd=$(cat cpu$i/topology/thread_siblings); printf %X $(( 0x$cd & ~( 1<<($i-1) ) ))
done
```

(NOTE: needs more detailed example)

### Restricting power-saving modes

CPUs, in addition to SMT capabilities, are able to change power and sleep states, `P-*` and `C-*` states. This means the operating system can change the speed and performance of CPUs in order to reduce power consumption. Although generally acceptable, this is a cause for jitter and unpredictability we would like to remove.

How these states are controlled depends on a component called `governor` while the actual controlling depends on a `driver`. For each CPU, the Linux VFS allows inspection and configuration of such in `/sys/devices/system/cpu`. Let's see how to detect and change governors.

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

As described in the boot parameter section, we could disable the p-state driver through `intel_pstate=disable`. This, however, does not guarantee that BIOS or hardware enforcement of p-states does not take place, but only tells the Linux kernel not to try to manage p-states exclusively. We advise thus to stick with the performance governor settings. [Source](https://www.kernel.org/doc/Documentation/cpu-freq/governors.txt)

Once done, let's check a newly introduced parameter, `power/pm_qos_resume_latency_us`. PM QoS is an infrastructure in the kernel that can be used to fine-tune the CPU idle system governance. It can be used to limit C—or sleep—states in all CPUs system-wide or per core. The following sections explain the user-level interface. Now, true, we never should require a resume as we are in a constant "wake" state, but just to be sure, let's verify.

```
cat /sys/devices/system/cpu/cpu0/power/pm_qos_resume_latency_us
```

The value displayed should be `n/a` in order to disable all C-states. We can change the value the same way as above. [Source](https://wiki.linuxfoundation.org/realtime/documentation/howto/applications/cpuidle)

### RR slicing and RT-throttling, or not?

Depending on the system configuration, it might be possible to disable real-time task throttling. The basic requirement here is that we are not applying a G-EDF schedule, but rather a P-EDF variant. The difference between the two is simply the use of CGroups to limit the amount of resources real-time tasks can use. 

In the normal case, the Linux kernel limits the RT CPU usage to (default) 95% of CPU time, or more precisely, 950000 µs (`sched_rt_runtime_us`) every 1000000 µs (`sched_rt_period_us`). This would mean that even if we have dedicated CPUs to execute the code and we do not incur overload and consequent hang, the kernel throttles down all active real-time tasks in order to execute pending lesser-priority tasks. If thus we have a partitioned system (P-EDF), we can deactivate (`-1`) this limit once we make sure all of the latter tasks are run on dedicated resources and are not compromised.

```
echo -1 > /proc/sys/kernel/sched_rt_runtime_us
```

Similarly, depending on your task configuration, it might be possible to run a large number of round-robin (`RR`) tasks. These tasks, by default, are limited to a rather generous 100ms time slice, i.e., they can lock CPU resources for that amount of time and are only preempted by higher priorities or EDF tasks.

If thus you execute more time-sensitive RR tasks and you want to avoid software mishaps breaking your system, you can reduce this maximum slice limit.

```
echo 50 > /proc/sys/kernel/sched_rr_timeslice_ms
```

### IRQs and affinity

Most modern OSs include a service that balances the IRQs, e.g., every 10 seconds and may overwrite any manual setting you perform. If that is the case, check with `service irqbalance status`. You may add the real-time critical CPUs to the service configuration as banned or disable the service completely.

To add the CPUs, edit the configuration file `/etc/default/irqbalance` in Ubuntu, remove the `#` in front of `IRQBALANCE_BANNED_CPULIST=`, and insert the CPUs. To disable the service run `systemctl disable irqbalance.service ` (works on systemd systems)

If you don't have a balancing daemon, you can proceed by editing the affinity yourself.`/proc/irq/default_smp_affinity` specifies a default affinity mask that applies to all non-active IRQs. It is also set through the `irqaffinity=` boot parameter. Once IRQ is allocated/activated its affinity bitmask will be set to the default mask. It can then be changed as described below. The default mask is 0xffffffff. [](https://docs.kernel.org/core-api/irq/irq-affinity.html)

```
echo "0x00ff" > /proc/irq/default_smp_affinity
```

Single, active IRQ affinity can be changed instead by setting the `smp_affinity` variable inside the VFS folder named after the IRQ number in `/proc/irq/`. As detailed in the boot parameter section, setting IRQ affinity should be done after considering eventual side effects and only by allocating enough resources to deal with the IRQs[^mig].

`ksoftirqd/N` are softirq handlers and must be forced to the target CPUs through a hotplug. In most cases, they relate to timers and can just be forced away by unplugging their CPU. To do this, disable and re-enable all CPUs but 0 in sequence. Here is an example of the mentioned task.

```
prcs=$(nproc --all) #get number of cpu-threads
#shut down cores
for ((i=$prcs-1;i>0;i--)); do 
	$(echo 0 > /sys/devices/system/cpu/cpu$i/online)
	sleep 1
done
sleep 1

# put them back online
for ((i=1;i<$prcs;i++)); do
	$(echo 1 > /sys/devices/system/cpu/cpu$i/online)
	sleep 1
done
```
Once the hotplug is done, do not put CPUs offline and online again. Please note that you may want to only selectively reactivate CPUs (threads) to only keep one thread of each multi-threading core active for the real-time CPUs. See "Disable SMT".

[Kernel admin Wiki](https://docs.kernel.org/admin-guide/kernel-per-CPU-kthreads.html)

### Changing kernel thread affinity

To change a kernel thread affinity mask and make it run only on CPUs to our liking, we can execute code with the pattern below. The first line identifies the list of PIDs corresponding to the mentioned thread, the second assigns them a new mask, one by one. Replace `<MASK HERE>` with the basic regex specified in the descriptions below. The CPU-mask in the example is 0xff, replace where needed and execute them together.

```
ps h -eo spid,command | grep -v 'grep' | grep -G "<MASK HERE>" | awk '{ print $1 }' | \
while read -t 1 pid ; do taskset -p 0xff $pid ; done
```

- `ehca_comp/*` are eHCA Infiniband hardware threads. Unless you use such hardware, you will not find any of these threads.

	`\B\[ehca_comp[/][[:digit:]]*`

- `irq/*-*"` are IRQ kernel threads whose affinity is changed by using the settings in the previous section for new entries, or the command below for running threads.

	`\B\[irq[/][[:digit:]]*-[[:alnum:]]*`

- `rcuop/*`and `rcuos/*` are present in kernels built with CONFIG_RCU_NOCB_CPU=y for no-callback CPU. Each "rcuox/N" kthread is created for that purpose, where "x" is "p" for RCU-preempt, "s" for RCU-sched, and "g" for the kthreads that mediate grace periods; and "N" is the CPU number. We can pin these to specific CPUs with the automated code below.

	`\B\[rcuop[/][[:digit:]]*`, `\B\[rcuos[/][[:digit:]]*` and `\B\[rcuog[/][[:digit:]]*` 

- `kworker/*:**` are worker threads for the kernel. They are automatically preempted if your tasks run with a real-time priority. The grep mask for this thread is

	`\B\[kworker[/][[:digit:]]*` 

Further information at [Kernel admin Wiki](https://docs.kernel.org/admin-guide/kernel-per-CPU-kthreads.html).

## Other provisions

We can stop timer migration between sockets at runtime with

```
echo 0 > /proc/sys/kernel/timer_migration
```

### Create separate CGroup trees

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

### Moving system tasks Cgroup

A futher step to better control latency and jitter is to isolate tasks performing system duties from tasks running inside our containers. Typically, as the docker case, this is done with control groups. Once the group is established, it is possible to reduce the resources assigned to system tasks and exclusively assign some to the running of containers.

As said in the previous section, recent system configurations moved to Cgroup v2. This new setup automatically creates a `system.slice` and `user.slice` containing all system and user tasks, respectively. However, for those still using the older VFS, here are some steps you can perform to move all non container-related tasks in a separate control group.

Inside the `/sys/fs/cgroup/` directory, enter one of the controller trees, e.g. `cpuset`, and do the following (with privileges):
```
mkdir system
cat tasks > system/tasks
```
Tasks contains a list of PIDs that are running and assigned to the present control group, by default the `root` control group. by creating a directory, we basically create a subgroup. Echoing the PID numbers into its tasks file will thus move the assignment from `cpuset`-cgroup `root` to `system`. 

N.B. Docker will automatically create a CGroup v1 subgroup `docker`, thus allowing us, by default, to control resources assigned to  daemon and containers.

### Setting restrictions with CGroup

There esist tools such as `taskset` and `cset` to set affinity and perform cpu-pinning. Nonetheless, I believe the easiest and most structured way of managing resources is through CGroup trees. Depending on the version, the files and usage changes a little, but in short it's all about  echoing to virtual files.

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

Another mode that can be selected in CGroup v2 is `isolated`. In addition to the function of `root`, the isolated control group is also shielded from the scheduler and other kernel interferece, replacing the now deprecated `isolcpus` kernel boot flag. The usage is identical to `root`.

### Switching to CGroup v2 

On `systemd`-based systems, if they still stick to CGroup v1, it is possible to manually switch to CGroup v2 using the following Kernel boot parameter `systemd.unified_cgroup_hierarchy=1`. Use the procedure described above to perform the change.

Further information can be found in the Kernel Wiki for [CGroups v1](https://docs.kernel.org/admin-guide/cgroup-v1/index.html) and [CGroups v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)

[^mig]: Hofer et. al. [Industrial Control via Application Containers: Migrating from Bare-Metal to IAAS](https://www.semanticscholar.org/paper/Industrial-Control-via-Application-Containers%3A-from-Hofer-Sehr/dff2213ab3dea999a60580271b8086960264260f)
