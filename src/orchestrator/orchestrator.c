// main settings and header file
#include "orchestrator.h"

// header files of launched threads
#include "update.h"
#include "manage.h"

// header file of configuration parser
#include "parse_config.h"

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
#include "rt-utils.h"	// trace and other utils
#include "kernutil.h"	// generic kernel utilities
#include "error.h"		// error and std error print functions

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

static void display_help(int); // declaration for compatibility

/* --------------------------- Global variables for all the threads and programms ------------------ */

containers_t * contparm; // container parameter settings
prgset_t * prgset; // program settings structure

#ifdef DEBUG
// debug output file
	FILE  * dbg_out;
#endif
// signal to keep status of triggers ext SIG
volatile sig_atomic_t stop;
// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;
// head of pidlist - PID runtime and configuration details
node_t * head = NULL;

// configuration read file -- TEMP public, -> then change to static
char * config = "config.json";

/* -------------------------------------------- DECLARATION END ---- CODE BEGIN -------------------- */

/// inthand(): interrupt handler for infinite while loop, help 
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: -
void inthand ( int signum ) {
	stop = 1;
}

// -------------- LOCAL variables for all the functions  ------------------

// TODO:  implement fifo thread as in cyclic-test for readout
//static pthread_t fifo_threadid;
//static char fifopath[MAX_PATH];

static unsigned long * smi_counter = NULL; // points to the list of SMI-counters
static int * smi_msr_fd = NULL; // points to file descriptors for MSR readout

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

/// prepareEnvironment(): prepares the runtime environment for real-time
/// operation. Creates CPU shield and configures the affinity of system
/// processes and interrupts to reduce off-load on RT resources
///
/// Arguments: - structure with parameter set
///
/// Return value: error code if present
///
static void prepareEnvironment(prgset_t *set) {

	/// --------------------
	/// verify 	cpu topology and distribution
	int maxcpu = get_nprocs();	
	int maxccpu = get_nprocs_conf(); // numa_num_configured_cpus();

	char cpus[10]; // cpu allocation string
	char constr[10]; // cpu online string
	char str[100]; // generic string... 

	info("This system has %d processors configured and "
        "%d processors available.",
        maxccpu, maxcpu);

	if (numa_available())
		err_exit( "NUMA is not available but mandatory for the orchestration");		

	info("Starting environment setup");

	// verify if SMT is disabled -> now force = disable, TODO: may change to disable only concerned cores
	if (!(set->blindrun) && (0 < getkernvar(set->cpusystemfileprefix, "smt/control", str, sizeof(str)))){
		// value read ok
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
	// TODO: else

	// prepare bit-mask, no need to do it before
	set->affinity_mask = parse_cpumask(set->affinity, maxccpu);
	if (!set->affinity_mask)
		display_help(1);

	smi_counter = calloc (maxccpu, sizeof(long));
	smi_msr_fd = calloc (maxccpu, sizeof(int));
	if (!smi_counter || !smi_msr_fd)
		err_exit("could not allocate memory!");

	struct bitmask * con;
	struct bitmask * naffinity = numa_bitmask_alloc((maxccpu/sizeof(long)+1)*sizeof(long)); 
	if (!naffinity)
		err_exit("could not allocate memory!");

	// get online cpu's
	if (0 < getkernvar(set->cpusystemfileprefix, "online", constr, sizeof(constr))) {
		con = numa_parse_cpustring_all(constr);
		// mask affinity and invert for system map / readout of smi of online CPUs
		for (int i=0;i<maxccpu;i++) {

			/* ---------------------------------------------------------*/
			/* Configure online cpus - HT, SMT and performance settings */
			/* ---------------------------------------------------------*/
			if (numa_bitmask_isbitset(con, i)){ // filter by online/existing

				char fstring[50]; 	// cpu string

				{ // start block governor
					char poss[50]; 		// possible settings string for governors

					// verify if CPU-freq is on performance -> set it
					(void)sprintf(fstring, "cpu%d/cpufreq/scaling_available_governors", i);
					if (!(set->blindrun) && (0 < getkernvar(set->cpusystemfileprefix, fstring, poss, sizeof(poss)))){
						// value possible read ok
						(void)sprintf(fstring, "cpu%d/cpufreq/scaling_governor", i);
						if (0 < getkernvar(set->cpusystemfileprefix, fstring, str, sizeof(str))){
							// value act read ok
							if (strcmp(str, CPUGOVR)) {
								// Governor is set to a different value
								cont("Possible CPU-freq scaling governors \"%s\" on CPU%d.", poss, i);

								if (set->dryrun)
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
				if (!(set->blindrun) && (0 < getkernvar(set->cpusystemfileprefix, fstring, str, sizeof(str)))){
					// value act read ok
					if (strcmp(str, "n/a")) {
						//
						cont("Setting for power-QoS now \"%s\" on CPU%d.", str, i);

						if (set->dryrun)
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

				// invert affinity for available cpus only -> for system
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
	}
	// TODO else

	// parse to string	
	if (parse_bitmask (naffinity, cpus))
		err_exit ("can not determine inverse affinity mask!");

	/// --------------------
	/// verify executable permissions - needed for dry run, not blind run
	int capIpc = 1;
	{
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
	}

	v=0;
	if (cap_get_flag(cap, CAP_SYS_RESOURCE, CAP_EFFECTIVE, &v)) // check for effective RESOURCE cap
		err_exit_n(errno, "Capability test failed");

	if (!CAP_IS_SUPPORTED(CAP_SYS_RESOURCE) || (0==v)){
		if (!(set->blindrun))
			err_exit("CAP_SYS_RESOURCE capability mandatory to operate properly!");
		else
			warn("CAP_SYS_RESOURCE unavailable");
	}

	v=0;
	if (cap_get_flag(cap, CAP_IPC_LOCK, CAP_EFFECTIVE, &v)) // check for effective RESOURCE cap
		err_exit_n(errno, "Capability test failed");

	if (!CAP_IS_SUPPORTED(CAP_IPC_LOCK) || (0==v)){
		warn("CAP_IPC_LOCK unavailable.");
		capIpc = 0; // tell unavailable fort IPC lock
	}

	}

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

	if (SCHED_RR == set->policy && 0 < set->rrtime) { //TODO: rrtime always?
		cont( "Set round robin interval to %dms..", set->rrtime);
		(void)sprintf(str, "%d", set->rrtime);
		if (0 > setkernvar(set->procfileprefix, "sched_rr_timeslice_ms", str, set->dryrun)){
			warn("RR time slice not changed!");
		}
	}

	// here.. off-line messes up cset
	cont("moving kernel thread affinity");
	// kernel interrupt threads affinity
	setPidMask("\\B\\[ehca_comp[/][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[irq[/][[:digit:]]*-[[:alnum:]]*", naffinity, cpus);
	setPidMask("\\B\\[kcmtpd_ctr[_][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[rcuop[/][[:digit:]]*", naffinity, cpus);
	setPidMask("\\B\\[rcuos[/][[:digit:]]*", naffinity, cpus);

	// ksoftirqd -> offline, online again
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
	// TODO: detect possible cpu alignment cat /proc/$$/cpuset, maybe change it once all is done
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
	char * numastr = "0"; // default NUMA string
	if (-1 != numa_available()) {
		int numanodes = numa_max_node();
		if (!(numastr = malloc (5)))
			err_exit("could not allocate memory!");

		sprintf(numastr, "0-%d", numanodes);
	}
	else
		warn("NUMA not enabled, defaulting to memory node '0'");

	/// --------------------
	/// CGroup present, fix cpu-sets of running containers
	if (AFFINITY_USEALL != set->setaffinity){ // TODO: useall = ignore setting of exclusive

		cont( "reassigning Docker's CGroups CPU's to %s exclusively", set->affinity);

		DIR *d;
		struct dirent *dir;
		d = opendir(set->cpusetdfileprefix);// -> pointing to global
		if (d) {

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
								warn("Can not remove cpu exclusive : %s", strerror(errno));
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
				warn("Can not remove cpu exclusive : %s", strerror(errno));
			}
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", constr, set->dryrun)){
				// global reset failed, try affinity only
				if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity, set->dryrun)){
					warn("Can not reset cpu-affinity. Expect malfunction!"); // set online cpus as default
				}
			}
			/* if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.mems", numastr, set->dryrun)){
				warn("Can not set NUMA memory nodes");// TODO: separte NUMA settings
			}*/

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
								warn("Can not set NUMA memory nodes"); // TODO: separte numa settings
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
				warn("Can not set NUMA memory nodes");// TODO: separte numa settings
			}
			if (0 > setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "1", set->dryrun)){
				warn("Can not set cpu exclusive");
			}

			closedir(d);
		}
	}

	//------- CREATE CGROUPs FOR CONFIGURED CONTAINER ids ------------
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
					warn("Can not set cgroup: %s", strerror(errno));
					continue;
				}

				if (0 > setkernvar(fileprefix, "/cpuset.cpus", set->affinity, set->dryrun)){
					warn("Can not set cpu-affinity");
				}
				if (0 > setkernvar(fileprefix, "/cpuset.mems", numastr, set->dryrun)){
					warn("Can not set numa memory nodes"); // TODO: separte numa settings
				}
			}
			else //realloc issues
				err_exit("could not allocate memory!\n");
		}
	}


	//------- CREATE NEW CGROUP AND MOVE ALL ROOT TASKS TO IT ------------
	// system CGroup, possible tasks are moved -> do for all

	char *fileprefix = NULL;

	cont("creating CGroup for system on %s", cpus);

	// TODO: system directory is hard-coded -> at least use MACRO
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
			warn("Can not set cpu-affinity");
		}
		if (0 > setkernvar(fileprefix, "cpuset.mems", numastr, set->dryrun)){
			warn("Can not set numa memory nodes");
		}
		if (0 > setkernvar(fileprefix, "cpuset.cpu_exclusive", "1", set->dryrun)){
			warn("Can not set cpu exclusive");
		}

		cont( "moving tasks..");

		if ((nfileprefix=malloc(strlen(set->cpusetfileprefix)+strlen("tasks")+1))) {
			nfileprefix[0] = '\0';   // ensures the memory is an empty string
			// copy to new prefix
			nfileprefix = strcat(strcat(nfileprefix,set->cpusetfileprefix),"tasks");

			int mtask = 0,
				mtask_old;
			do
			{
				// update counters, start again
				mtask_old = mtask;
				mtask = 0;

				char pidline[BUFRD];
				char *pid, *pid_ptr;
				int nleft=0; // reading left counter
				// prepare literal and open pipe request
				int path = open(nfileprefix,O_RDONLY);

				// Scan through string and put in array, leave one byte extra, needed for strtok to work
				while(nleft += read(path, pidline+nleft,BUFRD-nleft-1)) { 	// TODO: read vs fread
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
				
				// some unmoveable tasks?, one free try
				if (mtask_old != mtask && mtask_old == 0)
				{
					warn("Could not move %d tasks", mtask);					
					cont("retry..");
					sleep(5);
				}
			}
			while (mtask_old != mtask);
		}

sysend: // jumped here if not possible to create system

		// free string buffers
		free (fileprefix);
		free (nfileprefix);

	}
	else //re-alloc issues
		err_exit("could not allocate memory!\n");

	// composed static or generated NUMA string? if generated > 1
	if (1 < strlen(numastr))
		free(numastr);

	/* lock all memory (prevent swapping) -- do here */
	if (set->lock_pages) {
		// find lock configuration - depending on CAPS
		int lockt = MCL_CURRENT;
		if (capIpc)
			lockt |= MCL_FUTURE;
		if (-1 == mlockall(lockt))
			err_exit_n(errno, "MLockall failed");\
	}
}

/// display_help(): Print usage information 
///
/// Arguments: exit with error?
///
/// Return value: - 
static void display_help(int error)
{
	(void)
	printf("Usage:\n"
	       "orchestrator <options> [config.json]\n\n"
	       "-a [NUM] --affinity        run container threads on specified cpu range,\n"
           "                           colon separated list\n"
	       "                           run system threads on remaining inverse mask list.\n"
		   "                           default: System=0, Containers=1-MAX_CPU\n"
	       "-b       --bind            bind non-RT PIDs of container to same affinity\n"
	       "-c CLOCK --clock=CLOCK     select clock for measurement statistics\n"
	       "                           0 = CLOCK_MONOTONIC (default)\n"
	       "                           1 = CLOCK_REALTIME\n"
	       "                           2 = CLOCK_PROCESS_CPUTIME_ID\n"
	       "                           3 = CLOCK_THREAD_CPUTIME_ID\n"
	       "-C [CGRP]                  use CGRP Docker directory to identify containers\n"
	       "                           optional CGRP parameter specifies base signature,\n"
           "                           default=%s\n"
	       "-d       --dflag           set deadline overrun flag for dl PIDs\n"
		   "-D                         dry run: suppress system changes/test only\n"
	       "-f                         force execution with critical parameters\n"
//	       "-F       --fifo=<path>     create a named pipe at path and write stats to it\n"
	       "-i INTV  --interval=INTV   base interval of update thread in us default=%d\n"
	       "-k                         keep track of ended PIDs\n"
	       "-l LOOPS --loops=LOOPS     number of loops for container check: default=%d\n"
	       "-m       --mlockall        lock current and future memory allocations\n"
	       "-n [CMD]                   use CMD signature on PID to identify containers\n"
	       "                           optional CMD parameter specifies base signature,\n"
           "                           default=%s\n"
	       "-p PRIO  --priority=PRIO   priority of the measurement thread:default=0\n"
	       "         --policy=NAME     policy of measurement thread, where NAME may be one\n"
	       "                           of: other, normal, batch, idle, deadline, fifo or rr.\n"
	       "-P                         with option -n, scan for children threads\n"
	       "-q       --quiet           print a summary only on exit\n"
	       "-r RTIME --runtime=RTIME   set a maximum runtime in seconds, default=0(infinite)\n"
	       "         --rr=RRTIME       set a SCHED_RR interval time in ms, default=100\n"
	       "-s [CMD]                   use shim PPID container detection.\n"
	       "                           optional CMD parameter specifies ppid command\n"
#ifdef ARCH_HAS_SMI_COUNTER
               "         --smi             Enable SMI counting\n"
#endif
//	       "-t NUM   --threads=NUM     number of threads for resource management\n"
//	       "                           default = 1 (not changeable for now)\n"
	       "-u       --unbuffered      force unbuffered output for live processing (FIFO)\n"
#ifdef NUMA
//	       "-U       --numa            force numa distribution of memory nodes, RR\n"
#endif
#ifdef DEBUG
	       "-v       --verbose         verbose output for debug purposes\n"
#endif
	       "-w       --wcet=TIME       WCET runtime for deadline policy in us, default=%d\n"
			, CONT_DCKR, TSCAN, TDETM, CONT_PID, TWCET
		);
	if (error)
		exit(EXIT_FAILURE);

	// For --help query only

	(void)
	printf("Report bugs to: info@florianhofer.it\n"
	       "Project home page: <https://www.github.com/flhofer/real-time-containers/>\n");

	exit(EXIT_SUCCESS);
}

enum option_values {
	OPT_AFFINITY=1, OPT_BIND, OPT_CLOCK, OPT_DFLAG,
	OPT_FIFO, OPT_INTERVAL, OPT_LOOPS, OPT_MLOCKALL,
	OPT_NSECS, OPT_NUMA, OPT_PRIORITY, OPT_QUIET, 
	OPT_RRTIME, OPT_THREADS, OPT_UNBUFFERED, OPT_RTIME, 
	OPT_SMI, OPT_VERBOSE, OPT_WCET, OPT_POLICY, 
	OPT_HELP, OPT_VERSION
};

/// process_options(): Process commandline options 
///
/// Arguments: - structure with parameter set
///			   - passed command line variables
///			   - number of cpus
///
/// Return value: -
static void process_options (prgset_t *set, int argc, char *argv[], int max_cpus)
{
	int error = 0;
	int option_index = 0;
	int optargs = 0;
#ifdef DEBUG
	dbg_out = fopen("/dev/null", "w");
	int verbose = 0;
#endif

	// preset configuration default values
	parse_config_set_default(set);

	for (;;) {
		//option_index = 0;
		/*
		 * Options for getopt
		 * Ordered alphabetically by single letter name
		 */
		static struct option long_options[] = {
			{"affinity",         required_argument, NULL, OPT_AFFINITY},
			{"bind",     		 no_argument,       NULL, OPT_BIND },
			{"clock",            required_argument, NULL, OPT_CLOCK },
			{"dflag",            no_argument,		NULL, OPT_DFLAG },
			{"fifo",             required_argument, NULL, OPT_FIFO },
			{"interval",         required_argument, NULL, OPT_INTERVAL },
			{"loops",            required_argument, NULL, OPT_LOOPS },
			{"mlockall",         no_argument,       NULL, OPT_MLOCKALL },
			{"priority",         required_argument, NULL, OPT_PRIORITY },
			{"quiet",            no_argument,       NULL, OPT_QUIET },
			{"runtime",          required_argument, NULL, OPT_RTIME },
			{"rr",               required_argument, NULL, OPT_RRTIME },
			{"threads",          required_argument, NULL, OPT_THREADS },
			{"unbuffered",       no_argument,       NULL, OPT_UNBUFFERED },
			{"numa",             no_argument,       NULL, OPT_NUMA },
			{"smi",              no_argument,       NULL, OPT_SMI },
			{"version",			 no_argument,		NULL, OPT_VERSION},
			{"verbose",          no_argument,       NULL, OPT_VERBOSE },
			{"policy",           required_argument, NULL, OPT_POLICY },
			{"wcet",             required_argument, NULL, OPT_WCET },
			{"help",             no_argument,       NULL, OPT_HELP },
			{NULL, 0, NULL, 0}
		};
		int c = getopt_long(argc, argv, "a:bBc:C:dDfFhi:kl:mn::p:Pqr:s::t:uUvw:",
				    long_options, &option_index);
		if (-1 == c)
			break;
		switch (c) {
		case 'a':
		case OPT_AFFINITY:
//			if (numa)
//				break;
			if (NULL != optarg) {
				set->affinity = optarg;
				set->setaffinity = AFFINITY_SPECIFIED;
			} else if (optind<argc && atoi(argv[optind])) {
				set->affinity = argv[optind];
				optargs++;
				set->setaffinity = AFFINITY_SPECIFIED;
			} else {
				set->affinity = malloc(10);
				if (!set->affinity){
					err_msg("could not allocate memory!");
					exit(EXIT_FAILURE);
				}
				sprintf(set->affinity, "0-%d", max_cpus-1);
				set->setaffinity = AFFINITY_USEALL;
			}
			break;
		case 'b':
		case OPT_BIND:
			set->affother = 1; break;
		case 'B':
			set->blindrun = 1; break;
		case 'c':
		case OPT_CLOCK:
			set->clocksel = atoi(optarg); break;
		case 'C':
			set->use_cgroup = DM_CGRP;
			if (NULL != optarg) {
				free(set->cont_cgrp);
				set->cont_cgrp = optarg;
			} else if (optind<argc) {
				free(set->cont_cgrp);
				set->cont_cgrp = argv[optind];
				optargs++;
			}

			/// -------------------- DOCKER & CGROUP CONFIGURATION
			// create Docker CGroup prefix
			set->cpusetdfileprefix = realloc(set->cpusetdfileprefix, strlen(set->cpusetfileprefix) + strlen(set->cont_cgrp)+1);
			if (!set->cpusetdfileprefix)
				err_exit("could not allocate memory!");

			*set->cpusetdfileprefix = '\0'; // set first chat to null
			set->cpusetdfileprefix = strcat(strcat(set->cpusetdfileprefix, set->cpusetfileprefix), set->cont_cgrp);		

			break;
		case 'd':
		case OPT_DFLAG:
			set->setdflag = 1; break;
		case 'D':
			set->dryrun = 1; break;
		case 'f':
			set->force = 1; break;
		case 'F':
		case OPT_FIFO:
			set->use_fifo = 1;
			//TODO: strncpy(fifopath, optarg, strlen(optarg));
			break;
		case 'i':
		case OPT_INTERVAL:
			set->interval = atoi(optarg); break;
		case 'k':
			set->trackpids = 1; break;
		case 'l':
		case OPT_LOOPS:
			set->loops = atoi(optarg); break;
		case 'm':
		case OPT_MLOCKALL:
			set->lock_pages = 1; break;
		case 'n':
			set->use_cgroup = DM_CMDLINE;
			if (NULL != optarg) {
				free(set->cont_pidc);
				set->cont_pidc = optarg;
			} else if (optind<argc) {
				free(set->cont_pidc);
				set->cont_pidc = argv[optind];
				optargs++;
			}
			break;
		case 'p':
		case OPT_PRIORITY:
			set->priority = atoi(optarg);
			if (SCHED_FIFO != set->policy && SCHED_RR != set->policy) {
				warn(" policy and priority don't match: setting policy to SCHED_FIFO");
				set->policy = SCHED_FIFO;
}
			break;
		case 'P':
			set->psigscan = 1; break;
		case 'q':
		case OPT_QUIET:
			set->quiet = 1; break;
		case 'r':
		case OPT_RTIME:
			if (NULL != optarg) {
				set->runtime = atoi(optarg);
			} else if (optind<argc && atoi(argv[optind])) {
				set->runtime = atoi(argv[optind]);
				optargs++;
			}
			break;
		case OPT_RRTIME:
			if (NULL != optarg) {
				set->rrtime = atoi(optarg);
			} else if (optind<argc && atoi(argv[optind])) {
				set->rrtime = atoi(argv[optind]);
				optargs++;
			}
			break;
		case 's':
			set->use_cgroup = DM_CNTPID;
			if (NULL != optarg) {
				free(set->cont_ppidc);
				set->cont_ppidc = optarg;
			} else if (optind<argc) {
				free(set->cont_ppidc);
				set->cont_ppidc = argv[optind];
				optargs++;
			}
			break;
		case 'u':
		case OPT_UNBUFFERED:
			setvbuf(stdout, NULL, _IONBF, 0); break;
#ifdef DEBUG
		case 'v':
		case OPT_VERBOSE: 
			verbose = 1; 
			break;
#endif
		case OPT_VERSION:
			(void)printf("Source compilation date: %s\n", __DATE__);
			(void)printf("Copyright (C) 2019 Siemens Corporate Technologies, Inc.\n"
						 "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n"
						 "This is free software: you are free to change and redistribute it.\n"
						 "There is NO WARRANTY, to the extent permitted by law.\n");
			exit(EXIT_SUCCESS);
		case 'w':
		case OPT_WCET:
			set->update_wcet = atoi(optarg); break;
		case 'h':
		case OPT_HELP:
			display_help(0); break;
		case OPT_POLICY:
			if (string_to_policy(optarg, &set->policy) == 0)
				err_exit("Invalid policy %s", optarg);

			break;
		case OPT_SMI:
#ifdef ARCH_HAS_SMI_COUNTER
			set->smi = 1;
#else
			err_exit("--smi is not available on your arch");
#endif
			break;
		}
	}

#ifdef DEBUG
	if (verbose) {
		fclose(dbg_out);
		dbg_out = stderr;
	}
#endif

	// look for filename after options, we process only first
	if (optind+optargs < argc)
	{
	    config = argv[argc-1];
	}

	// always verify for configuration file -> segmentation fault??
	if ( access( config, F_OK )) {
		err_msg("configuration file '%s' not found", config);
		error = 1;
	}

	// create parameter structure
	if (!(contparm = malloc (sizeof(containers_t))))
		err_exit("Unable to allocate memory");

	if (!error)
		// parse json configuration
		parse_config_file(config, set, contparm);

	if (set->smi) { // TODO: verify this statements, I just put them all
		if (set->setaffinity == AFFINITY_UNSPECIFIED)
			err_exit("SMI counter relies on thread affinity");

		if (!has_smi_counter())
			err_exit("SMI counter is not supported "
			      "on this processor");
	}

	// check clock sel boundaries
	if (0 > set->clocksel || 3 < set->clocksel)
		error = 1;

	// check priority boundary
	if (0 > set->priority || 99 < set->priority)
		error = 1;

	// check detection mode and policy -> deadline does not allow fork!
	if (SCHED_DEADLINE == set->policy && (DM_CNTPID == set->use_cgroup || DM_CMDLINE == set->use_cgroup)) {
		warn("can not use SCHED_DEADLINE with PID detection modes: setting policy to SCHED_FIFO");
		set->policy = SCHED_FIFO;	
	}

	// deadline and high refresh might starve the system. require force
	if (SCHED_DEADLINE == set->policy && set->interval < 1000 && !set->force) {
		warn("Using SCHED_DEADLINE with such low intervals can starve a system. Use force (-f) to start anyway.");
		error = 1;
	}

	// check priority and policy match
	if (set->priority && (SCHED_FIFO != set->policy && SCHED_RR != set->policy)) {
		warn("policy and priority don't match: setting policy to SCHED_FIFO");
		set->policy = SCHED_FIFO;
	}

	// check policy with priority match 
	if ((SCHED_FIFO == set->policy || SCHED_RR == set->policy) && 0 == set->priority) {
		warn("defaulting real-time priority to %d", 10);
		set->priority = 10;
	}

	// error present? print help message and exit
	if (error) {
		display_help(1);
	}
}

/// main(): main program.. setup threads and keep loop for user/system break
///
/// Arguments: - Argument values not defined yet
///
/// Return value: Exit code - 0 for no error - EXIT_SUCCESS
int main(int argc, char **argv)
{
	int max_cpus = sysconf(_SC_NPROCESSORS_ONLN);

	(void)printf("%s V %s\n", PRGNAME, VERSION);
	(void)printf("This software comes with no warranty. Please be careful\n");

	prgset_t *tmpset;
	if (!(tmpset = malloc (sizeof(prgset_t))))
		err_exit("Unable to allocate memory");

	process_options(tmpset, argc, argv, max_cpus);

	// gather actual information at startup, prepare environment
	prepareEnvironment(tmpset);

	prgset = tmpset; // move to make write protected

	pthread_t thread1, thread2;
	int32_t t_stat1 = 0; // we control thread status 32bit to be sure read is atomic on 32 bit -> sm on treads
	int32_t t_stat2 = 0; 
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */ 
	if ((iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &t_stat1))) {
		err_msg_n (iret1, "could not start update thread");
		t_stat1 = -1;
	}
	if ((iret2 = pthread_create( &thread2, NULL, thread_update, (void*) &t_stat2))) {
		err_msg_n (iret2, "could not start management thread");
		t_stat2 = -1;
	}

	/* lock all memory (prevent swapping) -- do here
	if (prgset->lock_pages)
		if (-1 == mlockall(MCL_CURRENT|MCL_FUTURE)) // future avoids page creation
			err_exit_n(errno, "MLockall failed"); */

	// set interrupt sig hand
	// TODO: change to sigaction, mask unused signals
	signal(SIGINT, inthand); // CTRL+C
	signal(SIGTERM, inthand); // KILL termination or end of test
	signal(SIGUSR1, inthand); // USR1 signal, not handled yet TODO

	while (!stop && (t_stat1 > -1 || t_stat2 > -1)) {
		sleep (1);
	}

	// signal shutdown to threads
	if (-1 < t_stat1) // only if not already done internally
		t_stat1 = -1;
	if (-1 < t_stat2) // only if not already done internally 
		t_stat2 = -1;

	// wait until threads have stopped
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thread1, NULL);
	if (!iret2)	// thread started successfully
		iret2 = pthread_join( thread2, NULL); 

    info("exiting safely");

	if(prgset->smi) {

		unsigned long smi_old;
		int maxccpu = numa_num_configured_cpus();
		info("SMI counters for the CPUs");
		// mask affinity and invert for system map / readout of smi of online CPUs
		for (int i=0;i<maxccpu;i++) 
			// if smi is set, read SMI counter
			if (*(smi_msr_fd+i)) {
				/* get current smi count to use as base value */
				if (get_smi_counter(*smi_msr_fd+i, &smi_old))
					err_exit_n( errno, "Could not read SMI counter");
				cont("CPU%d: %ld", i, smi_old-*(smi_counter+i));
			}
	}

	/* unlock everything */
	if (prgset->lock_pages)
		munlockall();

    return 0;
}
