// main settings and header file
#include "schedstat.h" 

// header files of launched threads
#include "update.h"
#include "manage.h"

// header file of configuration parser
#include "parse_config.h"

// Things that should be needed only here
#include <sys/mman.h>		// mlock
#include <numa.h>			// numa node ident
#include <getopt.h>			// command line parsing
#include <sys/stat.h>		// directory and fs stat
#include <sys/capability.h>	// cap exploration
#include <sys/sysinfo.h>	// system general information

static void display_help(int); // declaration for compat

/* --------------------------- Global variables for all the threads and programms ------------------ */

const parm_t * contparm; // read only container parameter settings
prgset_t * prgset; // read only programm setings structure

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

// TODO:  implement fifo thread as in cycictest for readout
//static pthread_t fifo_threadid;
//static char fifopath[MAX_PATH];

static unsigned long * smi_counter = NULL; // points to the list of SMI-counters
static int * smi_msr_fd = NULL; // points to file descriptors for MSR readout

/// prepareEnvironment(): gets the list of active pids at startup, sets up
/// a CPU-shield if not present, prepares kernel settings for DL operation
/// and populates initial state of pid list
///
/// Arguments: - structure with parameter set
///
/// Return value: error code if present
///
static void prepareEnvironment(prgset_t *set) {

	/// --------------------
	/// verify 	cpu topology and distribution
	int maxcpu = get_nprocs();	
	int maxccpu = numa_num_configured_cpus(); //get_nprocs_conf();	

	char cpus[10]; // cpu allocation string
	char str[100]; // generic string... 

	info("This system has %d processors configured and "
        "%d processors available.\n",
        maxccpu, maxcpu);

	if (numa_available())
		err_exit( "NUMA is not available but mandatory for the orchestration\n");		

	// verify if SMT is disabled -> now force = disable, TODO: may change to disable only concerned cores
	if (!getkernvar(set->cpusystemfileprefix, "smt/control", str, sizeof(str))){
		// value read ok
		if (!strcmp(str, "on")) {
			// SMT - HT is on
			if (!set->force) 
				err_exit("SMT is enabled. Set -f (force) flag to authorize disabling\n");

			if (setkernvar(set->cpusystemfileprefix, "smt/control", "off"))
				err_exit("SMT is enabled. Disabling was unsuccessful!\n");

			cont("SMT is now disabled, as required. Refresh configurations..\n");
			sleep(1); // leave time to refresh conf buffers -> immediate query fails
			maxcpu = get_nprocs();	// update
		}
		else
			cont("SMT is disabled, as required\n");
	}

	// no mask specified, generate default
	if (AFFINITY_UNSPECIFIED == set->setaffinity){
		if (!(set->affinity = malloc(10))) // has never been set
			err_exit("could not allocate memory!\n");

		sprintf(set->affinity, "%d-%d", SYSCPUS+1, maxcpu-1);
		info("using default setting, affinity '%d' and '%s'.\n", SYSCPUS, set->affinity);
	}
	// prepare bitmask, no need to do it before
	set->affinity_mask = parse_cpumask(set->affinity, maxccpu);
	if (!set->affinity_mask)
		display_help(1);

	smi_counter = calloc (sizeof(long), maxccpu);
	smi_msr_fd = calloc (sizeof(int), maxccpu);
	if (!smi_counter || !smi_msr_fd)
		err_exit("could not allocate memory!\n");

	struct bitmask * con;
	struct bitmask * naffinity = numa_bitmask_alloc((maxccpu/sizeof(long)+1)*sizeof(long)); 
	if (!naffinity)
		err_exit("could not allocate memory!\n");

	// get online cpu's
	if (!getkernvar(set->cpusystemfileprefix, "online", str, sizeof(str))) {
		con = numa_parse_cpustring_all(str);
		// mask affinity and invert for system map / readout of smi of online CPUs
		for (int i=0;i<maxccpu;i++) {

			if (numa_bitmask_isbitset(con, i)){ // filter by online/existing

				char fstring[50]; // cpu string
				char poss[50]; // cpu string

				// verify if cpu-freq is on performance -> set it
				(void)sprintf(fstring, "cpu%d/cpufreq/scaling_available_governors", i);
				if (!getkernvar(set->cpusystemfileprefix, fstring, poss, sizeof(poss))){
					// value possible read ok
					(void)sprintf(fstring, "cpu%d/cpufreq/scaling_governor", i);
					if (!getkernvar(set->cpusystemfileprefix, fstring, str, sizeof(str))){
						// value act read ok
						if (strcmp(str, CPUGOVR)) {
							// SMT - HT is on
							cont("Possible CPU-freq scaling governors \"%s\" on CPU%d.\n", poss, i);
							if (!set->force)
								err_exit("CPU-freq is set to \"%s\" on CPU%d. Set -f (focre) flag to authorize change to \"" CPUGOVR "\"\n", str, i);

							if (setkernvar(set->cpusystemfileprefix, fstring, CPUGOVR))
								err_exit("CPU-freq change unsuccessful!\n");

							cont("CPU-freq on CPU%d is now set to \"" CPUGOVR "\" as required\n", i);
						}
						else
							cont("CPU-freq on CPU%d is set to \"" CPUGOVR "\" as required\n", i);
					}
				}

				// TODO: cpu-idle

				// if smi is set, read SMI counter
				if(set->smi) {
					*(smi_msr_fd+i) = open_msr_file(i);
					if (*(smi_msr_fd+i) < 0)
						err_exit("Could not open MSR interface, errno: %d\n",
							errno);
					// get current smi count to use as base value 
					if (get_smi_counter(*smi_msr_fd+i, smi_counter+i))
						err_exit("Could not read SMI counter, errno: %d\n",
							0, errno);
				}

				// invert affinity for avaliable cpus only -> for system
				if (!numa_bitmask_isbitset(set->affinity_mask, i))
					numa_bitmask_setbit(naffinity, i);
			}

			// if CPU not online
			else if (numa_bitmask_isbitset(set->affinity_mask, i))
				// disabled processor set to affinity
				err_exit("Unavailable CPU set for affinity.\n");

		}
	}

	// parse to string	
	if (parse_bitmask (naffinity, cpus))
		err_exit ("can not determine inverse affinity mask!\n");

	/// --------------------
	/// verify executable permissions	
	{
	info( "Verifying for process capabilities..\n");
	cap_t cap = cap_get_proc(); // get capability map of proc
	if (!cap) 
		err_exit_n(errno, "Can not get capability map");

	cap_flag_value_t v = 0; // flag to store return value
	if (cap_get_flag(cap, CAP_SYS_NICE, CAP_EFFECTIVE, &v)) // check for effective NICE cap
		err_exit_n(errno, "Capability test failed");

	if (!CAP_IS_SUPPORTED(CAP_SYS_NICE) || (0==v))
		err_exit("CAP_SYS_NICE capability mandatory to operate properly!\n");

	v=0;
	if (cap_get_flag(cap, CAP_SYS_RESOURCE, CAP_EFFECTIVE, &v)) // check for effective RESOURCE cap
		err_exit_n(errno, "Capability test failed");

	if (!CAP_IS_SUPPORTED(CAP_SYS_RESOURCE) || (0==v))
		err_exit("CAP_SYS_RESOURCE capability mandatory to operate properly!\n");
	}

	/// --------------------
	/// Kernel variables, disable bandwidth management and RT-throttle
	/// Kernel RT-bandwidth management must be disabled to allow deadline+affinity
	set->kernelversion = check_kernel();

	if (KV_NOT_SUPPORTED == set->kernelversion)
		warn("Running on unknown kernel version...YMMV\nTrying generic configuration..\n");

	cont( "Set realtime bandwith limit to (unconstrained)..\n");
	// disable bandwidth control and realtime throttle
	if (setkernvar(set->procfileprefix, "sched_rt_runtime_us", "-1")){
		warn("RT-throttle still enabled. Limitations apply.\n");
	}

	if (SCHED_RR == set->policy && 0 < set->rrtime) {
		cont( "Set round robin interval to %dms..\n", set->rrtime);
		(void)sprintf(str, "%d", set->rrtime);
		if (setkernvar(set->procfileprefix, "sched_rr_timeslice_ms", str)){
			warn("RR timeslice not changed!\n");
		}
	}

	/// --------------------
	/// running settings for scheduler
	struct sched_attr attr; 
	pid_t mpid = getpid();
	if (sched_getattr (mpid, &attr, sizeof(attr), 0U))
		warn("could not read orchestrator attributes: %s\n", strerror(errno));
	else {
		cont( "orchestrator scheduled as '%s'\n", policy_to_string(attr.sched_policy));

		// set new attributes here, avoid Realtime for this thread
		attr.sched_nice = 20;
		// reset on fork.. and if DL is set, grub and overrun
		attr.sched_flags |= SCHED_FLAG_RESET_ON_FORK;

		cont( "promoting process and setting attributes..\n");
		if (sched_setattr (mpid, &attr, 0U))
			warn("could not set orchestrator schedulig attributes, %s\n", strerror(errno));
	}

	if (AFFINITY_USEALL != set->setaffinity){ // set affinity only if not useall
		if (numa_sched_setaffinity(mpid, naffinity))
			warn("could not set orchestrator affinity: %s\n", strerror(errno));
		else
			cont("Orchestrator's PID reassigned to CPU's %s\n", cpus);
	}

	/// --------------------
	/// Docker CGROUP setup - detection if present

	{ // start environment detection CGroup
	struct stat s;

	int err = stat(set->cpusetdfileprefix, &s);
	if(-1 == err) {
		// Docker Cgroup not found, set->force enabled = try creating
		if(ENOENT == errno && set->force) {
			warn("CGroup '%s' does not exist. Is the daemon running?\n", set->cont_cgrp);
			if (0 != mkdir(set->cpusetdfileprefix, ACCESSPERMS))
				err_exit_n(errno, "Can not create container group");
			// if it worked, stay in Container mode	
		} else {
		// Docker Cgroup not found, set->force not enabled = try switching to pid
			if(ENOENT == errno) 
				err_msg("No set->force. Can not create container group: %s\n", strerror(errno));
			else 
				err_exit_n(errno, "Stat encountered an error");
		}
	} else {
		// CGroup found, but is it a dir?
		if(!(S_ISDIR(s.st_mode))) 
			// exists but is no dir -> goto PID detection
			err_exit("CGroup '%s' does not exist. Is the daemon running?\n", set->cont_cgrp);
	}

	// Display message according to detection mode set
	switch (set->use_cgroup) {
		default:
			// all fine?
			if (!err) {
				cont( "container id will be used to set PID execution..\n");
				break;
			}
			// error occurred, check for sCHED_DEADLINE first-> stop!
			if (SCHED_DEADLINE == set->policy) 
				err_exit("SCHED_DEADLINE does not allow forking. Can not switch to PID modes!\n");

			// otherwise switch to container PID detection
			set->use_cgroup = DM_CNTPID;

		case DM_CNTPID:
			cont( "will use PIDs of '%s' to detect processes..\n", set->cont_ppidc);
			break;
		case DM_CMDLINE:
			cont( "will use PIDs of command signtaure '%s' to detect processes..\n", set->cont_pidc);
			break;
	}
	} // end environment detection CGroup

	/// --------------------
	/// detect numa configuration TODO: adapt for full support
	char * numastr = "0"; // default numa string
	if (-1 != numa_available()) {
		int numanodes = numa_max_node();
		if (!(numastr = calloc (5, 1)))
			err_exit("could not allocate memory!\n");

		sprintf(numastr, "0-%d", numanodes);
	}
	else
		warn("Numa not enabled, defaulting to memory node '0'\n");

	/// --------------------
	/// cgroup present, fix cpu-sets of running containers
	if (AFFINITY_USEALL != set->setaffinity){ // useall = ignore setting of exclusive

		cont( "reassigning Docker's CGroups CPU's to %s exclusively\n", set->affinity);

		DIR *d;
		struct dirent *dir;
		d = opendir(set->cpusetdfileprefix);// -> pointing to global
		if (d) {

			char *contp = NULL; // clear pointer
			/// Reassigning preexisting containers?
			while ((dir = readdir(d)) != NULL) {
			// scan trough docker cgroups, find them?
				if (strlen(dir->d_name)>60) {
					if ((contp=realloc(contp,strlen(set->cpusetdfileprefix)  // container strings are very long!
						+ strlen(dir->d_name)+1))) {
						contp[0] = '\0';   // ensures the memory is an empty string
						// copy to new prefix
						contp = strcat(strcat(contp,set->cpusetdfileprefix),dir->d_name);

						if (setkernvar(contp, "/cpuset.cpus", set->affinity)){
							warn("Can not set cpu-affinity\n");
						}
					}
					else // realloc error
						err_exit("could not allocate memory!\n");
				}	
			}
			free (contp);

			// Docker CGroup settings and affinity
			if (setkernvar(set->cpusetdfileprefix, "cpuset.cpus", set->affinity)){
				warn("Can not set cpu-affinity\n");
			}
			if (setkernvar(set->cpusetdfileprefix, "cpuset.mems", numastr)){
				warn("Can not set numa memory nodes\n");
			}
			if (setkernvar(set->cpusetdfileprefix, "cpuset.cpu_exclusive", "1")){
				warn("Can not set cpu exclusive\n");
			}

			closedir(d);
		}
	}

	//------- CREATE NEW CGROUP AND MOVE ALL ROOT TASKS TO IT ------------
	// system CGroup, possible tasks are moved -> do for all

	char *fileprefix = NULL;

	cont("creating cgroup for system on %s\n", cpus);

	if ((fileprefix=malloc(strlen(set->cpusetfileprefix)+strlen("system/")+1))) {
		char * nfileprefix = NULL;

		fileprefix[0] = '\0';   // ensures the memory is an empty string
		// copy to new prefix
		fileprefix = strcat(strcat(fileprefix,set->cpusetfileprefix),"system/");
		// try to create directory
		if(0 != mkdir(fileprefix, ACCESSPERMS) && EEXIST != errno)
		{
			// error otherwise
			warn("Can not set cpu system group: %s\n", strerror(errno));
			goto sysend; // skip all system things 
			// FIXME: might create unexpected behaviour
		}

		if (setkernvar(fileprefix, "cpuset.cpus", cpus)){ 
			warn("Can not set cpu-affinity\n");
		}
		if (setkernvar(fileprefix, "cpuset.mems", numastr)){
			warn("Can not set numa memory nodes\n");
		}
		if (setkernvar(fileprefix, "cpuset.cpu_exclusive", "1")){
			warn("Can not set cpu exclusive\n");
		}

		cont( "moving tasks..\n");

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
				char *pid;
				int nleft=0; // reading left counter
				// prepare literal and open pipe request
				pidline[BUFRD-1] = '\0'; // safety to avoid overrun	
				int path = open(nfileprefix,O_RDONLY);

				// Scan through string and put in array, leave one byte extra, needed for strtok to work
				while(nleft += read(path, pidline+nleft,BUFRD-nleft-2)) {
					pidline[BUFRD-2] = '\n'; // end of read check, set\n to be sure to end strtok, not on \0
					printDbg("Pid string return %s\n", pidline);
					pid = strtok (pidline,"\n");	
					while (NULL != pid && nleft && ('\0' != pidline[BUFRD-2]))  { 

						// fileprefix still pointing to system/
						if (setkernvar(fileprefix, "tasks", pid)){
							printDbg( KMAG "Warn!" KNRM " Can not move task %s\n", pid);
							mtask++;
						}
						nleft-=strlen(pid)+1;
						pid = strtok (NULL,"\n");	
					}
					if (pid) // copy leftover chars to beginning of string buffer
						memcpy(pidline, pidline+BUFRD-nleft-2, nleft); 
				}

				close(path);
				
				// some unmoveable tasks?, one free try
				if (mtask_old != mtask && mtask_old == 0)
				{
					warn("Could not move %d tasks\n", mtask);					
					cont("retry..\n");
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
	else //realloc issues
		err_exit("could not allocate memory!\n");

	// composed static or generated numa string? if generated > 1
	if (1 < strlen(numastr))
		free(numastr);

	/* lock all memory (prevent swapping) */
	if (set->lock_pages)
		if (-1 == mlockall(MCL_CURRENT|MCL_FUTURE)) 
			err_exit_n(errno, "Mlockall failed");

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
	       "schedstat <options> [config.json]\n\n"
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
	int verbose = 0;
#endif

	// preset configuration default values
	config_set_default(set);

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
		int c = getopt_long(argc, argv, "a:bc:C:dDfFhi:kl:mn::p:Pqr:s::t:uUvw:",
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
					err_msg("could not allocate memory!\n");
					exit(EXIT_FAILURE);
				}
				sprintf(set->affinity, "0-%d", max_cpus-1);
				set->setaffinity = AFFINITY_USEALL;
			}
			break;
		case 'b':
		case OPT_BIND:
			set->affother = 1; break;
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
				err_exit("could not allocate memory!\n");

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
				set->cont_pidc = optarg;
			} else if (optind<argc) {
				set->cont_pidc = argv[optind];
				optargs++;
			}
			break;
		case 'p':
		case OPT_PRIORITY:
			set->priority = atoi(optarg);
			if (SCHED_FIFO != set->policy && SCHED_RR != set->policy) {
				warn(" policy and priority don't match: setting policy to SCHED_FIFO\n");
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
				set->cont_ppidc = optarg;
			} else if (optind<argc) {
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
			set->policy = string_to_policy(optarg); break;
		case OPT_SMI:
#ifdef ARCH_HAS_SMI_COUNTER
			set->smi = 1;
#else
			err_exit("--smi is not available on your arch\n");
#endif
			break;
		}
	}

	// look for filename after options, we process only first
	if (optind+optargs < argc)
	{
	    config = argv[argc-1];
	}

	// allways verify for config file -> segmentation fault??
	if ( access( config, F_OK )) {
		err_msg("configuration file '%s' not found\n", config);
		error = 1;
	}

	// create parameter structure
	parm_t *tmpparm;
	if (!(tmpparm = malloc (sizeof(parm_t))))
		err_exit("Unable to allocate memory");

	if (!error)
		// parse json configuration
		parse_config(strdup(config), set, tmpparm);

#ifdef DEBUG
	if (verbose)
		dbg_out = stderr;
	else
		dbg_out = fopen("/dev/null", "w");
#endif

	if (set->smi) { // TODO: verify this statements, I just put them all
		if (set->setaffinity == AFFINITY_UNSPECIFIED)
			err_exit("SMI counter relies on thread affinity\n");

		if (!has_smi_counter())
			err_exit("SMI counter is not supported "
			      "on this processor\n");
	}

	// check clock sel boundaries
	if (0 > set->clocksel || 3 < set->clocksel)
		error = 1;

	// check priority boundary
	if (0 > set->priority || 99 < set->priority)
		error = 1;

	// check detection mode and policy -> deadline does not allow fork!
	if (SCHED_DEADLINE == set->policy && (DM_CNTPID == set->use_cgroup || DM_CMDLINE == set->use_cgroup)) {
		warn("can not use SCHED_DEADLINE with PID detection modes: setting policy to SCHED_FIFO\n");
		set->policy = SCHED_FIFO;	
	}

	// deadline and high refresh might starve the system. require force
	if (SCHED_DEADLINE == set->policy && set->interval < 1000 && !set->force) {
		warn("Using SCHED_DEADLINE with such low intervals can starve a system. Use force (-f) to start anyway.\n");
		error = 1;
	}

	// check priority and policy match
	if (set->priority && (SCHED_FIFO != set->policy && SCHED_RR != set->policy)) {
		warn("policy and priority don't match: setting policy to SCHED_FIFO\n");
		set->policy = SCHED_FIFO;
	}

	// check policy with priority match 
	if ((SCHED_FIFO == set->policy || SCHED_RR == set->policy) && 0 == set->priority) {
		warn("defaulting realtime priority to %d\n", 10);
		set->priority = 10;
	}

	// error present? print help message and exit
	if (error) {
		display_help(1);
	}
}

/// main(): mein program.. setup threads and keep loop for user/system break
///
/// Arguments: - Argument values not defined yet
///
/// Return value: Exit code - 0 for no error - EXIT_SUCCESS
int main(int argc, char **argv)
{
	int max_cpus = sysconf(_SC_NPROCESSORS_ONLN);

	(void)printf("%s V %s\n", PRGNAME, VERSION);
	(void)printf("This software comes with no waranty. Please be careful\n");

	prgset_t *tmpset;
	parm_t *tmpparm;
	if (!(tmpset = malloc (sizeof(prgset_t))) || 
		!(tmpparm = malloc (sizeof(parm_t))))
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

	// set interrupt sig hand
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

    info("exiting safely\n");

	if(prgset->smi) {

		unsigned long smi_old;
		int maxccpu = numa_num_configured_cpus();
		info("SMI counters for the CPUs\n");
		// mask affinity and invert for system map / readout of smi of online CPUs
		for (int i=0;i<maxccpu;i++) 
			// if smi is set, read SMI counter
			if (*(smi_msr_fd+i)) {
				/* get current smi count to use as base value */
				if (get_smi_counter(*smi_msr_fd+i, &smi_old))
					err_exit_n( errno, "Could not read SMI counter");
				cont("CPU%d: %ld\n", i, smi_old-*(smi_counter+i));
			}
	}

	/* unlock everything */
	if (prgset->lock_pages)
		munlockall();

    return 0;
}
