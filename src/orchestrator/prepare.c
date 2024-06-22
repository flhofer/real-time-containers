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
static int cpuretry = 1;		// allow one retry to reset CPUs online

// static capability mask
#define CAPMASK_ALL		0x7
#define CAPMASK_NICE	0x1
#define CAPMASK_RES		0x2
#define CAPMASK_IPC		0x4
static int capMask = CAPMASK_ALL;

static void resetCPUonline (prgset_t *set);
static int resetCPUstring(prgset_t *set);

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
		if (0 > setkernvar(set->procfileprefix, "sched_rr_timeslice_ms", str, set->dryrun & MSK_DRYNORTSLCE)){
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
pushCPUirqs (prgset_t *set){

	cont("Trying to push CPU's interrupts");
	if (set->blindrun || (set->dryrun & MSK_DRYNOCPUPSH))
		cont("skipped.");

	char fstring[50]; // CPU VFS string

	// bring all affinity except 0 off-line
	for (int i=set->affinity_mask->size-1;i>0;i--)
		if (numa_bitmask_isbitset(set->affinity_mask, i)){ // filter by online/existing and affinity

			(void)sprintf(fstring, "cpu%d/online", i);
			if (0 > setkernvar(set->cpusystemfileprefix, fstring, "0", 0))
				err_exit_n(errno, "CPU%d-Hotplug unsuccessful!", i);

			cont("CPU%d off-line", i);
		}

	// bring all back online
	for (int i=1;i<set->affinity_mask->size;i++)
		if (numa_bitmask_isbitset(set->affinity_mask, i)){ // filter by online/existing and affinity

			(void)sprintf(fstring, "cpu%d/online", i);
			if (0 > setkernvar(set->cpusystemfileprefix, fstring, "1", 0))
				err_exit_n(errno, "CPU%d-Hotplug unsuccessful!", i);

			cont("CPU%d online", i);
		}
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
 *  testSMT(): verify SMT status
 *
 *  Arguments: - structure with parameter set
 *
 *  Return value: Error if -1, 0 off, 1 on
 */
static int
testSMT(prgset_t *set) {

	char str[10]; 	// generic string...

	// verify if SMT is disabled -> now force = disable
	if (!(set->blindrun)
			&& (0 < getkernvar(set->cpusystemfileprefix, "smt/control", str, sizeof(str))))
		// value read OK
		return (!strcmp(str, "on"));

	// SMT failed or DryRun
	warn("Skipping read of SMT status. This can influence latency performance!");

	return -1;
}

/*
 *  switchCPUsibling(): disable/enable SMT sibling through kernel file system
 *
 *  Arguments: - structure with parameter set
 * 			   - bit-mask to use, resets bits of disabled CPUs
 *
 *  Return value: Error if -1, 0 ok, 1 = reset mask
 */
static int
disableCPUsibling(prgset_t *set) {

	char fstring[50]; 	// CPU VFS file string
	char str[50]; 		// generic string...

	if (set->dryrun && MSK_DRYNOSMTOFF)
		cont("Skipping Hot-plug but testing mask..");

	for (int i=0;i<set->affinity_mask->size;i++)
		if (numa_bitmask_isbitset(set->affinity_mask, i)){ // filter by set bits

			(void)sprintf(fstring, "cpu%d/topology/thread_siblings_list", i);
			if (0 >= getkernvar(set->cpusystemfileprefix, fstring, str, sizeof(str))){
				warn ("Can not read CPU topology data! Skipping.");
				return -1;
			}

			struct bitmask * sibling = parse_cpumask(str);
			if (NULL == sibling){
				err_msg("Unable to parse sibling list '%s'!", str);
				return -1;
			}

			numa_bitmask_clearbit(sibling, i);

			if (0 == numa_bitmask_weight(sibling)){
				numa_free_cpumask(sibling);
				continue;
			}

			for (int j=0;j<set->affinity_mask->size;j++)
				if (numa_bitmask_isbitset(sibling, j)){
					int cpu = j;
					if (j<i){
						warn("Using sibling thread CPU%d in RT affinity and can not disable main CPU%d thread not in RT-range!", i, j);
						if (1 == numa_bitmask_weight(set->affinity_mask))
							continue;
						warn("Disabling sibling CPU%d for RT-operation instead!", i);
						cpu=i;
					}
					numa_bitmask_clearbit(set->affinity_mask, cpu);

					if (set->dryrun && MSK_DRYNOSMTOFF)	// Do nothing, just test mask
						continue;

					(void)sprintf(fstring, "cpu%d/online", cpu);
					if (0 > setkernvar(set->cpusystemfileprefix, fstring, "0", 0)){
						err_msg_n(errno, "CPU%d-Hotplug unsuccessful!", cpu);
						continue;
					}

					cont("CPU%d's sibling CPU%d is now off-line.", (i==cpu) ? j : i, cpu);
				}
			numa_free_cpumask(sibling);
			}

	// Reconstruct affinity list
	if (resetCPUstring(set))
		return -1;

	return 0;
}

/*
 *  disableSMT(): disable SMT through kernel file system
 *
 *  Arguments: - structure with parameter set
 *
 *  Return value: Error if -1, 0 ok, 1 = reset mask
 *  				Only valid if the function returns
 */
static int
disableSMT(prgset_t *set) {

	if (set->dryrun & MSK_DRYNOSMTOFF){
		cont("Skipping setting SMT.");
		return 0;
	}
	if (!set->force)
		err_exit("SMT is enabled. Set -f (force) flag to authorize disabling");

	if (0 > setkernvar(set->cpusystemfileprefix, "smt/control", "off", 0))
		err_exit_n(errno, "SMT is enabled. Disabling was unsuccessful!");

	// We just disabled some CPUs -> re-check affinity
	cont("SMT is now disabled, as required. Refresh configurations..");

	return 1;
}

/*
 *  resetCPUstring(): reconstructs the affinity list
 *
 *  Arguments: - structure with parameter set
 *
 *  Return value: Error if -1, 0 ok
 *  				Only valid if the function returns
 */
static int
resetCPUstring(prgset_t *set){

	char str[100]; // generic string...

	// replace affinity string with new string!
	if (parse_bitmask(set->affinity_mask, str, sizeof(str))){
		err_msg("Could not reconstruct CPU-list from new CPU-mask");
		return -1;
	}

	free(set->affinity);
	set->affinity = strdup(str);

	return 0;
}



/*
 *  resetCPUmask(): test the affinity mask for correctness (offline CPUs in mask?)
 *
 *  Arguments: - structure with parameter set
 *
 *  Return value: Error if -1, 0 ok
 *  				Only valid if the function returns
 */
static int
resetCPUmask(prgset_t *set){

	char str[100]; // generic string...

	// Did the number of available CPUs change?
	// SMT has been disabled - update affinity mask

	struct bitmask * conmask;
	struct bitmask * oldmask = numa_allocate_cpumask();

	copy_bitmask_to_bitmask(set->affinity_mask, oldmask); 		// keep a copy for comparison

	// get online cpu's
	if (0 < getkernvar(set->cpusystemfileprefix, "online", str, sizeof(str))) {
		conmask = numa_parse_cpustring_all(str);
		if (!conmask)
			err_exit("Can not parse online CPUs");
		numa_and_cpumask(conmask,set->affinity_mask);				// AND with online CPUs
		numa_free_cpumask(conmask);
	}
	else
		err_exit("Can not read online CPUs");

	if (!numa_bitmask_equal(set->affinity_mask, oldmask)){		// Did the mask change?
		if (0 == numa_bitmask_weight(set->affinity_mask)) {
			err_msg("After disabling SMT, the resulting CPUset is empty!");
			numa_free_cpumask(oldmask);
			return -1; // return to display help
		}
		warn("Disabling SMT has reduced the number of available CPUs for real-time tasks from %d to %d!",
				numa_bitmask_weight(oldmask), numa_bitmask_weight(set->affinity_mask));

		numa_free_cpumask(oldmask);

		if (resetCPUstring(set))
			return -1;
	}
	else
		numa_free_cpumask(oldmask);

	return 0;
}


/*
 *  setCPUgovernor(): set CPU governor to performance
 *
 *  Arguments: - structure with parameter set
 *  		   - CPU number to set
 *
 *  Return value: Error code
 *  				Only valid if the function returns
 */
static int
setCPUgovernor(prgset_t *set, int cpuno) {
	char fstring[50]; 	// CPU VFS file string
	char str[50]; 		// generic string...
	char poss[50]; 		// possible settings string for governors

	// read set scaling governor
	(void)sprintf(fstring, "cpu%d/cpufreq/scaling_governor", cpuno);
	if (0 >= getkernvar(set->cpusystemfileprefix, fstring, str, sizeof(str))){
		warn("CPU%d Scaling governor settings not found. Skipping.", cpuno);
		return -1;
	}

	// verify if CPU-freq is on performance
	if (!strcmp(str, CPUGOVR)) {
		cont("CPU-freq on CPU%d is set to \"" CPUGOVR "\" as required", cpuno);
		return 0;
	}

	// value possible governors read ok?
	(void)sprintf(fstring, "cpu%d/cpufreq/scaling_available_governors", cpuno);
	if (0 >= getkernvar(set->cpusystemfileprefix, fstring, poss, sizeof(poss))){
		warn("CPU%d available CPU scaling governors not found. Skipping.", cpuno);
		return -1;
	}

	// verify if CPU-freq is on performance
	if (NULL == strstr(poss, CPUGOVR)) {
		warn("\"" CPUGOVR "\" is not part of possible CPU governors!", cpuno);
		info("Possible CPU-freq scaling governors \"%s\" on CPU%d.", poss, cpuno);
		cont("Skipping setting of governor on CPU%d.", cpuno);
		return -1;
	}

	// Governor is set to a different value
	// ---

	if ((set->dryrun & MSK_DRYNOCPUGOV) || set->blindrun){
		cont("Skipping setting of governor on CPU%d.", cpuno);
		return 0;
	}

	if (!set->force)
		err_exit("CPU-freq is set to \"%s\" on CPU%d. Set -f (force) flag to authorize change to \"" CPUGOVR "\"", str, cpuno);

	if (0 > setkernvar(set->cpusystemfileprefix, fstring, CPUGOVR, 0))
		err_exit_n(errno, "CPU-freq change unsuccessful!");

	cont("CPU-freq on CPU%d is now set to \"" CPUGOVR "\" as required", cpuno);

	return 0;
}

/*
 *  adjustCPUfreq(): set CPUfreq minimum frequency to base/max to avoid scaling
 *
 *  Arguments: - structure with parameter set
 *  		   - CPU number to set
 *
 *  Return value: Error code
 *  				Only valid if the function returns
 */
static int
adjustCPUfreq(prgset_t *set, int cpuno) {
	char fstring[50]; 	// CPU VFS file string
	char str[50]; 		// generic string...
	char setfrq[50]; 	// frequency setting for scaling, either base or max frequency
	int noturbo = 1;	// is turbo disabled?

	// Disable Turbo if present
	if (0 < getkernvar(set->cpusystemfileprefix, "intel_pstate/no_turbo", str, sizeof(str))){
		if ((strcmp(str, "1"))
			&& (0 > setkernvar(set->cpusystemfileprefix, "intel_pstate/no_turbo", "1", 0))) {
				err_msg_n(errno, "Disabling Intel Turbo was unsuccessful!");
				noturbo = 0;
			}
	}
	else
		printDbg(PIN "Can not read Turbo-state. Is your CPU Turbo-capable? Skipping.");

	// verify if CPU-freq is on performance -> set it
	(void)sprintf(fstring, "cpu%d/cpufreq/base_frequency", cpuno);
	if (0 >= getkernvar(set->cpusystemfileprefix, fstring, setfrq, sizeof(setfrq))){
		if (!noturbo){
			// NOTE: skip min=max setting if base frequency not set to avoid setting min=max=Turbo => overheating
			warn("CPU%d frequency scaling base-frequency not found. Is your CPU Turbo-Capable? Skipping.", cpuno);
			return -1;
		}
		(void)sprintf(fstring, "cpu%d/cpufreq/cpuinfo_max_freq", cpuno);
		if (0 >= getkernvar(set->cpusystemfileprefix, fstring, setfrq, sizeof(setfrq))){
			warn("CPU%d frequency max-frequency info not found. Skipping.", cpuno);
			return -1;
		}
		noturbo = 2; // no turbo ok but we are using max_freq instead of base-freq
	}

	// value possible read ok
	(void)sprintf(fstring, "cpu%d/cpufreq/scaling_min_freq", cpuno);
	if (0 >= getkernvar(set->cpusystemfileprefix, fstring, str, sizeof(str))){
		warn("CPU%d frequency minimum settings not found. Skipping.", cpuno);
		return -1;
	}

	// value act read ok
	if (!strcmp(str, setfrq)) {
		cont("CPU-freq minimum on CPU%d is set to \"%s\" as required", cpuno, str);
		return 0;
	}

	// Minimum frequency is set to a different value
	if (1 == noturbo)
		cont("Base frequency set to \"%s\" on CPU%d.", setfrq, cpuno);
	else
		cont("Max frequency set to \"%s\" on CPU%d.", setfrq, cpuno);

	if ((set->dryrun & MSK_DRYNOCPUGOV) || set->blindrun || !set->force){
		cont("Skipping setting of minimum frequency on CPU%d.", cpuno);
		return 0;
	}

	if (0 > setkernvar(set->cpusystemfileprefix, fstring, setfrq, 0))
		err_msg_n(errno, "CPU-freq change unsuccessful!");

	cont("CPU-freq minimum frequency on CPU%d is now set to %s", cpuno, setfrq);

	return 0;
}

/*
 *  setCPUpowerQos(): set the CPU's power management QoS behavior
 *
 *  Arguments: - structure with parameter set
 *  		   - CPU number to set
 *
 *  Return value: Error code
 *  				Only valid if the function returns
 */
static int
setCPUpowerQos(prgset_t *set, int cpuno) {
	char fstring[50]; 	// CPU VFS file string
	char str[50]; 		// generic string...

	// CPU-IDLE settings, added with Kernel 4_15? 4_13?
	(void)sprintf(fstring, "cpu%d/power/pm_qos_resume_latency_us", cpuno);
	if (0 >= getkernvar(set->cpusystemfileprefix, fstring, str, sizeof(str))){
		warn("CPU%d power saving configuration not found. Skipping.", cpuno);
		return -1;
	}

	// value act read ok
	if (!strcmp(str, "n/a")){
		cont("CPU power-QoS on CPU%d is set to \"" "n/a" "\" as required", cpuno);
		return 0;
	}

	if ((set->dryrun & MSK_DRYNOCPUQOS) || set->blindrun){
		cont("Skipping setting of power QoS policy on CPU%d.", cpuno);
		return 0;
	}

	cont("CPU%d's power-QoS is \"%s\".", cpuno, str);

	if (!set->force)
		err_exit("Set -f (force) flag to authorize change to \"" "n/a" "\"", str, cpuno);

	if (0 > setkernvar(set->cpusystemfileprefix, fstring, "n/a", 0))
		err_exit_n(errno, "CPU-QoS change unsuccessful!");

	cont("CPU power-QoS on CPU%d is now set to \"" "n/a" "\" as required", cpuno);

	return 0;
}

/*
 *  resetCPUonline(): return all CPUs online and retry
 *
 *  Arguments: - structure with parameter set
 *
 *  Return value: -
 */
static void
resetCPUonline (prgset_t *set){

	// Limit to present CPUs
	int mask_sz = MIN(get_nprocs_conf(), set->affinity_mask->size);

	cont("Trying reset CPU's online status");
	if (!set->blindrun)
	{
		if (0 > setkernvar(set->cpusystemfileprefix, "smt/control", "on", 0))
			err_exit_n(errno, "Can not change SMT state!");

		char fstring[CMD_LEN]; 	// CPU VFS string
		struct stat s;			// Stats to check if CPU exists (to permit holes)
		// bring all back online
		for (int i=1;i<mask_sz;i++) {

			(void)sprintf(fstring, "%scpu%d/online", set->cpusystemfileprefix, i);

			if(!stat(set->cpusetdfileprefix, &s))
				continue;

			(void)sprintf(fstring, "cpu%d/online", i);
			if (0 > setkernvar(set->cpusystemfileprefix, fstring, "1", 0))
				err_msg_n(errno, "CPU%d-Hotplug unsuccessful!", i);
			else
				cont("CPU%d online", i);
		}
	}
	else
		cont("skipped.");
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

	info("Starting environment setup");

	// prepare bit-mask, no need to do it before
	set->affinity_mask = parse_cpumask(set->affinity);
	if (!set->affinity_mask){
		err_msg("The resulting CPUset is empty");
		return -1; // return to display help
	}

	/// --------------------
	/// verify CPU topology and distribution
	if (!testSMT(set))
		cont("SMT is disabled, as required");
	else
		{
			int ret = 0;

			if (disableCPUsibling(set)) // selective SMT off
				// selective failed, try traditional
				ret = disableSMT(set); // SMT off

			if (1 == ret && (resetCPUmask(set)))
					return -1;
			if (-1 == ret)
				return -1;
		}

	/*
	 * ---------------------------------------------------------*
	 * Configure online CPUs - Power and performance settings *
	 * ---------------------------------------------------------*
	 */

	// get online cpu's
	if (0 >= getkernvar(set->cpusystemfileprefix, "online", constr, sizeof(constr)))
		// online CPU string not readable
		err_exit("Can not read online CPUs");

	struct bitmask * naffinity = numa_allocate_cpumask();
	if (!naffinity)
		err_exit("could not allocate memory!");

	{
		struct bitmask * con = numa_parse_cpustring_all(constr);
		if (!con)
			err_exit("Can not parse online CPUs");

		// mask affinity and invert for system map / readout of smi of online CPUs
		int smi_cpu = 0;

		for (int i=0;i<set->affinity_mask->size;i++) {

			if (numa_bitmask_isbitset(con, i)){ // filter by online/existing

				if (setCPUgovernor(set, i))
					warn("Could not configure CPU governor. Expect performance loss!");

				if (adjustCPUfreq(set, i))
					warn("Could not configure CPU-frequency. Expect performance loss!");

				if (setCPUpowerQos(set, i))
					warn("Could not configure CPU PM-QoS latency. Expect performance loss!");

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

				if ((set->force) & (cpuretry)){
					cpuretry--;
					resetCPUonline(set);
					return prepareEnvironment(set); // retry
				}
				else
					err_exit("Unavailable CPUset for affinity.");
			}
		}
		numa_free_cpumask(con);
	}


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
		if (sched_setattr (mpid, &attr, 0U))	// Custom function!
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
			if(ENOENT == errno && set->force) {
				warn("CGroup '%s' does not exist. Is the daemon running?", set->cont_cgrp);
				if (0 != mkdir(set->cpusetdfileprefix, ACCESSPERMS))
					err_exit_n(errno, "Can not create container group");
				// if it worked, stay in Container mode
				err=0;
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
	 * Kernel variables, disable bandwidth management and RT-throttle
	 * Kernel RT-bandwidth management must be disabled to allow deadline+affinity
	 * --------------------
	 */
	set->kernelversion = check_kernel();

	if (KV_NOT_SUPPORTED == set->kernelversion)
		warn("Running on unknown kernel version; Trying generic configuration..");

	if (resetRTthrottle (set, -1)){
		// reset failed, let's try a CGroup reset first?? partitioned should work
		cont( "trying to reset Docker's CGroups CPU's to %s first", set->affinity);
		resetContCGroups(set, constr, set->numa);
		setContCGroups(set, 1);

		// retry
		resetRTthrottle (set, -1);
	}
	getRRslice(set);

	if (!(set->dryrun & MSK_DRYNOKTRDAF)){
		// here.. off-line messes up CSET
		cont("moving kernel thread affinity");
		// kernel interrupt threads affinity
		setPidMask("\\B\\[ehca_comp[/][[:digit:]]*", naffinity, cpus);
		setPidMask("\\B\\[irq[/][[:digit:]]*-[[:alnum:]]*", naffinity, cpus);
		setPidMask("\\B\\[kcmtpd_ctr[_][[:digit:]]*", naffinity, cpus);
	#ifndef CGROUP2 // controlled by root slice in CGroup v2 - affinity does not work
		setPidMask("\\B\\[kworker[/][[:digit:]]*", naffinity, cpus);
	#endif
		setPidMask("\\B\\[rcuop[/][[:digit:]]*", naffinity, cpus);
		setPidMask("\\B\\[rcuos[/][[:digit:]]*", naffinity, cpus);
		setPidMask("\\B\\[rcuog[/][[:digit:]]*", naffinity, cpus);
	}
	else
		cont("skipping affionity-set for kernel threads");

	if (0 == countCGroupTasks(set))
		// ksoftirqd -> offline, online again
		pushCPUirqs(set);
	else
		info("Running container tasks present, skipping CPU hot-plug");

	/* --------------------
	 * CGroup present, fix CPU-sets of running containers
	 * --------------------
	 */
	cont( "reassigning Docker's CGroups CPU's to %s", set->affinity);
	resetContCGroups(set, constr, set->numa);
	setContCGroups(set, 1);


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

				// try to create directory
				if(0 != mkdir(fileprefix, ACCESSPERMS) && EEXIST != errno)
				{
					warn("Can not set CGroup: %s", strerror(errno));
					continue;
				}

				if (0 > setkernvar(fileprefix, "/cpuset.cpus", set->affinity, set->dryrun & MSK_DRYNOAFTY)){
					warn("Can not set CPU-affinity");
				}
				if (0 > setkernvar(fileprefix, "/cpuset.mems", set->numa, set->dryrun & MSK_DRYNOAFTY)){
					warn("Can not set NUMA memory nodes");
				}
			}
			else //realloc issues
				err_exit("could not allocate memory!");
		}
	}

	/* ------- CREATE NEW CGROUP AND MOVE ALL ROOT TASKS TO IT ------------
	 * system CGroup, possible tasks are moved -> do for all
	 * --------------------
	 */
	char *fileprefix = NULL;

#ifndef CGROUP2
	cont("creating CGroup for system on %s", cpus);
#endif
	if ((fileprefix=malloc(strlen(set->cgroupfileprefix)+strlen(CGRP_CSET CGRP_SYS)+1))) {

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

		if (0 > setkernvar(fileprefix, "cpuset.cpus", cpus, set->dryrun & MSK_DRYNOAFTY)){
			warn("Can not set CPU-affinity");
		}
		if (0 > setkernvar(fileprefix, "cpuset.mems", set->numa, set->dryrun & MSK_DRYNOAFTY)){
			warn("Can not set NUMA memory nodes");
		}
#ifdef CGROUP2	// redo for user slice
		// CGroup2 -> user slice is also present and would loose all control if Sys/docker use all CPUs
		// if docker.slice has a root partition, the resources are removed from the root partition, avoiding overlaps - unlike v1
		if ((fileprefix=realloc(fileprefix, strlen(set->cgroupfileprefix)+strlen(CGRP_USER)+1))) {
			// copy to new prefix
			fileprefix = strcat(strcpy(fileprefix,set->cgroupfileprefix),CGRP_USER);


			if (0 > setkernvar(fileprefix, "cpuset.cpus", cpus, set->dryrun & MSK_DRYNOAFTY)){
				warn("Can not set CPU-affinity");
			}
			if (0 > setkernvar(fileprefix, "cpuset.mems", set->numa, set->dryrun & MSK_DRYNOAFTY)){
				warn("Can not set NUMA memory nodes");
			}
		}
#else
		if (AFFINITY_USEALL != set->setaffinity) // set only if not set use-all
			if (0 > setkernvar(fileprefix, "cpuset.cpu_exclusive", "1", set->dryrun & MSK_DRYNOCGRPRT)){
				warn("Can not set CPU exclusive partition: %s", strerror(errno));
			}

		cont( "moving tasks..");

		char * nfileprefix = NULL;

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

				printDbg(PFX "Pid string return %s\n", buf);
				buf[nleft] = '\0'; // end of read check, nleft = max 1023;
				pid = strtok_r (buf,"\n", &pid_ptr);
				while (NULL != pid && nleft && (6 < (&buf[BUFRD-1]-pid))) { // <6 = 5 pid no + \n
					// DO STUFF

					// file prefix still pointing to CGRP_SYS
					if (0 > setkernvar(fileprefix, CGRP_PIDS, pid, set->dryrun & MSK_DRYNOTSKPSH)){
						//printDbg(PFX "Warn! Can not move task %s\n", pid);
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
		// free string buffers
		free (nfileprefix);
#endif
		// free string buffers
		free (fileprefix);

	}
	else //re-alloc issues
		err_exit("could not allocate memory!");

	numa_free_cpumask(naffinity);

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

	freeTracer(&rHead); // free
	adaptFree();

	// unlock memory pages
	if (set->lock_pages)
		munlockall();
}
