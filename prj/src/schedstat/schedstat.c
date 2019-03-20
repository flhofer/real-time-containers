#include "schedstat.h" // main settings and header file

#include "update.h"
#include "manage.h"
#include <sys/mman.h>

// Global variables for all the threads and programms

// signal to keep status of triggers ext SIG
volatile sig_atomic_t stop;
// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;

// head of pidlist
node_t * head = NULL;

int use_cgroup = DM_CMDLINE; // identify processes via cgroup


/// inthand(): interrupt handler for infinite while loop, help 
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: 

void inthand ( int signum ) {
	stop = 1;
}


/// Kernel variable management (ct extract)

#define KVARS			32
#define KVARNAMELEN		32
#define KVALUELEN		32

int enable_events;
int verbose = 0;

static int kernelversion;
static int sys_cpus = 1; // 0-> count reserved for orchestrator and system

static int lockall = 0;
static int duration = 0;
static int use_nsecs = 0;
static int refresh_on_max;
//static int force_sched_other;
static int check_clock_resolution;
static int use_fifo = 0;
//static pthread_t fifo_threadid;


static int aligned = 0;
static int secaligned = 0;
static int offset = 0;

static char fifopath[MAX_PATH]; // TODO:  implement fifo thread as in cycictest for readout
static char *fileprefix;

static int32_t latency_target_value = 0;


/* Backup of kernel variables that we modify */
static struct kvars {
	char name[KVARNAMELEN];
	char value[KVALUELEN];
} kv[KVARS];

enum kernelversion {
	KV_NOT_SUPPORTED,
	KV_26_LT18,
	KV_26_LT24,
	KV_26_33,
	KV_30,
	KV_40,
	KV_413,	// includes full EDF for the first time
	KV_416	// includes full EDF with GRUB-PA for ARM
};

void inline vbprintf ( const char * format, ... )
{
	if (!verbose)
		return;
	va_list args;
	va_start (args, format);
	vprintf (format, args);
  	va_end (args);
}

static int check_kernel(void)
{
	struct utsname kname;
	int maj, min, sub, kv;

	if (uname(&kname)) {
		printDbg(KRED "Error!" KNRM " uname failed: %s. Assuming not 2.6\n",
				strerror(errno));
		return KV_NOT_SUPPORTED;
	}
	sscanf(kname.release, "%d.%d.%d", &maj, &min, &sub);
	if (maj == 2 && min == 6) {
		if (sub < 18)
			kv = KV_26_LT18;
			// toto if 
		else if (sub < 24)
			kv = KV_26_LT24;
			// toto if 
		else if (sub < 28) 
			kv = KV_26_33;
			// toto if 
		else 
			kv = KV_26_33;
			// toto if 
		
	} else if (maj == 3) {
		// kernel 3.x standard LT kernel for embedded
		kv = KV_30;
		// toto if 
	} else if (maj == 4) { // fil

		// kernel 4.x introduces Deadline scheduling
		if (sub < 13)
			// standard
			kv = KV_40;
		else if (sub < 16)
			// full EDF
			kv = KV_413;
		else 
			// full EDF -PA
			kv = KV_416;

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
	if (path >= 0) {
		if (mode == O_RDONLY) {
			int got;
			if ((got = read(path, value, sizeofvalue)) > 0) {
				retval = 0;
				value[got-1] = '\0';
			}
		} else if (mode == O_WRONLY) {
			if (write(path, value, sizeofvalue) == sizeofvalue)
				retval = 0;
		}
		close(path);
	}
	return retval;
}

static int setkernvar(const char *name, char *value)
{
	int i;
	char oldvalue[KVALUELEN];

	if (kernelversion < KV_26_33) {
		if (kernvar(O_RDONLY, name, oldvalue, sizeof(oldvalue)))
			printDbg(KRED "Error!" KNRM " could not retrieve %s\n", name);
		else {
			for (i = 0; i < KVARS; i++) {
				if (!strcmp(kv[i].name, name))
					break;
				if (kv[i].name[0] == '\0') {
					strncpy(kv[i].name, name,
						sizeof(kv[i].name));
					strncpy(kv[i].value, oldvalue,
					    sizeof(kv[i].value));
					break;
				}
			}
			if (i == KVARS)
				printDbg(KRED "Error!" KNRM " could not backup %s (%s)\n", name, oldvalue);
		}
	}
	if (kernvar(O_WRONLY, name, value, strlen(value))){
// PDB		printDbg(KRED "Error!" KNRM " could not set %s to %s\n", name, value);
		return -1;
	}
	
	return 0;

}

static void restorekernvars(void)
{
	int i;

	for (i = 0; i < KVARS; i++) {
		if (kv[i].name[0] != '\0') {
			if (kernvar(O_WRONLY, kv[i].name, kv[i].value,
			    strlen(kv[i].value)))
				printDbg(KRED "Error!" KNRM " could not restore %s to %s\n",
					kv[i].name, kv[i].value);
		}
	}
}

/// Kernel variable management - end (ct extract)

/// prepareEnvironment(): gets the list of active pids at startup, sets up
/// a CPU-shield if not present, prepares kernel settings for DL operation
/// and populates initial state of pid list
///
/// Arguments: 
///
/// Return value: error code if present
///
static int prepareEnvironment() {

	/// prerequisites
	printf("Info: This system has %d processors configured and "
        "%d processors available.\n",
        get_nprocs_conf(), get_nprocs());

	/// --------------------
	/// verify executable permissions	
	// TODO: upgrade to libcap-ng
	printDbg( "Info: Verifying for process capabilities..\n");
	cap_t cap = cap_get_proc(); // get capability map of proc
	if (!cap) {
		printDbg( KRED "Error!" KNRM " Can not get capability map!\n");
		return errno;
	}
	cap_flag_value_t v = 0; // flag to store return value
	if (cap_get_flag(cap, CAP_SYS_NICE, CAP_EFFECTIVE, &v)) {// check for effective NICE cap
		printDbg( KRED "Error!" KNRM " Capability test failed!\n");
		return errno;
	}

	if (!CAP_IS_SUPPORTED(CAP_SYS_NICE) || (0==v)) {
		printDbg( KRED "Error!" KNRM " SYS_NICE capability mandatory to operate properly!\n");
		return -1;
	}

	/// --------------------
	/// Kernel variables, disable bandwidth management and RT-throttle
	/// Kernel RT-bandwidth management must be disabled to allow deadline+affinity
	kernelversion = check_kernel();
	fileprefix = procfileprefix; // set working prefix for vfs

	if (kernelversion == KV_NOT_SUPPORTED)
		printDbg( KMAG "Warn!" KNRM " Running on unknown kernel version...YMMV\nTrying generic configuration..\n");

	printDbg( "... Set realtime bandwith limit to (unconstrained)..\n");
	// disable bandwidth control and realtime throttle
	if (setkernvar("sched_rt_runtime_us", "-1")){
		printDbg( KMAG "Warn!" KNRM " RT-throttle still enabled. Limitations apply.\n");
	}

	/// --------------------
	/// running settings for scheduler
	struct sched_attr attr; 
	int flags = 0U; // must be initialized?
	pid_t mpid = getpid();
	if (sched_getattr (mpid, &attr, sizeof(attr), flags))
		printDbg(KRED "Error!" KNRM " reading attributes: %s\n", strerror(errno));

	printDbg( "... orchestrator scheduled as '%s'\n", policyname(attr.sched_policy));


	printDbg( "... promoting process and setting affinity..\n");

	if (sched_setattr (mpid, &attr, flags))
		printDbg(KRED "Error!" KNRM ": %s\n", strerror(errno));

	cpu_set_t cset;
	CPU_ZERO(&cset);
	CPU_SET(0, &cset); // set process to CPU zero

	if (sched_setaffinity(mpid, sizeof(cset), &cset ))
		printDbg(KRED "Error!" KNRM " affinity: %s\n", strerror(errno));
		// not possible with sched_deadline
	else
		printDbg("... Pid %d reassigned to CPU%d\n", mpid, 0);


	/// TODO: setup cgroup -> may conflict with container groups?

	/// --------------------
	/// checkout cgroup cpuset for docker instances
	struct stat s;

	// no memory has been allocated yet
	fileprefix = cpusetdfileprefix; // set to docker directory

	int err = stat(fileprefix, &s);
	if(-1 == err) {
		if(ENOENT == errno) {
		    printDbg(KMAG "Warn!" KNRM " : cgroup '%s' does not exist. Is it running?\n", "docker/");
			printDbg( "... will use PIDs of '%s' to detect processes..\n", CONT_PPID);
		} else {
		    perror("stat");
		}
		use_cgroup = DM_CNTPID;
	} else {
		if(S_ISDIR(s.st_mode)) {
		    /* it's a dir */
			printDbg( "Info: using Cgroups to detect processes..\n");
			use_cgroup = DM_CGRP;
		} else {
		    /* exists but is no dir */
			use_cgroup = DM_CNTPID;
		}
	}

	/// --------------------
	/// cgroup present, fix cpu-sets of running containers
	if (DM_CGRP == use_cgroup) {

		char cpus[10];
		sprintf(cpus, "%d-%d", SYSCPUS,get_nprocs_conf()-1); 

		printDbg( "... reassigning Docker's CGroups CPU's to %s exclusively\n", cpus);

		DIR *d;
		struct dirent *dir;
		d = opendir(fileprefix);// -> pointing to global
		fileprefix = NULL; // clear pointer
		if (d) {

			while ((dir = readdir(d)) != NULL) {
			// scan trough docker cgroups, find them?
				if ((strlen(dir->d_name)>60) && // container strings are very long!
						(fileprefix=realloc(fileprefix,strlen(cpusetdfileprefix)+strlen(dir->d_name)+1))) {
					fileprefix[0] = '\0';   // ensures the memory is an empty string
					// copy to new prefix
					strcat(fileprefix,cpusetdfileprefix);
					strcat(fileprefix,dir->d_name);

					if (setkernvar("/cpuset.cpus", cpus)){
						printDbg( KMAG "Warn!" KNRM " Can not set cpu-affinity\n");
					}


				}
			}
			if (fileprefix)
				free (fileprefix);

			fileprefix = cpusetdfileprefix; // set to docker directory

			if (setkernvar("cpuset.cpus", cpus)){
				printDbg( KMAG "Warn!" KNRM " Can not set cpu-affinity\n");
			}

			if (setkernvar("cpuset.cpu_exclusive", "1")){
				printDbg( KMAG "Warn!" KNRM " Can not set cpu exclusive\n");
			}

			closedir(d);
		}

		fileprefix = NULL;
		sprintf(cpus, "%d-%d", 0, SYSCPUS-1); 
		printDbg( "... creating cgroup for system on %s\n", cpus);

		if ((fileprefix=realloc(fileprefix,strlen(cpusetfileprefix)+strlen("system/")+1))) {
			fileprefix[0] = '\0';   // ensures the memory is an empty string
			// copy to new prefix
			strcat(fileprefix,cpusetfileprefix);
			strcat(fileprefix,"system/");
			if (!mkdir(fileprefix, ACCESSPERMS)) {
				if (setkernvar("cpuset.cpus", cpus)){
					printDbg( KMAG "Warn!" KNRM " Can not set cpu-affinity\n");
				}
				if (setkernvar("cpuset.mems", "0")){ // TODO: fix cpuset mems
					printDbg( KMAG "Warn!" KNRM " Can not set cpu exclusive\n");
				}
				if (setkernvar("cpuset.cpu_exclusive", "1")){
					printDbg( KMAG "Warn!" KNRM " Can not set cpu exclusive\n");
				}
			}
			else{
				switch (errno) {
				case EEXIST: // directory exists. do nothing?
					break;

				default: // error otherwise
					printDbg( KMAG "Warn!" KNRM " Can not set cpu system group\n");
				}
			}
			printDbg( "... moving tasks..\n");

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
					//printDbg("Pid string return %s\n", pidline);
					pid = strtok (pidline,"\n");	
					while (pid != NULL) {

						// fileprefix still pointing to system/
						if (setkernvar("tasks", pid)){
// PDB							printDbg( KMAG "Warn!" KNRM " Can not move task %s\n", pid);
							mtask++;
						}
						pid = strtok (NULL,"\n");	

					}
				}
				if (mtask) 
					printDbg( KMAG "Warn!" KNRM " Could not move %d tasks\n", mtask);					

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
		if (mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
			perror("mlockall");
			return -1;
		}


	return 0;
}

// -- new options
/// Option parsing !!

static int use_nanosleep;
static int timermode = TIMER_ABSTIME;
static int use_system;
int priority=0;
int policy = SCHED_OTHER;	/* default policy if not specified */
static int num_threads = 1;
static int clocksel = 0;
static int quiet;
int interval = TSCAN;
int loops = TDETM;
static int distance = -1;
static struct bitmask *affinity_mask = NULL;
static int smp = 0;

enum {
	AFFINITY_UNSPECIFIED,
	AFFINITY_SPECIFIED,
	AFFINITY_USEALL
};
static int setaffinity = AFFINITY_UNSPECIFIED;

static int clocksources[] = {
	CLOCK_MONOTONIC,
	CLOCK_REALTIME,
};

/* Print usage information */
static void display_help(int error)
{
	char tracers[MAX_PATH];
	char *prefix;

	prefix = get_debugfileprefix();
	if (prefix[0] == '\0')
		strcpy(tracers, "unavailable (debugfs not mounted)");
	else {
		fileprefix = prefix;
		if (kernvar(O_RDONLY, "available_tracers", tracers, sizeof(tracers)))
			strcpy(tracers, "none");
	}

	printf("%s V %1.2f\n", PRGNAME, VERSION);
	printf("Usage:\n"
	       "schedstat <options>\n\n"
#if LIBNUMA_API_VERSION >= 2
	       "-a [CPUSET] --affinity     Run thread #N on processor #N, if possible, or if CPUSET\n"
	       "                           given, pin threads to that set of processors in round-\n"
	       "                           robin order.  E.g. -a 2 pins all threads to CPU 2,\n"
	       "                           but -a 3-5,0 -t 5 will run the first and fifth\n"
	       "                           threads on CPU (0),thread #2 on CPU 3, thread #3\n"
	       "                           on CPU 4, and thread #5 on CPU 5.\n"
#else
	       "-a [NUM] --affinity        run thread #N on processor #N, if possible\n"
	       "                           with NUM pin all threads to the processor NUM\n"
#endif
	       "-F       --fifo=<path>     create a named pipe at path and write stats to it\n"
	       "-i INTV  --interval=INTV   base interval of update thread in us default=1000\n"
	       "-l LOOPS --loops=LOOPS     number of loops for container check: default=10\n"
	       "-m       --mlockall        lock current and future memory allocations\n"
	       "-p PRIO  --priority=PRIO   priority of the measurement thread:default=0\n"
	       "	 --policy=NAME     policy of measurement thread, where NAME may be one\n"
	       "                           of: other, normal, batch, idle, deadline, fifo or rr.\n"
	       "-q       --quiet           print a summary only on exit\n"
	       "-S       --smp             Standard SMP testing: options -a -t -n and\n"
	       "                           same priority of all threads\n"
	       "-t       --threads         one thread per available processor\n"
	       "-t [NUM] --threads=NUM     number of threads:\n"
	       "                           without NUM, threads = max_cpus\n"
	       "                           without -t default = 1\n"
	       "-u       --unbuffered      force unbuffered output for live processing\n"
#ifdef NUMA
	       "-U       --numa            Standard NUMA testing (similar to SMP option)\n"
	       "                           thread data structures allocated from local node\n"
#endif
	       "-v       --verbose         output values on stdout for statistics\n"
	       "                           format: n:c:v n=tasknum c=count v=value in us\n"
		);
	if (error)
		exit(EXIT_FAILURE);
	exit(EXIT_SUCCESS);
}

static unsigned int is_cpumask_zero(const struct bitmask *mask)
{
	return (rt_numa_bitmask_count(mask) == 0);
}

/*
static inline struct bitmask* rt_numa_parse_cpustring(const char* s,
	int max_cpus)
{
	int cpu;
	struct bitmask *mask = NULL;
	cpu = atoi(s);
	if (0 <= cpu && cpu < max_cpus) {
		mask = malloc(sizeof(*mask));
		if (mask) {
			/ Round up to integral number of longs to contain
			 * max_cpus bits /
			int nlongs = (max_cpus+BITS_PER_LONG-1)/BITS_PER_LONG;

			mask->maskp = calloc(nlongs, sizeof(unsigned long));
			if (mask->maskp) {
				mask->maskp[cpu/BITS_PER_LONG] |=
					(1UL << (cpu % BITS_PER_LONG));
				mask->size = max_cpus;
			} else {
				free(mask);
				mask = NULL;
			}
		}
	}
	return mask;
}
*/

static void parse_cpumask(const char *option, const int max_cpus)
{

	affinity_mask = rt_numa_parse_cpustring(option, max_cpus);
	if (affinity_mask) {
		if (is_cpumask_zero(affinity_mask)) {
			rt_bitmask_free(affinity_mask);
			affinity_mask = NULL;
		}
	}
	if (!affinity_mask)
		display_help(1);

	if (verbose) {
		printf("%s: Using %u cpus.\n", __func__,
			rt_numa_bitmask_count(affinity_mask));
	}
}


enum option_values {
	OPT_AFFINITY=1, OPT_ALIGNED, OPT_CLOCK,
	OPT_DISTANCE, OPT_DURATION, OPT_LATENCY, OPT_EVENT,
	OPT_FIFO, OPT_INTERVAL, OPT_LOOPS, OPT_MLOCKALL, OPT_REFRESH,
	OPT_NANOSLEEP, OPT_NSECS, OPT_PRIORITY, OPT_QUIET, 
	OPT_RELATIVE, OPT_RESOLUTION, OPT_SYSTEM, OPT_SMP, OPT_THREADS,
	OPT_UNBUFFERED, OPT_NUMA, OPT_VERBOSE, OPT_POLICY, 
	OPT_HELP, OPT_NUMOPTS, OPT_SECALIGNED,  
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

	for (;;) {
		int option_index = 0;
		/*
		 * Options for getopt
		 * Ordered alphabetically by single letter name
		 */
		static struct option long_options[] = {
			{"affinity",         required_argument, NULL, OPT_AFFINITY},
			{"fifo",             required_argument, NULL, OPT_FIFO },
			{"interval",         required_argument, NULL, OPT_INTERVAL },
			{"loops",            required_argument, NULL, OPT_LOOPS },
			{"mlockall",         no_argument,       NULL, OPT_MLOCKALL },
			{"priority",         required_argument, NULL, OPT_PRIORITY },
			{"quiet",            no_argument,       NULL, OPT_QUIET },
			{"smp",              no_argument,       NULL, OPT_SMP },
			{"threads",          optional_argument, NULL, OPT_THREADS },
			{"unbuffered",       no_argument,       NULL, OPT_UNBUFFERED },
			{"numa",             no_argument,       NULL, OPT_NUMA },
			{"verbose",          no_argument,       NULL, OPT_VERBOSE },
			{"policy",           required_argument, NULL, OPT_POLICY },
			{"help",             no_argument,       NULL, OPT_HELP },
			{NULL, 0, NULL, 0}
		};
		int c = getopt_long(argc, argv, "a:Fi:l:mp:qSt::uUv",
				    long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
		case 'a':
		case OPT_AFFINITY:
			option_affinity = 1;
			if (smp || numa)
				break;
			if (optarg != NULL) {
				parse_cpumask(optarg, max_cpus);
				setaffinity = AFFINITY_SPECIFIED;
			} else if (optind<argc && atoi(argv[optind])) {
				parse_cpumask(argv[optind], max_cpus);
				setaffinity = AFFINITY_SPECIFIED;
			} else {
				setaffinity = AFFINITY_USEALL;
			}
			break;
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
		case 'p':
		case OPT_PRIORITY:
			priority = atoi(optarg);
			if (policy != SCHED_FIFO && policy != SCHED_RR)
				policy = SCHED_FIFO;
			break;
		case 'q':
		case OPT_QUIET:
			quiet = 1; break;
		case 'S':
		case OPT_SMP: /* SMP testing */
			if (numa)
				fatal("numa and smp options are mutually exclusive\n");
			smp = 1;
			num_threads = max_cpus;
			setaffinity = AFFINITY_USEALL;
//			use_nanosleep = MODE_CLOCK_NANOSLEEP;
			break;
		case 't':
		case OPT_THREADS:
			if (smp) {
				warn("-t ignored due to --smp\n");
				break;
			}
			if (optarg != NULL)
				num_threads = atoi(optarg);
			else if (optind<argc && atoi(argv[optind]))
				num_threads = atoi(argv[optind]);
			else
				num_threads = max_cpus;
			break;
		case 'u':
		case OPT_UNBUFFERED:
			setvbuf(stdout, NULL, _IONBF, 0); break;
		case 'U':
		case OPT_NUMA: /* NUMA testing */
			numa = 1;	/* Turn numa on */
			if (smp)
				fatal("numa and smp options are mutually exclusive\n");
			numa_on_and_available();
#ifdef NUMA
			num_threads = max_cpus;
			setaffinity = AFFINITY_USEALL;
//			use_nanosleep = MODE_CLOCK_NANOSLEEP;
#else
			warn("cyclictest was not built with the numa option\n");
			warn("ignoring --numa or -U\n");
#endif
			break;
		case 'v':
		case OPT_VERBOSE: verbose = 1; break;
		case '?':
		case OPT_HELP:
			display_help(0); break;

		/* long only options */
		case OPT_LATENCY:
                          /* power management latency target value */
			  /* note: default is 0 (zero) */
			latency_target_value = atoi(optarg);
			if (latency_target_value < 0)
				latency_target_value = 0;
			break;
		case OPT_POLICY:
			handlepolicy(optarg); break;
		}
	}

	if (option_affinity) {
		if (smp) {
			warn("-a ignored due to --smp\n");
		} else if (numa) {
			warn("-a ignored due to --numa\n");
		}
	}

	if (priority < 0 || priority > 99)
		error = 1;

	if (priority && (policy != SCHED_FIFO && policy != SCHED_RR)) {
		printDbg(KMAG "Warn!" KNRM " policy and priority don't match: setting policy to SCHED_FIFO\n");
		policy = SCHED_FIFO;
	}

	if ((policy == SCHED_FIFO || policy == SCHED_RR) && priority == 0) {
		printDbg(KMAG "Warn!" KNRM " defaulting realtime priority to %d\n",
			num_threads+1);
		priority = num_threads+1;
	}

	if (num_threads < 1)
		error = 1;

	if (error) {
		if (affinity_mask)
			rt_bitmask_free(affinity_mask);
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

	process_options(argc, argv, max_cpus);
	
	printDbg("Starting main PID: %d\n", getpid()); // TODO: duplicate main pid query?
	printDbg("%s V %1.2f\n", PRGNAME, VERSION);	
	printDbg("Source compilation date: %s\n", __DATE__);
	printDbg("This software comes with no waranty. Please be careful\n\n");

	// gather actual information at startup, prepare environment
	if (prepareEnvironment()) {
		printDbg("Hard HALT.\n");
		exit(EXIT_FAILURE);
	}

	pthread_t thread1, thread2;
	int32_t t_stat1 = 0; // we control thread status 32bit to be sure read is atomic on 32 bit -> sm on treads
	int32_t t_stat2 = 0; 
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */
	iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &t_stat1);
	iret2 = pthread_create( &thread2, NULL, thread_update, (void*) &t_stat2);

	// set interrupt sig hand
	signal(SIGINT, inthand);
	signal(SIGTERM, inthand);
	signal(SIGUSR1, inthand);

	while (!stop && (t_stat1 != -1 || t_stat2 != -1)) {
		sleep (1);
	}

	// signal shutdown to threads
	t_stat1 = -1;
	t_stat2 = -1;

	// wait until threads have stopped
	pthread_join( thread1, NULL);
	pthread_join( thread2, NULL); 

    printDbg("exiting safely\n");

	/* unlock everything */
	if (lockall)
		munlockall();

	restorekernvars(); // restore previous variables
    return 0;
}
