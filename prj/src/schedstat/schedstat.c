// main settings and header file
#include "schedstat.h" 

// header files of launched threads
#include "update.h"
#include "manage.h"

// Things that should be needed only here
#include <sys/mman.h>		// mlock
#include <numa.h>			// numa node ident
#include <getopt.h>			// command line parsing
#include <sys/stat.h>		// directory and fs stat
#include <sys/utsname.h>	// kernel info
#include <sys/capability.h>	// cap exploration
#include <sys/sysinfo.h>	// system general information

// -------------- Global variables for all the threads and programms ------------------

// signal to keep status of triggers ext SIG
volatile sig_atomic_t stop;
// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;
// head of pidlist - PID runtime and configuration details
node_t * head = NULL;
// configuration of detection mode of containers
int use_cgroup = DM_CGRP; // identify processes via cgroup

int verbose = 0;
int kernelversion; // kernel version -> opts based on this
char * config = "config.json";
char * cont_ppidc = CONT_PPID;
char * cont_pidc = CONT_PID;

int priority=0;
int clocksel = 0;
int policy = SCHED_OTHER;	/* default policy if not specified */
int quiet = 0;
int affother = 0;
int setdflag = 0;
int interval = TSCAN;
int update_wcet = TWCET;
int loops = TDETM;
int runtime = 0;
int negiszero = 0;

static char *fileprefix; // Work variable for local things -> procfs & sysfs

/* -------------------------------------------- DECLARATION END ---- CODE BEGIN -------------------- */

/// inthand(): interrupt handler for infinite while loop, help 
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: 
void inthand ( int signum ) {
	stop = 1;
}


/// check_kernel(): check the kernel version,
///
/// Arguments: - 
///
/// Return value: - 
static int check_kernel(void)
{
	struct utsname kname;
	int maj, min, sub, kv;

	if (uname(&kname)) {
		err_msg(KRED "Error!" KNRM " uname failed: %s. Assuming not 2.6\n",
				strerror(errno));
		return KV_NOT_SUPPORTED;
	}
	sscanf(kname.release, "%d.%d.%d", &maj, &min, &sub);
	if (3 == maj) {
		if (14  >min)
			// EDF not implemented 
			kv = KV_NOT_SUPPORTED;
		else
		// kernel 3.x standard LT kernel for embedded
		kv = KV_314;

	} else if (4 == maj) { // fil

		// kernel 4.x introduces Deadline scheduling
		if (13 > min)
			// standard
			kv = KV_40;
		else if (16 > min)
			// full EDF
			kv = KV_413;
		else 
			// full EDF -PA
			kv = KV_416;
	} else if (5 == maj) { // fil
		// full EDF -PA, newest kernel
		kv = KV_50;
	} else
		kv = KV_NOT_SUPPORTED;

	return kv;
}

static int kernvar(int mode, const char *name, char *value, size_t sizeofvalue)
{
	char filename[128];
	int retval = 1;
	int path;
	size_t len_prefix = strlen(fileprefix), len_name = strlen(name);

	if (len_prefix + len_name + 1 > sizeof(filename)) {
		errno = ENOMEM;
		return 1;
	}

	memcpy(filename, fileprefix, len_prefix);
	memcpy(filename + len_prefix, name, len_name + 1);

	path = open(filename, mode);
	if (0 <= path) {
		if (O_RDONLY == mode) {
			int got;
			if ((got = read(path, value, sizeofvalue)) > 0) {
				retval = 0;
				value[got-1] = '\0';
			}
		} else if (O_WRONLY == mode) {
			if (write(path, value, sizeofvalue) == sizeofvalue)
				retval = 0;
		}
		close(path);
	}
	return retval;
}

static int setkernvar(const char *name, char *value)
{

	if (kernvar(O_WRONLY, name, value, strlen(value))){
		printDbg(KRED "Error!" KNRM " could not set %s to %s\n", name, value);
		return -1;
	}
	
	return 0;

}

// -------------- LOCAL variables for all the threads and programms ------------------

static int sys_cpus = 1;// TODO: separate orchestrator from system? // 0-> count reserved for orchestrator and system
static int lockall = 0;
static int numa = 0;

// TODO:  implement fifo thread as in cycictest for readout
static int use_fifo = 0;
//static pthread_t fifo_threadid;
static char fifopath[MAX_PATH];

static int num_threads = 1; // TODO: extend number of scan threads??
static int smp = 0;  // TODO: add special configurations for SMP modes

enum {
	AFFINITY_UNSPECIFIED,
	AFFINITY_SPECIFIED,
	AFFINITY_USEALL
};

static int setaffinity = AFFINITY_UNSPECIFIED;
static int affinity = SYSCPUS; // default split, 0-0 SYS, Syscpus to end rest

/// prepareEnvironment(): gets the list of active pids at startup, sets up
/// a CPU-shield if not present, prepares kernel settings for DL operation
/// and populates initial state of pid list
///
/// Arguments: 
///
/// Return value: error code if present
///
static int prepareEnvironment() {

	int maxcpu = get_nprocs();	
	int maxccpu = get_nprocs_conf();	

	/// prerequisites
	info("This system has %d processors configured and "
        "%d processors available.\n",
        maxccpu, maxcpu);
	
	if (maxccpu < maxcpu) {
		cont ("numbers differ, assuming Hyprerthreading disabled.");
	}

	/// --------------------
	/// verify executable permissions	
	// TODO: upgrade to libcap-ng
	info( "Verifying for process capabilities..\n");
	cap_t cap = cap_get_proc(); // get capability map of proc
	if (!cap) {
		err_msg( KRED "Error!" KNRM " Can not get capability map!\n");
		return errno;
	}
	cap_flag_value_t v = 0; // flag to store return value
	if (cap_get_flag(cap, CAP_SYS_NICE, CAP_EFFECTIVE, &v)) {// check for effective NICE cap
		err_msg( KRED "Error!" KNRM " Capability test failed!\n");
		return errno;
	}

	if (!CAP_IS_SUPPORTED(CAP_SYS_NICE) || (0==v)) {
		err_msg( KRED "Error!" KNRM " SYS_NICE capability mandatory to operate properly!\n");
		return -1;
	}

	/// --------------------
	/// Kernel variables, disable bandwidth management and RT-throttle
	/// Kernel RT-bandwidth management must be disabled to allow deadline+affinity
	kernelversion = check_kernel();
	fileprefix = procfileprefix; // set working prefix for vfs

	if (KV_NOT_SUPPORTED == kernelversion)
		warn("Running on unknown kernel version...YMMV\nTrying generic configuration..\n");

	cont( "Set realtime bandwith limit to (unconstrained)..\n");
	// disable bandwidth control and realtime throttle
	if (setkernvar("sched_rt_runtime_us", "-1")){
		warn("RT-throttle still enabled. Limitations apply.\n");
	}

	/// --------------------
	/// running settings for scheduler
	struct sched_attr attr; 
	pid_t mpid = getpid();
	if (sched_getattr (mpid, &attr, sizeof(attr), 0U))
		warn("could not read orchestrator attributes: %s\n", strerror(errno));
	else {
		cont( "orchestrator scheduled as '%s'\n", policy_to_string(attr.sched_policy));

		// TODO: set new attributes here

		cont( "promoting process and setting affinity..\n");
		if (sched_setattr (mpid, &attr, 0U))
			warn("could not set orchestrator schedulig attributes, %s\n", strerror(errno));
	}

	cpu_set_t cset;
	CPU_ZERO(&cset);
	CPU_SET(0, &cset); // set process to CPU zero

	if (sched_setaffinity(mpid, sizeof(cset), &cset ))
		warn("could not set orchestrator affinity: %s\n", strerror(errno));
	else
		cont("PID %d reassigned to CPU%d\n", mpid, 0);

	/// Docker CGROUP setup == TODO: verify cmd-line parameters cgroups
	if (DM_CGRP == use_cgroup) { // option enabled, test for it
		struct stat s;

		// no memory has been allocated yet
		fileprefix = cpusetdfileprefix; // set to docker directory

		int err = stat(fileprefix, &s);
		if(-1 == err) {
			if(ENOENT == errno) { // TODO: parametrizable Cgroup value
				warn("CGroup '%s' does not exist. Is the daemon running?\n", "docker/");
			} else {
				perror("Stat encountered an error");
			}
			// check for sCHED_DEADLINE first-> stop!
			if (SCHED_DEADLINE == policy) {
				err_msg( KRED "Error!" KNRM " SCHED_DEADLINE does not allow forking. Can not switch to PID modes!\n");
				return -1;
			}
			// otherwise switch to next mode
			use_cgroup = DM_CNTPID;
		} else {
			if(S_ISDIR(s.st_mode)) {
				/* it's a dir */
				cont("using CGroups to detect processes..\n");
			} else {
				/* exists but is no dir */
				// check for sCHED_DEADLINE first-> stop!
				if (SCHED_DEADLINE == policy) {
					err_msg( KRED "Error!" KNRM " SCHED_DEADLINE does not allow forking. Can not switch to PID modes!\n");
					return -1;
				}
				// otherwise switch to next mode
				use_cgroup = DM_CNTPID;
			}
		}
	}

	if (DM_CNTPID == use_cgroup)
		cont( "will use PIDs of '%s' to detect processes..\n", cont_ppidc);
	if (DM_CMDLINE == use_cgroup)
		cont( "will use PIDs of command signtaure '%s' to detect processes..\n", cont_pidc);

	/// --------------------
	/// cgroup present, fix cpu-sets of running containers
	if (DM_CGRP == use_cgroup) {

		char cpus[10];
		sprintf(cpus, "%d-%d", affinity,maxcpu-1); 

		cont( "reassigning Docker's CGroups CPU's to %s exclusively\n", cpus);

		DIR *d;
		struct dirent *dir;
		d = opendir(fileprefix);// -> pointing to global
		fileprefix = NULL; // clear pointer
		if (d) {

			while ((dir = readdir(d)) != NULL) {
			// scan trough docker cgroups, find them?
				if ((strlen(dir->d_name)>60) && // container strings are very long!
						(fileprefix=realloc(fileprefix,strlen(cpusetdfileprefix)
						+ strlen(dir->d_name)+1))) {
					fileprefix[0] = '\0';   // ensures the memory is an empty string
					// copy to new prefix
					strcat(fileprefix,cpusetdfileprefix);
					strcat(fileprefix,dir->d_name);

					if (setkernvar("/cpuset.cpus", cpus)){
						warn("Can not set cpu-affinity\n");
					}


				}
			}
			if (fileprefix)
				free (fileprefix);

			fileprefix = cpusetdfileprefix; // set to docker directory

			if (setkernvar("cpuset.cpus", cpus)){
				warn("Can not set cpu-affinity\n");
			}

			if (setkernvar("cpuset.cpu_exclusive", "1")){
				warn("Can not set cpu exclusive\n");
			}

			closedir(d);
		}

		//------- CREATE NEW CGROUP AND MOVE ALL ROOT TASKS TO IT ------------
		// system CGroup, possible tasks are moved

		fileprefix = NULL;
		sprintf(cpus, "%d-%d", 0, affinity-1); 
		cont("creating cgroup for system on %s\n", cpus);

		// detect numa configuration 
		char * numastr = "0"; // default numa string
		if (-1 != numa_available()) {
			int numanodes = numa_max_node();
			numastr = calloc (5, 1); // WARN -> not unallocated
			sprintf(numastr, "0-%d", numanodes);
		}
		else
			warn("Numa not enabled, defaulting to memory node '0'\n");

		if ((fileprefix=realloc(fileprefix,strlen(cpusetfileprefix)+strlen("system/")+1))) {
			fileprefix[0] = '\0';   // ensures the memory is an empty string
			// copy to new prefix
			strcat(fileprefix,cpusetfileprefix);
			strcat(fileprefix,"system/");
			if (!mkdir(fileprefix, ACCESSPERMS)) {
				if (setkernvar("cpuset.cpus", cpus)){
					warn("Can not set cpu-affinity\n");
				}
				if (setkernvar("cpuset.mems", numastr)){
					warn("Can not set numa memory nodes\n");
				}
				if (setkernvar("cpuset.cpu_exclusive", "1")){
					warn("Can not set cpu exclusive\n");
				}
			}
			else{
				switch (errno) {
				case EEXIST: // directory exists. do nothing?
					break;

				default: // error otherwise
					warn("Can not set cpu system group\n");
				}
			}
			cont( "moving tasks..\n");

			int mtask = 0;
			char * nfileprefix = NULL;
			if ((nfileprefix=realloc(nfileprefix,strlen(cpusetfileprefix)+strlen("tasks")+1))) {
				nfileprefix[0] = '\0';   // ensures the memory is an empty string
				// copy to new prefix
				strcat(nfileprefix,cpusetfileprefix);
				strcat(nfileprefix,"tasks");

				char pidline[1024];
				char *pid;
				int i =0  ;
				// prepare literal and open pipe request
				int path = open(nfileprefix,O_RDONLY);

				// Scan through string and put in array
				while(read(path, pidline,1024)) { // TODO: fix, doesn't get all tasks, readln?
					printDbg("Pid string return %s\n", pidline);
					pid = strtok (pidline,"\n");	
					while (NULL != pid) {

						// fileprefix still pointing to system/
						if (setkernvar("tasks", pid)){
							printDbg( KMAG "Warn!" KNRM " Can not move task %s\n", pid);
							mtask++;
						}
						pid = strtok (NULL,"\n");	

					}
				}
				if (mtask) 
					warn("Could not move %d tasks\n", mtask);					

				close(path);
			}

			// free string buffers
			if (fileprefix)
				free (fileprefix);

			if (nfileprefix)
				free (nfileprefix);

		}

	}

	/* lock all memory (prevent swapping) */
	if (lockall)
		if (-1 == mlockall(MCL_CURRENT|MCL_FUTURE)) {
			perror("mlockall");
			return -1;
		}

	return 0;
}

/// display_help(): Print usage information 
///
/// Arguments: exit with error?
///
/// Return value: - 
static void display_help(int error)
{
	printf("Usage:\n"
	       "schedstat <options> [config.json]\n\n"
	       "-a [NUM] --affinity        run system threads on processor 0-(NUM-1), if possible\n"
	       "                           run container threads on processor NUM-MAX_CPU \n"
	       "-b       --bind            bind non-RT PIDs of container to same container affinity\n"
	       "-c CLOCK --clock=CLOCK     select clock for measurement statistics\n"
	       "                           0 = CLOCK_MONOTONIC (default)\n"
	       "                           1 = CLOCK_REALTIME\n"
	       "                           2 = CLOCK_PROCESS_CPUTIME_ID\n"
	       "                           3 = CLOCK_THREAD_CPUTIME_ID\n"
	       "-d       --dflag           set deadline overrun flag for dl PIDs\n"
	       "-f                         force execution with critical parameters\n"
//	       "-F       --fifo=<path>     create a named pipe at path and write stats to it\n"
	       "-i INTV  --interval=INTV   base interval of update thread in us default=%d\n"
	       "-l LOOPS --loops=LOOPS     number of loops for container check: default=%d\n"
	       "-m       --mlockall        lock current and future memory allocations\n"
	       "-n [CMD]                   use CMD signature on PID to identify containers\n"
	       "                           optional CMD parameter specifies base signature, default=%s\n"
	       "-p PRIO  --priority=PRIO   priority of the measurement thread:default=0\n"
	       "	 --policy=NAME     policy of measurement thread, where NAME may be one\n"
	       "                           of: other, normal, batch, idle, deadline, fifo or rr.\n"
//	       "-q       --quiet           print a summary only on exit\n"
	       "-r RTIME --runtime=RTIME   set a maximum runtime in seconds, default=0(infinite)\n"
	       "-s [CMD]                   use shim PPID container detection.\n"
	       "                           optional CMD parameter specifies ppid command\n"
//	       "-t NUM   --threads=NUM     number of threads for resource management\n"
//	       "                           default = 1 (not changeable for now)\n"
//	       "-u       --unbuffered      force unbuffered output for live processing (FIFO)\n"
#ifdef NUMA
//	       "-U       --numa            force numa distribution of memory nodes, RR\n"
#endif
//	       "-v       --verbose         output values on stdout for statistics\n"
	       "-w       --wcet            WCET runtime for deadline policy in us, default=%d\n"
	       "-0                         negative deadlined difference are 0 (if GRUB is active\n"
			, TSCAN, TDETM, CONT_PID, TWCET
		);
	if (error)
		exit(EXIT_FAILURE);
	exit(EXIT_SUCCESS);
}

enum option_values {
	OPT_AFFINITY=1, OPT_BIND, OPT_CLOCK, OPT_DFLAG,
	OPT_FIFO, OPT_INTERVAL, OPT_LOOPS, OPT_MLOCKALL,
	OPT_NSECS, OPT_PRIORITY, OPT_QUIET, 
	OPT_THREADS, OPT_SMP, OPT_RTIME, OPT_UNBUFFERED, OPT_NUMA, 
	OPT_VERBOSE, OPT_WCET, OPT_POLICY, OPT_HELP,
};

/// process_options(): Process commandline options 
///
/// Arguments: - passed command line variables
///			   - number of cpus
///
/// Return value: -
static void process_options (int argc, char *argv[], int max_cpus)
{
	int error = 0;
	int option_affinity = 0;
	int option_index = 0;
	int force = 0;

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
			{"threads",          required_argument, NULL, OPT_THREADS },
			{"unbuffered",       no_argument,       NULL, OPT_UNBUFFERED },
			{"numa",             no_argument,       NULL, OPT_NUMA },
			{"verbose",          no_argument,       NULL, OPT_VERBOSE },
			{"policy",           required_argument, NULL, OPT_POLICY },
			{"wcet",             required_argument, NULL, OPT_WCET },
			{"help",             no_argument,       NULL, OPT_HELP },
			{NULL, 0, NULL, 0}
		};
		int c = getopt_long(argc, argv, "a:bc:dfFi:l:mn::p:qr:s::t:uUvw:0?",
				    long_options, &option_index);
		if (-1 == c)
			break;
		switch (c) {
		case 'a':
		case OPT_AFFINITY:
			option_affinity = 1;
			if (optarg != NULL) {
				affinity = atoi(optarg);
				if (affinity < 1 || affinity > max_cpus) {
					error = 1;
					err_msg(KRED "Error!" KNRM " affinity value '%s' not valid\n", optarg);
				}
				setaffinity = AFFINITY_SPECIFIED;
			} else if (optind<argc && atoi(argv[optind])) {
				affinity = atoi(argv[optind]);
				if (affinity < 1 || affinity > max_cpus) {
					error = 1;
					err_msg(KRED "Error!" KNRM " affinity value '%s' not valid\n", argv[optind]);
				}
				setaffinity = AFFINITY_SPECIFIED;
			} else { // TODO: remove cgroup limitations if present!
				setaffinity = AFFINITY_USEALL;
			}
			break;
		case 'b':
		case OPT_BIND:
			affother = 1; break;
		case 'c':
		case OPT_CLOCK:
			clocksel = atoi(optarg); break;
		case 'd':
		case OPT_DFLAG:
			setdflag = 1; break;
		case 'f':
			force = 1; break;
/*		case 'F':
		case OPT_FIFO:
			use_fifo = 1;
			strncpy(fifopath, optarg, strlen(optarg));
			break;
*/
		case 'i':
		case OPT_INTERVAL:
			interval = atoi(optarg); break;
		case 'l':
		case OPT_LOOPS:
			loops = atoi(optarg); break;
		case 'm':
		case OPT_MLOCKALL:
			lockall = 1; break;
		case 'n':
			use_cgroup = DM_CMDLINE;
			if (NULL != optarg) {
				cont_pidc = optarg;
			} else if (optind<argc && atoi(argv[optind])) {
				cont_pidc = argv[optind];
			}
			break;
		case 'p':
		case OPT_PRIORITY:
			priority = atoi(optarg);
			if (SCHED_FIFO != policy && SCHED_RR != policy)
				warn(" policy and priority don't match: setting policy to SCHED_FIFO\n");
				policy = SCHED_FIFO;
			break;
		case 'q':
		case OPT_QUIET:
			quiet = 1; break;
		case 'r':
		case OPT_RTIME:
			if (NULL != optarg) {
				runtime = atoi(optarg);
			} else if (optind<argc && atoi(argv[optind])) {
				runtime = atoi(argv[optind]);
			}
			break;
		case 's':
			use_cgroup = DM_CNTPID;
			if (NULL != optarg) {
				cont_ppidc = optarg;
			} else if (optind<argc && atoi(argv[optind])) {
				cont_ppidc = argv[optind];
			}
			break;
/*		case 't':
		case OPT_THREADS:
			if (smp) {
				warn("-t ignored due to --smp\n");
				break;
			}
			if (NULL != optarg)
				num_threads = atoi(optarg);
			else if (optind<argc && atoi(argv[optind]))
				num_threads = atoi(argv[optind]);
			else
				num_threads = max_cpus;
			break;*/
/*		case 'u':
		case OPT_UNBUFFERED:
			setvbuf(stdout, NULL, _IONBF, 0); break;*/
/*		case 'U': //TODO: fix numa ??
		case OPT_NUMA: // NUMA testing 
			numa = 1;	// Turn numa on 
			if (smp)
				fatal("numa and smp options are mutually exclusive\n");
			//numa_on_and_available();
#ifdef NUMA
			num_threads = max_cpus;
			setaffinity = AFFINITY_USEALL;
#else
			warn("schedstat was not built with the numa option\n");
			warn("ignoring --numa or -U\n");
#endif
			break; */ 
/*		case 'v':
		case OPT_VERBOSE: 
			verbose = 1; break;*/
		case 'w':
		case OPT_WCET:
			update_wcet = atoi(optarg); break;
		case '0':
			negiszero = 1; break;
		case '?':
		case OPT_HELP:
			display_help(0); break;
		case OPT_POLICY:
			policy = string_to_policy(optarg); break;
		}
	}

	// option mismatch verification
	if (option_affinity) {
		if (smp) {
			warn("-a ignored due to --smp\n");
		} else if (numa) {
			warn("-a ignored due to --numa\n");
		}
	}

	// check clock sel boundaries
	if (0 > clocksel || 3 < clocksel)
		error = 1;

	// check priority boundary
	if (0 > priority || 99 < priority)
		error = 1;

	// check detection mode and policy -> deadline does not allow fork!
	if (SCHED_DEADLINE == policy && (DM_CNTPID == use_cgroup || DM_CMDLINE == use_cgroup)) {
		warn("can not use SCHED_DEADLINE with PID detection modes: setting policy to SCHED_FIFO\n");
		policy = SCHED_FIFO;	
	}

	// deadline and high refresh might starve the system. require force
	if (SCHED_DEADLINE == policy && interval < 10000 && !force) {
		warn("Using SCHED_DEADLINE with such low intervals can starve a system. Use force (-f) to start anyway.\n");
		error = 1;
	}

	// check priority and policy match
	if (priority && (SCHED_FIFO != policy && SCHED_RR != policy)) {
		warn("policy and priority don't match: setting policy to SCHED_FIFO\n");
		policy = SCHED_FIFO;
	}

	// check policy with priority match 
	if ((SCHED_FIFO == policy || SCHED_RR == policy) && 0 == priority) {
		warn("defaulting realtime priority to %d\n",
		num_threads+1); // TODO: num threads and prio connection??
		priority = num_threads+1;
	}

	// num theads must be > 0 
	if (1 > num_threads)
		error = 1;

	// look for filename after options, we process only first
	if (optind < argc)
	{
	    config = argv[optind];
	}

	// allways verify for config file -> segmentation fault??
	if ( access( config, F_OK )) {
		err_msg(KRED "Error!" KNRM " configuration file '%s' not found\n", config);
		error = 1;
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

//	(void)printf("Starting main PID: %d\n", getpid()); // ?? duplicate main pid query?
	(void)printf("%s V %1.2f\n", PRGNAME, VERSION);	
	(void)printf("Source compilation date: %s\n", __DATE__);
	(void)printf("This software comes with no waranty. Please be careful\n\n");

	process_options(argc, argv, max_cpus);
	
	// gather actual information at startup, prepare environment
	if (prepareEnvironment()) 
		err_quit("Hard HALT.\n");

	pthread_t thread1, thread2;
	int32_t t_stat1 = 0; // we control thread status 32bit to be sure read is atomic on 32 bit -> sm on treads
	int32_t t_stat2 = 0; 
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */ 
	if (iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &t_stat1)) {
		err_msg (KRED "Error!" KNRM " could not start update thread: %s", strerror(iret1));
		t_stat1 = -1;
	}
	if (iret2 = pthread_create( &thread2, NULL, thread_update, (void*) &t_stat2)) {
		err_msg (KRED "Error!" KNRM " could not start management thread: %s", strerror(iret2));
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

	/* unlock everything */
	if (lockall)
		munlockall();

    return 0;
}
