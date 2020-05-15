/*
 * prepare.c
 *
 *  Created on: Feb 10, 2020
 *      Author: Florian Hofer
 */

#include "orchestrator.h"

// Default stuff, needed form main operation
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants

#include <sched.h>			// scheduler functions
#include <linux/sched.h>	// Linux specific scheduling
#include <linux/types.h>	// data structure types, short names and linked list
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <fcntl.h>			// file control, new open/close functions
#include <dirent.h>			// directory entry structure and functions
#include <errno.h>			// error numbers and strings

// Custom includes
#include "kernutil.h"	// generic kernel utilities
#include "error.h"		// error and std error print functions
#include "cmnutil.h"	// common definitions and functions

// Things that should be needed only here
#include <pthread.h>// used for thread management

#include <sys/mman.h>		// memory lock
#include <numa.h>			// Numa node identification
#include <getopt.h>			// command line parsing
#include <sys/stat.h>		// directory and file system statistics
#include <sys/capability.h>	// cap exploration
#include <sys/sysinfo.h>	// system general information


// for musl based systems
#ifndef ACCESSPERMS
	#define ACCESSPERMS (S_IRWXU|S_IRWXG|S_IRWXO) /* 0777 */
#endif
#ifndef ALLPERMS
	#define ALLPERMS (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO)/* 07777 */
#endif
#ifndef DEFFILEMODE
	#define DEFFILEMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)/* 0666*/
#endif

// -------------- LOCAL variables for all the functions  ------------------

static unsigned long * smi_counter = NULL; // points to the list of SMI-counters
static int * smi_msr_fd = NULL; // points to file descriptors for MSR readout

// static capability mask
#define CAPMASK_ALL		0x7
#define CAPMASK_NICE	0x1
#define CAPMASK_RES		0x2
#define CAPMASK_IPC		0x4
static int capMask = CAPMASK_ALL;

/// setPidMask(): utility function to set all pids of a certain mask's affinity
/// Arguments: - tag to search for
///			   - bit mask to set
///
/// Return value: --
///
static void setPidMask (char * tag, struct bitmask * amask, char * cpus)
{
	FILE *fp;

	{
		if (!tag)
			err_exit("Process signature tag is a null pointer!");

		int tlen = strlen (tag) + 35;
		char req[tlen];

		// prepare literal and open pipe request, request spid (thread) ids
		// spid and pid coincide for main process
		(void)sprintf (req,  "ps h -eo spid,command | grep -G '%s'", tag);

		if(!(fp = popen(req,"r")))
			return;
	}

	char pidline[BUFRD];
	char *pid, *pid_ptr;
	int mpid;
	// Scan through string and put in array
	while(fgets(pidline,BUFRD,fp)) {
		pid = strtok_r (pidline," ", &pid_ptr);
		mpid = atoi(pid);
        pid = strtok_r (NULL, "\n", &pid_ptr); // end of line?

		if (numa_sched_setaffinity(mpid, amask))
			warn("could not set pid %d-'%s' affinity: %s", mpid, pid, strerror(errno));
		else
			cont("PID %d-'%s' reassigned to CPU's %s", mpid, pid, cpus);
    }

	pclose(fp);
}

/// getCapMask(): utility function get our capability mask
///
/// Arguments: - program settings structure
///
/// Return value: --
///
static int getCapMask(prgset_t *set) {
	/// verify executable permissions - needed for dry run, not blind run

	int capMask = 0;

	cont( "Verifying for process capabilities..");
	cap_t cap = cap_get_proc(); // get capability map of process
	if (!cap)
		err_exit_n(errno, "Can not get capability map");

	cap_flag_value_t v = 0; // flag to store return value
	if (cap_get_flag(cap, CAP_SYS_NICE, CAP_EFFECTIVE, &v)) // check for effective NICE cap
		err_exit_n(errno, "Capability test failed");

	if (!CAP_IS_SUPPORTED(CAP_SYS_NICE) || (0==v)){
		if (!(set->blindrun))
			err_exit("CAP_SYS_NICE capability mandatory to operate properly!");
		else
			warn("CAP_SYS_NICE unavailable");
		capMask &= CAPMASK_NICE;
	}

	v=0;
	if (cap_get_flag(cap, CAP_SYS_RESOURCE, CAP_EFFECTIVE, &v)) // check for effective RESOURCE cap
		err_exit_n(errno, "Capability test failed");

	if (!CAP_IS_SUPPORTED(CAP_SYS_RESOURCE) || (0==v)){
		if (!(set->blindrun))
			err_exit("CAP_SYS_RESOURCE capability mandatory to operate properly!");
		else
			warn("CAP_SYS_RESOURCE unavailable");
		capMask &= CAPMASK_RES;
	}

	v=0;
	if (cap_get_flag(cap, CAP_IPC_LOCK, CAP_EFFECTIVE, &v)) // check for effective RESOURCE cap
		err_exit_n(errno, "Capability test failed");

	if (!CAP_IS_SUPPORTED(CAP_IPC_LOCK) || (0==v)){
		warn("CAP_IPC_LOCK unavailable.");
		capMask &= CAPMASK_IPC;
	}

	return capMask;
}

/// prepareEnvironment(): prepares the runtime environment for real-time
/// operation. Creates CPU shield and configures the affinity of system
/// processes and interrupts to reduce off-load on RT resources
///
/// Arguments: - structure with parameter set
///
/// Return value: Error code
/// 				Only valid if the function returns
///
int prepareEnvironment(prgset_t *set) {

	// TODO: CPU number vs CPU enabling mask
	// TODO: check maxcpu vs last cpu!
	// Important when many are disabled from the beginning
	// TODO: numa_allocate_cpumask vs malloc!! -> Allocates the size!!

	/// --------------------
	/// verify CPU topology and distribution
	// TODO: global update maxCPU -> max CPU number of last installed CPU, could be higher number than expected
	int maxcpu = get_nprocs();
	int maxccpu = get_nprocs_conf(); // numa_num_configured_cpus();

	char cpus[CPUSTRLEN]; // cpu allocation string
	char constr[CPUSTRLEN]; // cpu online string
	char str[100]; // generic string...

	info("This system has %d processors configured and "
        "%d processors available.",
        maxccpu, maxcpu);

	if (numa_available())
		err_exit( "NUMA is not available but mandatory for the orchestration");

	info("Starting environment setup");

	// verify if SMT is disabled -> now force = disable, TODO: may change to disable only concerned cores
	if (!(set->blindrun) && (0 < getkernvar(set->cpusystemfileprefix, "smt/control", str, sizeof(str)))){
		// value read OK
		if (!strcmp(str, "on")) {
			// SMT - HT is on
			if (set->dryrun)
				cont("Skipping setting SMT.");
			else
				if (!set->force)
					err_exit("SMT is enabled. Set -f (force) flag to authorize disabling");
			else
				if (0 > setkernvar(set->cpusystemfileprefix, "smt/control", "off", set->dryrun))
					err_exit_n(errno, "SMT is enabled. Disabling was unsuccessful!");
			else
				cont("SMT is now disabled, as required. Refresh configurations..");
			sleep(1); // leave time to refresh conf buffers -> immediate query fails
			maxcpu = get_nprocs();	// update
		}
		else
			cont("SMT is disabled, as required");
	}
	else // SMT failed or DryRun
		warn("Skipping read of SMT status. This can influence latency performance!");

	// prepare bit-mask, no need to do it before
	set->affinity_mask = parse_cpumask(set->affinity);
	if (!set->affinity_mask)
		return -1; // return to display help

	smi_counter = calloc (maxccpu, sizeof(long));
	smi_msr_fd = calloc (maxccpu, sizeof(int));
	if (!smi_counter || !smi_msr_fd)
		err_exit("could not allocate memory!");

	struct bitmask * naffinity = numa_allocate_cpumask();
	if (!naffinity)
		err_exit("could not allocate memory!");

	// get online cpu's
	if (0 < getkernvar(set->cpusystemfileprefix, "online", constr, sizeof(constr))) {
		struct bitmask * con = numa_parse_cpustring_all(constr);
		// mask affinity and invert for system map / readout of smi of online CPUs
		for (int i=0;i<maxccpu;i++) {

			/* ---------------------------------------------------------*/
			/* Configure online CPUs - HT, SMT and performance settings */
			/* ---------------------------------------------------------*/
			if (numa_bitmask_isbitset(con, i)){ // filter by online/existing

				char fstring[50]; 	// cpu string

				{ // start block governor
					char poss[50]; 		// possible settings string for governors

					// verify if CPU-freq is on performance -> set it
					(void)sprintf(fstring, "cpu%d/cpufreq/scaling_available_governors", i);
					if (0 < getkernvar(set->cpusystemfileprefix, fstring, poss, sizeof(poss))){
						// value possible read ok
						(void)sprintf(fstring, "cpu%d/cpufreq/scaling_governor", i);
						if (0 < getkernvar(set->cpusystemfileprefix, fstring, str, sizeof(str))){
							// value act read ok
							if (strcmp(str, CPUGOVR)) {
								// Governor is set to a different value
								cont("Possible CPU-freq scaling governors \"%s\" on CPU%d.", poss, i);

								if (set->dryrun || set->blindrun)
									cont("Skipping setting of governor on CPU%d.", i);
								else
									if (!set->force)
										err_exit("CPU-freq is set to \"%s\" on CPU%d. Set -f (force) flag to authorize change to \"" CPUGOVR "\"", str, i);
									else
										if (0 > setkernvar(set->cpusystemfileprefix, fstring, CPUGOVR, set->dryrun))
											err_exit_n(errno, "CPU-freq change unsuccessful!");
										else
											cont("CPU-freq on CPU%d is now set to \"" CPUGOVR "\" as required", i);
							}
							else
								cont("CPU-freq on CPU%d is set to \"" CPUGOVR "\" as required", i);
						}
						else
							warn("CPU%d Scaling governor settings not found. Skipping.", i);
					}
					else
						warn("CPU%d available CPU scaling governors not found. Skipping.", i);

				} // end block

				// CPU-IDLE settings, added with Kernel 4_16
				(void)sprintf(fstring, "cpu%d/power/pm_qos_resume_latency_us", i);
				if (0 < getkernvar(set->cpusystemfileprefix, fstring, str, sizeof(str))){
					// value act read ok
					if (strcmp(str, "n/a")) {
						//
						cont("Setting for power-QoS now \"%s\" on CPU%d.", str, i);

						if (set->dryrun || set->blindrun)
							cont("Skipping setting of power QoS policy on CPU%d.", i);
						else
							if (!set->force)
								err_exit("Set -f (force) flag to authorize change to \"" "n/a" "\"", str, i);
							else
								if (0 > setkernvar(set->cpusystemfileprefix, fstring, "n/a", set->dryrun))
									err_exit_n(errno, "CPU-QoS change unsuccessful!");
								else
									cont("CPU power QoS on CPU%d is now set to \"" "n/a" "\" as required", i);
					}
					else
						cont("CPU-freq on CPU%d is set to \"" "n/a" "\" as required", i);
				}
				else
					warn("CPU%d power saving configuration not found. Skipping.", i);

				// if smi is set, read SMI counter
				if(set->smi) {
					*(smi_msr_fd+i) = open_msr_file(i);
					if (*(smi_msr_fd+i) < 0)
						err_exit("Could not open MSR interface, errno: %d",
							errno);
					// get current smi count to use as base value
					if (get_smi_counter(*smi_msr_fd+i, smi_counter+i))
						err_exit("Could not read SMI counter, errno: %d",
							0, errno);
				}

				// invert affinity for available CPUs only -> for system
				if (!numa_bitmask_isbitset(set->affinity_mask, i))
					numa_bitmask_setbit(naffinity, i);
			}

			// if CPU not online
			else if (numa_bitmask_isbitset(set->affinity_mask, i)){
				// disabled processor set to affinity
				info("Processor %d is set for affinity mask, but is disabled.", i);
				err_exit("Unavailable CPU set for affinity.");
			}
		}
		numa_free_cpumask(con);
	}
	else // online CPU string not readable
		err_exit("Can not read online CPUs");


	// parse to string
	if (parse_bitmask (naffinity, cpus, CPUSTRLEN))
		err_exit ("can not determine inverse affinity mask!");

	// verify our capability mask
	capMask = getCapMask(set);

	/// --------------------
	/// Kernel variables, disable bandwidth management and RT-throttle
	/// Kernel RT-bandwidth management must be disabled to allow deadline+affinity
	set->kernelversion = check_kernel();

	if (KV_NOT_SUPPORTED == set->kernelversion)
		warn("Running on unknown kernel version...YMMVTrying generic configuration..");

	cont( "Set real-time bandwidth limit to (unconstrained)..");
	// disable bandwidth control and real-time throttle
	if (0 > setkernvar(set->procfileprefix, "sched_rt_runtime_us", "-1", set->dryrun)){
		warn("RT-throttle still enabled. Limitations apply.");
	}

	if (SCHED_RR == set->policy && 0 < set->rrtime) { // TODO: maybe change to global as now used for adapt
		cont( "Set round robin interval to %dms..", set->rrtime);
		(void)sprintf(str, "%d", set->rrtime);
		if (0 > setkernvar(set->procfileprefix, "sched_rr_timeslice_ms", str, set->dryrun)){
			warn("RR time slice not changed!");
		}
	}
	else{
		if (0 > getkernvar(set->procfileprefix, "sched_rr_timeslice_ms", str, sizeof(str))){
			warn("Could not read RR time slice! Setting to default 100ms");
			set->rrtime=100; // default to 100ms
		}
		else{
			set->rrtime = atoi(str);
			info("RR slice is set to %d ms", set->rrtime);
		}
	}

	// here.. off-line messes up CSET
	cont("moving kernel thread affinity");
	// kernel interrupt threads affinity
	setPidMask("\\B\\[ehca_comp[/][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[irq[/][[:digit:]]*-[[:alnum:]]*", naffinity, cpus);
	setPidMask("\\B\\[kcmtpd_ctr[_][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[rcuop[/][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[rcuos[/][[:digit:]]*", naffinity, cpus);

	// ksoftirqd -> offline, online again
	// TODO: dryrun?? print and show ?
	cont("Trying to push CPU's interrupts");
	if (!set->blindrun && !set->dryrun)
	{
		char fstring[50]; // cpu string
		// bring all affinity except 0 off-line
		for (int i=maxccpu-1;i>0;i--) {

			if (numa_bitmask_isbitset(set->affinity_mask, i)){ // filter by online/existing

				// verify if cpu-freq is on performance -> set it
				(void)sprintf(fstring, "cpu%d/online", i);
				if (0 > setkernvar(set->cpusystemfileprefix, fstring, "0", set->dryrun))
					err_exit_n(errno, "CPU%d-Hotplug unsuccessful!", i);
				else
					cont("CPU%d off-line", i);
			}
		}
		// bring all back online
		for (int i=1;i<maxccpu;i++) {

			if (numa_bitmask_isbitset(set->affinity_mask, i)){ // filter by online/existing

				// verify if CPU-frequency is on performance -> set it
				(void)sprintf(fstring, "cpu%d/online", i);
				if (0 > setkernvar(set->cpusystemfileprefix, fstring, "1", set->dryrun))
					err_exit_n(errno, "CPU%d-Hotplug unsuccessful!", i);
				else
					cont("CPU%d online", i);
			}
		}
	}
	else
		if (set->dryrun)
			cont("skipped.");

	// lockup detector
	// echo 0 >  /proc/sys/kernel/watchdog
	// or echo 9999 >  /proc/sys/kernel/watchdog

	/// --------------------
	/// running settings for scheduler
	// TODO: detect possible CPU alignment cat /proc/$$/cpuset, maybe change it once all is done
	struct sched_attr attr;
	pid_t mpid = getpid();
	if (sched_getattr (mpid, &attr, sizeof(attr), 0U))
		warn("could not read orchestrator attributes: %s", strerror(errno));
	else {
		cont( "orchestrator scheduled as '%s'", policy_to_string(attr.sched_policy));

		// set new attributes here, avoid real-time for this thread
		attr.sched_nice = 20;
		// reset on fork.. and if DL is set, grub and overrun
		attr.sched_flags |= SCHED_FLAG_RESET_ON_FORK;

		cont( "promoting process and setting attributes..");
		if (sched_setattr (mpid, &attr, 0U))
			warn("could not set orchestrator scheduling attributes, %s", strerror(errno));
	}

	if (AFFINITY_USEALL != set->setaffinity){ // set affinity only if not useall
		if (numa_sched_setaffinity(mpid, naffinity))
			warn("could not set orchestrator affinity: %s", strerror(errno));
		else
			cont("Orchestrator's PID reassigned to CPU's %s", cpus);
	}

	/// --------------------
	/// Docker CGROUP setup - detection if present

	{ // start environment detection CGroup
	struct stat s;

	int err = stat(set->cpusetdfileprefix, &s);
	if(-1 == err) {
		// Docker CGroup not found, set->force enabled = try creating
		if(ENOENT == errno && set->force) {
			warn("CGroup '%s' does not exist. Is the daemon running?", set->cont_cgrp);
			if (0 != mkdir(set->cpusetdfileprefix, ACCESSPERMS))
				err_exit_n(errno, "Can not create container group");
			// if it worked, stay in Container mode
		} else {
		// Docker CGroup not found, set->force not enabled = try switching to pid
			if(ENOENT == errno)
				err_msg("No set->force. Can not create container group: %s", strerror(errno));
			else
				err_exit_n(errno, "stat encountered an error");
		}
	} else {
		// CGroup found, but is it a dir?
		if(!(S_ISDIR(s.st_mode)))
			// exists but is no dir -> goto PID detection
			err_exit("CGroup '%s' does not exist. Is the daemon running?", set->cont_cgrp);
	}

	// Display message according to detection mode set
	switch (set->use_cgroup) {
		default:
			// all fine?
			if (!err) {
				cont( "container id will be used to set PID execution..");
				break;
			}
			// error occurred, check for sCHED_DEADLINE first-> stop!
			if (SCHED_DEADLINE == set->policy)
				err_exit("SCHED_DEADLINE does not allow forking. Can not switch to PID modes!");

			// otherwise switch to container PID detection
			set->use_cgroup = DM_CNTPID;
			//no break

		case DM_CNTPID:
			cont( "will use PIDs of '%s' to detect processes..", set->cont_ppidc);
			break;
		case DM_CMDLINE:
			cont( "will use PIDs of command signtaure '%s' to detect processes..", set->cont_pidc);
			break;
	}
	} // end environment detection CGroup

	/// --------------------
	/// detect NUMA configuration TODO: adapt for full support
	char * numastr = malloc (5);
	if (!(numastr))
			err_exit("could not allocate memory!");
	if (-1 != numa_available()) {
		int numanodes = numa_max_node();

		sprintf(numastr, "0-%d", numanodes);
	}
	else{
		warn("NUMA not enabled, defaulting to memory node '0'");
		// default NUMA string
		sprintf(numastr, "0");
	}

	/// --------------------
	/// CGroup present, fix CPU-sets of running containers
	if (AFFINITY_USEALL != set->setaffinity){ // TODO: useall = ignore setting of exclusive

		cont( "reassigning Docker's CGroups CPU's to %s exclusively", set->affinity);

		DIR *d;
		struct dirent *dir;
		d = opendir(set->cpusetdfileprefix);// -> pointing to global
		if (d) {

			// CLEAR exclusive flags in all existing containers
			{
				char *contp = NULL; // clear pointer
				while ((dir = readdir(d)) != NULL) {
				// scan trough docker CGroup, find container IDs
					if (64 == (strspn(dir->d_name, "abcdef1234567890"))) {
						if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
							+ strlen(dir->d_name)+1))) {
							contp[0] = '\0';   // ensures the memory is an empty string
							// copy to new prefix
							contp = strcat(strcat(contp,set->cpusetdfileprefix),dir->d_name);

							// remove exclusive!
							if (0 > setkernvar(contp, "/cpuset.cpu_exclusive", "0", set->dryrun)){
								warn("Can not remove CPU exclusive : %s", strerror(errno));
							}
						}
						else // realloc error
							err_exit("could not allocate memory!");
					}
				}
				free (contp);
			}

			// clear Docker CGroup settings and affinity first..
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "0", set->dryrun)){
				warn("Can not remove CPU exclusive : %s", strerror(errno));
			}
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", constr, set->dryrun)){
				// global reset failed, try affinity only
				if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity, set->dryrun)){
					warn("Can not reset CPU-affinity. Expect malfunction!"); // set online cpus as default
				}
			}
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.mems", numastr, set->dryrun)){
				warn("Can not set NUMA memory nodes");// TODO: separate NUMA settings
			}

			// rewind, stard configuring
			rewinddir(d);


			{
				char *contp = NULL; // clear pointer
				/// Reassigning pre-existing containers?
				while ((dir = readdir(d)) != NULL) {
				// scan trough docker CGroup, find them?
					if (64 == (strspn(dir->d_name, "abcdef1234567890"))) {
						if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
							+ strlen(dir->d_name)+1))) {
							contp[0] = '\0';   // ensures the memory is an empty string
							// copy to new prefix
							contp = strcat(strcat(contp,set->cpusetdfileprefix),dir->d_name);

							if (0 > setkernvar(contp, "/cpuset.cpus", set->affinity, set->dryrun)){
								warn("Can not set CPU-affinity");
							}
							if (0 > setkernvar(contp, "/cpuset.mems", numastr, set->dryrun)){
								warn("Can not set NUMA memory nodes"); // TODO: separate NUMA settings
							}
						}
						else // realloc error
							err_exit("could not allocate memory!");
					}
				}
				free (contp);
			}

			// Docker CGroup settings and affinity
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity, set->dryrun)){
				warn("Can not set CPU-affinity");
			}
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.mems", numastr, set->dryrun)){
				warn("Can not set NUMA memory nodes");// TODO: separate NUMA settings
			}
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "1", set->dryrun)){
				warn("Can not set CPU exclusive");
			}

			closedir(d);
		}
	}

	//------- CREATE CGROUPs FOR CONFIGURED CONTAINER IDs ------------
	// we know of, so set it up-front
	// TODO: simplify code using single function for all -> internal CG library?
	cont("creating CGroup entries for configured CIDs");
	{
		char * fileprefix = NULL;

		for (cont_t * cont = contparm->cont; ((cont)); cont=cont->next) {

			// check if a valid and full sha256 id
			if (!(cont->contid) || !(64==(strspn(cont->contid, "abcdef1234567890"))))
				continue;
			if ((fileprefix=realloc(fileprefix, strlen(set->cpusetdfileprefix)+strlen(cont->contid)+1))) {

				fileprefix[0] = '\0';   // ensures the memory is an empty string
				// copy to new prefix
				fileprefix = strcat(strcat(fileprefix,set->cpusetdfileprefix), cont->contid);

				// try to create directory
				if(0 != mkdir(fileprefix, ACCESSPERMS) && EEXIST != errno)
				{
					warn("Can not set CGroup: %s", strerror(errno));
					continue;
				}

				if (0 > setkernvar(fileprefix, "/cpuset.cpus", set->affinity, set->dryrun)){
					warn("Can not set CPU-affinity");
				}
				if (0 > setkernvar(fileprefix, "/cpuset.mems", numastr, set->dryrun)){
					warn("Can not set NUMA memory nodes"); // TODO: separte NUMA settings
				}
			}
			else //realloc issues
				err_exit("could not allocate memory!");
		}
	}


	//------- CREATE NEW CGROUP AND MOVE ALL ROOT TASKS TO IT ------------
	// system CGroup, possible tasks are moved -> do for all

	char *fileprefix = NULL;

	cont("creating CGroup for system on %s", cpus);

	if ((fileprefix=malloc(strlen(set->cpusetfileprefix)+strlen(CSET_SYS)+1))) {
		char * nfileprefix = NULL;

		fileprefix[0] = '\0';   // ensures the memory is an empty string
		// copy to new prefix
		fileprefix = strcat(strcat(fileprefix,set->cpusetfileprefix), CSET_SYS);
		// try to create directory
		if(0 != mkdir(fileprefix, ACCESSPERMS) && EEXIST != errno)
		{
			// IF: error - excluding not already existing
			warn("Can not set CPUset system group: %s", strerror(errno));
			cont("this may inflict unexpected delays. Skipping..");
			goto sysend; // skip all system things
		}
		// ELSE: created, or directory already exists

		if (0 > setkernvar(fileprefix, "cpuset.cpus", cpus, set->dryrun)){
			warn("Can not set CPU-affinity");
		}
		if (0 > setkernvar(fileprefix, "cpuset.mems", numastr, set->dryrun)){
			warn("Can not set NUMA memory nodes");
		}
		if (0 > setkernvar(fileprefix, "cpuset.cpu_exclusive", "1", set->dryrun)){
			warn("Can not set CPU exclusive");
		}

		cont( "moving tasks..");

		if ((nfileprefix=malloc(strlen(set->cpusetfileprefix)+strlen("tasks")+1))) {
			nfileprefix[0] = '\0';   // ensures the memory is an empty string
			// copy to new prefix
			nfileprefix = strcat(strcat(nfileprefix,set->cpusetfileprefix),"tasks");

			int mtask = 0;

			char pidline[BUFRD];
			char *pid, *pid_ptr;
			int nleft=0, got; // reading left counter
			// prepare literal and open pipe request
			int path = open(nfileprefix,O_RDONLY);

			// Scan through string and put in array, leave one byte extra, needed for strtok to work
			while((got = read(path, pidline+nleft,BUFRD-nleft-1))) {
				nleft += got;
				printDbg("%s: Pid string return %s\n", __func__, pidline);
				pidline[nleft] = '\0'; // end of read check, nleft = max 1023;
				pid = strtok_r (pidline,"\n", &pid_ptr);
				while (NULL != pid && nleft && (6 < (&pidline[BUFRD-1]-pid))) { // <6 = 5 pid no + \n
					// DO STUFF

					// file prefix still pointing to CSET_SYS
					if (0 > setkernvar(fileprefix, "tasks", pid, set->dryrun)){
						printDbg( "Warn! Can not move task %s\n", pid);
						mtask++;
					}
					nleft-=strlen(pid)+1;
					pid = strtok_r (NULL,"\n", &pid_ptr);
				}
				if (pid) // copy leftover chars to beginning of string buffer
					memcpy(pidline, pidline+BUFRD-nleft-1, nleft);
			}

			close(path);

			// some non-movable tasks?
			if (0!= mtask)
				warn("Could not move %d tasks", mtask);
		}

sysend: // jumped here if not possible to create system

		// free string buffers
		free (fileprefix);
		free (nfileprefix);

	}
	else //re-alloc issues
		err_exit("could not allocate memory!");

	numa_free_cpumask(naffinity);
	free(numastr);

	// TODO: check if it makes sense to do this before or after starting threads
	/* lock all memory (prevent swapping) -- do here */
	if (set->lock_pages) {
		// find lock configuration - depending on CAPS
		int lockt = MCL_CURRENT;
		if ((capMask & CAPMASK_IPC))
			lockt |= MCL_FUTURE;
		if (-1 == mlockall(lockt))
			err_exit_n(errno, "MLockall failed");
	}

	return 0;
}

/// cleanupEnvironment(): Cleanup of some system settings before exiting
///
/// Arguments: - structure with parameter set
///
/// Return value: -
///
void cleanupEnvironment(prgset_t *set){

	// reset and read counters
	if(set->smi) {

		unsigned long smi_old;
		int maxccpu = get_nprocs_conf();
		info("SMI counters for the CPUs");
		// mask affinity and invert for system map / readout of smi of online CPUs
		for (int i=0;i<maxccpu;i++)
			// if smi is set, read SMI counter
			if (*(smi_msr_fd+i)) {
				/* get current smi count to use as base value */
				if (get_smi_counter(*smi_msr_fd+i, &smi_old))
					err_exit_n( errno, "Could not read SMI counter");
				cont("CPU%d: %ld", i, smi_old-*(smi_counter+i));
				close(*smi_msr_fd+i);
			}
	}

	// unlock memory pages
	if (set->lock_pages)
		munlockall();
}
