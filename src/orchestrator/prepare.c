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
#include "resmgnt.h"	// resource management
#include "adaptive.h"	// adaptive scheduling

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

/*
 *  setPidMask(): utility function to set all PIDs of a certain CMD mask's affinity
 *  Arguments: - tag to search for
 * 			   - bit mask to set
 *
 *  Return value: --
 */
static void
setPidMask (char * tag, struct bitmask * amask, char * cpus)
{
	FILE *fp;

	{
		if (!tag)
			err_exit("Process signature tag is a null pointer!");

		int tlen = strlen (tag) + 52;
		char req[tlen];

		// prepare literal and open pipe request, request spid (thread) ids
		// spid and pid coincide for main process
#ifdef BUSYBOX		
		(void)sprintf (req,  "ps -o pid,comm | grep -v 'grep' | grep '%s'", tag);
#else
		(void)sprintf (req,  "ps h -eo spid,command | grep -v 'grep' | grep -G '%s'", tag);
#endif
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
			warn("could not set PID %d-'%s' affinity: %s", mpid, pid, strerror(errno));
		else
			cont("PID %d-'%s' reassigned to CPU's %s", mpid, pid, cpus);
    }

	pclose(fp);
}

/*
 *  getCapMask(): utility function get our capability mask
 *
 *  Arguments: - program settings structure
 *
 *  Return value: --
 */
static int
getCapMask(prgset_t *set) {
	//  verify executable permissions - needed for dry run, not blind run

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

/*
 *  getRRslice(): reads the round-robin slice and or sets defaults for system
 *
 *  Arguments: - structure with parameter set
 *
 *  Return value: -
 */
static void
getRRslice(prgset_t * set){
	char str[100]; // generic string...

	if (SCHED_RR == set->policy && 0 < set->rrtime) {
		cont( "Set round robin interval to %dms..", set->rrtime);
		(void)sprintf(str, "%d", set->rrtime);
		if (0 > setkernvar(set->procfileprefix, "sched_rr_timeslice_ms", str, set->dryrun)){
			warn("RR time slice not changed!");
		}
	}
	else{
		if (0 > getkernvar(set->procfileprefix, "sched_rr_timeslice_ms", str, sizeof(str))){
			warn("Could not read RR time slice! Assuming a default of 100ms");
			set->rrtime=100; // default to 100ms
		}
		else{
			set->rrtime = atoi(str);
			info("RR slice is set to %d ms", set->rrtime);
		}
	}
}

/*
 *  pushCPUirqs(): pushes affine CPU's offline and online again to force-move IRQs
 *
 *  Arguments: - structure with parameter set
 *  		   - bit-size of CPU-masks
 *
 *  Return value: -
 */
static void
pushCPUirqs (prgset_t *set, int mask_sz){
	cont("Trying to push CPU's interrupts");
	if (!set->blindrun && !set->dryrun)
	{
		char fstring[50]; // cpu string
		// bring all affinity except 0 off-line
		for (int i=mask_sz-1;i>0;i--) {

			if (numa_bitmask_isbitset(set->affinity_mask, i)){ // filter by online/existing and affinity

				// verify if cpu-freq is on performance -> set it
				(void)sprintf(fstring, "cpu%d/online", i);
				if (0 > setkernvar(set->cpusystemfileprefix, fstring, "0", set->dryrun))
					err_exit_n(errno, "CPU%d-Hotplug unsuccessful!", i);
				else
					cont("CPU%d off-line", i);
			}
		}
		// bring all back online
		for (int i=1;i<mask_sz;i++) {

			if (numa_bitmask_isbitset(set->affinity_mask, i)){ // filter by online/existing and affinity

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
		cont("skipped.");
}

/*
 * countCGroupTasks : count the number of tasks in the cpu-set docker
 *
 * Arguments: - configuration parameter structure
 *
 * Return value: int with number of tasks, error if < 0
 */
static int
countCGroupTasks(prgset_t *set) {

#ifdef CGROUP2
	char count[16];
	if (0 > getkernvar(set->cpusetdfileprefix, "pids.current", count, 16)){
		warn("Can not read docker number of tasks");
		return -1;
	}
	return atoi(count);
#else
	DIR *d;
	struct dirent *dir;
	d = opendir(set->cpusetdfileprefix);// -> pointing to global
	if (d) {
		int count= 0;

		// CLEAR exclusive flags in all existing containers
		{
			char *contp = NULL; // clear pointer
			while ((dir = readdir(d)) != NULL) {
			// scan trough docker CGroup, find container IDs
				if  ((DT_DIR == dir->d_type)
					&& (64 == (strspn(dir->d_name, "abcdef1234567890")))) {
					if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
						+ strlen(dir->d_name)+1+strlen("/" CGRP_PIDS)))) { // \0 + /tasks
						// copy to new prefix
						contp = strcat(strcpy(contp,set->cpusetdfileprefix),dir->d_name);
						contp = strcat(contp,"/" CGRP_PIDS);

						// Open the file
						FILE * fp = fopen(contp, "r");
						char c;

						// Check if file exists
						if (fp == NULL)
						{
							warn("Could not open file %s", contp);
							return -1;
						}

						// Extract characters from file and store in character c
						for (c = getc(fp); c != EOF; c = getc(fp))
							if (c == '\n') // Increment count if this character is newline
								count = count + 1;

						// Close the file
						fclose(fp);
						printDbg(PFX "The file %s has %d lines\n ", contp, count);

					}
					else // realloc error
						err_exit("could not allocate memory!");
				}
			}
			free (contp);
		}
		closedir(d);
		return count;
	}
	return -1;
#endif
}

/*
 *  prepareEnvironment(): prepares the runtime environment for real-time
 *  operation. Creates CPU shield and configures the affinity of system
 *  processes and interrupts to reduce off-load on RT resources
 *
 *  Arguments: - structure with parameter set
 *
 *  Return value: Error code
 *  				Only valid if the function returns
 */
int
prepareEnvironment(prgset_t *set) {

	if (numa_available())
		err_exit( "NUMA is not available but mandatory for the orchestration");

	{	// System CPU info, allocate SMI counters based on CPU numbers

		// numa_num_configured_cpus() seems to be the same, parsing /sys
		int maxcpu = get_nprocs(); 			// sysconf(_SC_NPROCESSORS_ONLN);
		int maxccpu = get_nprocs_conf();	// sysconf(_SC_NPROCESSORS_CONF);

		info("This system has %d processors configured and "
			"%d processors available.",
			maxccpu, maxcpu);

		smi_counter = calloc (maxccpu, sizeof(long));	// alloc to 0
		smi_msr_fd 	= calloc (maxccpu, sizeof(int));	// alloc to 0
		if (!smi_counter || !smi_msr_fd)
			err_exit("could not allocate memory!");
	}

	char cpus[CPUSTRLEN]; // cpu allocation string
	char constr[CPUSTRLEN]; // cpu online string
	char str[100]; // generic string...

	info("Starting environment setup");

	/// --------------------
	/// verify CPU topology and distribution

	// verify if SMT is disabled -> now force = disable
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

	struct bitmask * naffinity = numa_allocate_cpumask();
	if (!naffinity)
		err_exit("could not allocate memory!");

	// Get size of THIS system's CPU-masks to obtain loop limit (they're dynamic)
	// and avoid to fall into the CPU numbering trap
	int mask_sz = numa_bitmask_nbytes(naffinity) * 8;

	// get online cpu's
	if (0 < getkernvar(set->cpusystemfileprefix, "online", constr, sizeof(constr))) {
		struct bitmask * con = numa_parse_cpustring_all(constr);
		if (!con)
			err_exit("Can not parse online CPUs");

		// mask affinity and invert for system map / readout of smi of online CPUs
		int smi_cpu = 0;

		for (int i=0;i<mask_sz;i++) {

			/*
			 * ---------------------------------------------------------*
			 * Configure online CPUs - HT, SMT and performance settings *
			 * ---------------------------------------------------------*
			 */
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

				// CPU-IDLE settings, added with Kernel 4_15? 4_13?
				(void)sprintf(fstring, "cpu%d/power/pm_qos_resume_latency_us", i); // TODO: value dependent on governor>
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
						cont("CPU power-QoS on CPU%d is set to \"" "n/a" "\" as required", i);
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
					// next CPU
					smi_cpu++;
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

	/* --------------------
	 * running settings for scheduler
	 * --------------------
	 */
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

	// set affinity only if not use-all
	if (numa_sched_setaffinity(mpid, naffinity))
		warn("could not set orchestrator affinity: %s", strerror(errno));
	else
		cont("Orchestrator's PID reassigned to CPU's %s", cpus);

	/* --------------------
	 * Docker CGROUP setup - detection if present
	 * --------------------
	 */

	{ // start environment detection CGroup
	struct stat s;

	int err = stat(set->cpusetdfileprefix, &s);
	if(-1 == err) {
		// Docker CGroup not found, set->force enabled = try creating
		if(ENOENT == errno && set->force) { // TODO: check docker setting of CGroup.slice for v2
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

	/* --------------------
	 * detect NUMA configuration, sets all nodes to active (for now)
	 * --------------------
	 */
	char * numastr = malloc (5);
	if (!(numastr))
			err_exit("could not allocate memory!");
	if (-1 != numa_available()) {
		int numanodes = numa_max_node();

		(void)sprintf(numastr, "0-%d", numanodes);
	}
	else{
		warn("NUMA not enabled, defaulting to memory node '0'");
		// default NUMA string
		(void)sprintf(numastr, "0");
	}

	/* --------------------
	 * Kernel variables, disable bandwidth management and RT-throttle
	 * Kernel RT-bandwidth management must be disabled to allow deadline+affinity
	 * --------------------
	 */
	set->kernelversion = check_kernel(); // TODO: update

	if (KV_NOT_SUPPORTED == set->kernelversion)
		warn("Running on unknown kernel version; Trying generic configuration..");

	if (resetRTthrottle (set, -1)){ // TODO: throttle is limited if no affinity lock is set
		// reset failed, let's try a CGroup reset first?? partitioned should work
		cont( "trying to reset Docker's CGroups CPU's to %s first", set->affinity);
		resetContCGroups(set, constr, numastr);
		setContCGroups(set, numastr);

		// retry
		resetRTthrottle (set, -1);
	}
	getRRslice(set);

	// here.. off-line messes up CSET
	cont("moving kernel thread affinity");
	// kernel interrupt threads affinity
	setPidMask("\\B\\[ehca_comp[/][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[irq[/][[:digit:]]*-[[:alnum:]]*", naffinity, cpus);
	setPidMask("\\B\\[kcmtpd_ctr[_][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[kworker[/][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[rcuop[/][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[rcuos[/][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[rcuog[/][[:digit:]]*", naffinity, cpus);


	if (0 == countCGroupTasks(set))
		// ksoftirqd -> offline, online again
		pushCPUirqs(set, mask_sz);
	else
		info("Running container tasks present, skipping CPU hot-plug");

	/* --------------------
	 * CGroup present, fix CPU-sets of running containers
	 * --------------------
	 */
	cont( "reassigning Docker's CGroups CPU's to %s", set->affinity);
	resetContCGroups(set, constr, numastr);
	setContCGroups(set, numastr);


	// lockup detector
	// echo 0 >  /proc/sys/kernel/watchdog
	// or echo 9999 >  /proc/sys/kernel/watchdog

	/*------- CREATE CGROUPs FOR CONFIGURED CONTAINER IDs ------------
	 * we know of, so set it up-front
	 * --------------------
	 */
	cont("creating CGroup entries for configured CIDs");
	{
		char * fileprefix = NULL;

		for (cont_t * cont = contparm->cont; ((cont)); cont=cont->next) {

			// check if a valid and full sha256 id
			if (!(cont->contid) || !(64==(strspn(cont->contid, "abcdef1234567890"))))
				continue;
			if ((fileprefix=realloc(fileprefix, strlen(set->cpusetdfileprefix)+strlen(cont->contid)
#ifndef CGROUP2
					+1))) {
				// copy to new prefix
				fileprefix = strcat(strcpy(fileprefix,set->cpusetdfileprefix), cont->contid);
#else
					+strlen(CGRP_DCKP CGRP_DCKS)+1))) { // 'docker-' + '.scope' = '\n'

				// copy to new prefix
				fileprefix = strcat(strcpy(fileprefix,set->cpusetdfileprefix), CGRP_DCKP);
				fileprefix = strcat(strcat(fileprefix,cont->contid), CGRP_DCKS);
#endif

				// try to create directory // TODO update string for v2
				if(0 != mkdir(fileprefix, ACCESSPERMS) && EEXIST != errno)
				{
					warn("Can not set CGroup: %s", strerror(errno));
					continue;
				}

				if (0 > setkernvar(fileprefix, "/cpuset.cpus", set->affinity, set->dryrun)){
					warn("Can not set CPU-affinity");
				}
				if (0 > setkernvar(fileprefix, "/cpuset.mems", numastr, set->dryrun)){
					warn("Can not set NUMA memory nodes");
				}
			}
			else //realloc issues
				err_exit("could not allocate memory!");
		}
	}

	// TODO: add function to detect if GGv2 docker slice has been created
	/* ------- CREATE NEW CGROUP AND MOVE ALL ROOT TASKS TO IT ------------
	 * system CGroup, possible tasks are moved -> do for all
	 * --------------------
	 */
	char *fileprefix = NULL;

#ifndef CGROUP2
	cont("creating CGroup for system on %s", cpus);
#endif
	if ((fileprefix=malloc(strlen(set->cgroupfileprefix)+strlen(CGRP_CSET CGRP_SYS)+1))) {
		char * nfileprefix = NULL;

		// copy to new prefix
		fileprefix = strcat(strcpy(fileprefix,set->cgroupfileprefix), CGRP_CSET	CGRP_SYS);

#ifndef CGROUP2
// try to create directory
		if(0 != mkdir(fileprefix, ACCESSPERMS) && EEXIST != errno)
		{
			// IF: error - excluding not already existing
			warn("Can not set CPUset system group: %s", strerror(errno));
			cont("this may inflict unexpected delays. Skipping..");
			goto sysend; // skip all system things
		}
		// ELSE: created, or directory already exists
#endif

		if (0 > setkernvar(fileprefix, "cpuset.cpus", cpus, set->dryrun)){
			warn("Can not set CPU-affinity");
		}
		if (0 > setkernvar(fileprefix, "cpuset.mems", numastr, set->dryrun)){
			warn("Can not set NUMA memory nodes");
		}
#ifndef CGROUP2
		// CGroup2 -> user slice is also present and would loose all control if Sys/docker use all CPUs
		// if docker.slice has a root partition, the resources are removed from the root partition, avoiding overlaps - unlike v1
		if (AFFINITY_USEALL != set->setaffinity) // set only if not set use-all
			if (0 > setkernvar(fileprefix, "cpuset.cpu_exclusive", "1", set->dryrun)){
				warn("Can not set CPU exclusive partition: %s", strerror(errno));
			}

		cont( "moving tasks..");

		if ((nfileprefix=malloc(strlen(set->cgroupfileprefix)+strlen(CGRP_CSET CGRP_PIDS)+1))) {
			// copy to new prefix
			nfileprefix = strcat(strcpy(nfileprefix,set->cgroupfileprefix), CGRP_CSET CGRP_PIDS);

			int mtask = 0;

			char buf[BUFRD];
			char *pid, *pid_ptr;
			int nleft=0;	// parsing left counter
			int ret;

			// prepare literal and open pipe request
			int path = open(nfileprefix, O_RDONLY);

			// Scan through string and put in array, stops on EOF
			while((ret = read(path, buf+nleft,BUFRD-nleft-1))) {

				if (0 > ret){
					if (EINTR == errno) // retry on interrupt
						continue;
					warn("kernel tasks read error!");
					break;
				}

				nleft += ret;

				printDbg("%s: Pid string return %s\n", __func__, buf);
				buf[nleft] = '\0'; // end of read check, nleft = max 1023;
				pid = strtok_r (buf,"\n", &pid_ptr);
				while (NULL != pid && nleft && (6 < (&buf[BUFRD-1]-pid))) { // <6 = 5 pid no + \n
					// DO STUFF

					// file prefix still pointing to CGRP_SYS
					if (0 > setkernvar(fileprefix, CGRP_PIDS, pid, set->dryrun)){
						//printDbg( "Warn! Can not move task %s\n", pid);
						mtask++;
					}
					nleft-=strlen(pid)+1;
					pid = strtok_r (NULL,"\n", &pid_ptr);
				}
				if (pid) // copy leftover chars to beginning of string buffer
					memcpy(buf, buf+BUFRD-nleft-1, nleft);
			}

			close(path);

			// some non-movable tasks?
			if (0!= mtask)
				warn("Could not move %d tasks", mtask);
		}

sysend: // jumped here if not possible to create system
#endif

		// free string buffers
		free (fileprefix);
		free (nfileprefix);

	}
	else //re-alloc issues
		err_exit("could not allocate memory!");

	numa_free_cpumask(naffinity);
	free(numastr);

	/* lock all memory (prevent swapping) -- do here */
	if (set->lock_pages) {
		// find lock configuration - depending on CAPS
		int lockt = MCL_CURRENT;
		if ((capMask & CAPMASK_IPC))
			lockt |= MCL_FUTURE;
		if (-1 == mlockall(lockt))
			err_exit_n(errno, "MLockall failed");
	}

	info("Environment setup complete, starting orchestration...");
	return 0;
}

/*
 *  cleanupEnvironment(): Cleanup of some system settings before exiting
 *
 *  Arguments: - structure with parameter set
 *
 *  Return value: -
 */
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

	// TODO: restore CGroup 2 to member
	freeTracer(&rHead); // free
	adaptFree();

	// unlock memory pages
	if (set->lock_pages)
		munlockall();
}
