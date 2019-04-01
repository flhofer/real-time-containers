// main settings and header file
#include "schedstat.h" 

// header files of launched threads
#include "update.h"
#include "manage.h"

// Things that should be needed only here
#include <sys/mman.h>
#include <numa.h>

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

int priority=0;
int clocksel = 0;
int policy = SCHED_OTHER;	/* default policy if not specified */
int quiet = 0;
int interval = TSCAN;
int update_wcet = TWCET;
int loops = TDETM;

/* Backup of kernel variables that we modify */

/// Kernel variable management (ct extract)
#define KVARS			32
#define KVARNAMELEN		32
#define KVALUELEN		32

static struct kvars {
	char name[KVARNAMELEN];
	char value[KVALUELEN];
} kv[KVARS];

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
	if (maj == 3) {
		if (min < 14)
			// EDF not implemented 
			kv = KV_NOT_SUPPORTED;
		else
		// kernel 3.x standard LT kernel for embedded
		kv = KV_314;
		// todo if 
	} else if (maj == 4) { // fil

		// kernel 4.x introduces Deadline scheduling
		if (min < 13)
			// standard
			kv = KV_40;
		else if (min < 16)
			// full EDF
			kv = KV_413;
		else 
			// full EDF -PA
			kv = KV_416;
	} else if (maj == 4) { // fil
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
/*	int i;
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
	}*/
	if (kernvar(O_WRONLY, name, value, strlen(value))){
		printDbg(KRED "Error!" KNRM " could not set %s to %s\n", name, value);
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
				err_msg(KRED "Error!" KNRM " could not restore %s to %s\n",
					kv[i].name, kv[i].value);
		}
	}
}

/// Kernel variable management - end (ct extract)

// -- new options
/// Option parsing !!

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

	/// prerequisites
	info("This system has %d processors configured and "
        "%d processors available.\n",
        get_nprocs_conf(), get_nprocs());

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

	if (kernelversion == KV_NOT_SUPPORTED)
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
		cont( "orchestrator scheduled as '%s'\n", policyname(attr.sched_policy));

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
			use_cgroup = DM_CNTPID;
		} else {
			if(S_ISDIR(s.st_mode)) {
				/* it's a dir */
				cont("using CGroups to detect processes..\n");
			} else {
				/* exists but is no dir */
				use_cgroup = DM_CNTPID;
			}
		}
	}

	if (DM_CNTPID == use_cgroup)
		cont( "will use PIDs of '%s' to detect processes..\n", cont_ppidc);
	if (DM_CMDLINE == use_cgroup)
		cont( "will use PIDs of configured commands to detect processes..\n");

	/// --------------------
	/// cgroup present, fix cpu-sets of running containers
	if (DM_CGRP == use_cgroup) {

		char cpus[10];
		sprintf(cpus, "%d-%d", affinity,get_nprocs_conf()-1); 

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

		fileprefix = NULL;
		sprintf(cpus, "%d-%d", 0, affinity-1); 
		cont("creating cgroup for system on %s\n", cpus);

		if ((fileprefix=realloc(fileprefix,strlen(cpusetfileprefix)+strlen("system/")+1))) {
			fileprefix[0] = '\0';   // ensures the memory is an empty string
			// copy to new prefix
			strcat(fileprefix,cpusetfileprefix);
			strcat(fileprefix,"system/");
			if (!mkdir(fileprefix, ACCESSPERMS)) {
				if (setkernvar("cpuset.cpus", cpus)){
					warn("Can not set cpu-affinity\n");
				}
				if (setkernvar("cpuset.mems", "0")){ // TODO: fix cpuset mems
					warn("Can not set cpu exclusive\n");
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
					while (pid != NULL) {

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
		if (mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
			perror("mlockall");
			return -1;
		}

	return 0;
}

/* Print usage information */
static void display_help(int error)
{
	printf("Usage:\n"
	       "schedstat <options> [config.json]\n\n"
	       "-a [NUM] --affinity        run system threads on processor 0-(NUM-1), if possible\n"
	       "                           run container threads on processor NUM-MAX_CPU \n"
	       "-c CLOCK --clock=CLOCK     select clock for measurement statistics\n"
	       "                           0 = CLOCK_MONOTONIC (default)\n"
	       "                           1 = CLOCK_REALTIME\n"
	       "                           2 = CLOCK_PROCESS_CPUTIME_ID\n"
	       "                           3 = CLOCK_THREAD_CPUTIME_ID\n"
//	       "-F       --fifo=<path>     create a named pipe at path and write stats to it\n"
	       "-i INTV  --interval=INTV   base interval of update thread in us default=%d\n"
	       "-l LOOPS --loops=LOOPS     number of loops for container check: default=%d\n"
	       "-m       --mlockall        lock current and future memory allocations\n"
	       "-n                         use CMD signature on PID to identify containers\n"
	       "-p PRIO  --priority=PRIO   priority of the measurement thread:default=0\n"
	       "	 --policy=NAME     policy of measurement thread, where NAME may be one\n"
	       "                           of: other, normal, batch, idle, deadline, fifo or rr.\n"
//	       "-q       --quiet           print a summary only on exit\n"
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
			, TSCAN, TDETM, TWCET
		);
	if (error)
		exit(EXIT_FAILURE);
	exit(EXIT_SUCCESS);
}

enum option_values {
	OPT_AFFINITY=1, OPT_CLOCK,
	OPT_FIFO, OPT_INTERVAL, OPT_LOOPS, OPT_MLOCKALL,
	OPT_NSECS, OPT_PRIORITY, OPT_QUIET, 
	OPT_THREADS, OPT_SMP, OPT_UNBUFFERED, OPT_NUMA, 
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

	for (;;) {
		//option_index = 0;
		/*
		 * Options for getopt
		 * Ordered alphabetically by single letter name
		 */
		static struct option long_options[] = {
			{"affinity",         required_argument, NULL, OPT_AFFINITY},
			{"clock",            required_argument, NULL, OPT_CLOCK },
			{"fifo",             required_argument, NULL, OPT_FIFO },
			{"interval",         required_argument, NULL, OPT_INTERVAL },
			{"loops",            required_argument, NULL, OPT_LOOPS },
			{"mlockall",         no_argument,       NULL, OPT_MLOCKALL },
			{"priority",         required_argument, NULL, OPT_PRIORITY },
			{"quiet",            no_argument,       NULL, OPT_QUIET },
			{"threads",          required_argument, NULL, OPT_THREADS },
			{"unbuffered",       no_argument,       NULL, OPT_UNBUFFERED },
			{"numa",             no_argument,       NULL, OPT_NUMA },
			{"verbose",          no_argument,       NULL, OPT_VERBOSE },
			{"policy",           required_argument, NULL, OPT_POLICY },
			{"wcet",             required_argument, NULL, OPT_WCET },
			{"help",             no_argument,       NULL, OPT_HELP },
			{NULL, 0, NULL, 0}
		};
		int c = getopt_long(argc, argv, "a:c:Fi:l:mnp:qs::t:uUvw:",
				    long_options, &option_index);
		if (c == -1)
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
			} else {
				setaffinity = AFFINITY_USEALL;
			}
			break;
		case 'c':
		case OPT_CLOCK:
			clocksel = atoi(optarg); break;
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
			break;
		case 'p':
		case OPT_PRIORITY:
			priority = atoi(optarg);
			if (policy != SCHED_FIFO && policy != SCHED_RR)
				warn(" policy and priority don't match: setting policy to SCHED_FIFO\n");
				policy = SCHED_FIFO;
			break;
		case 'q':
		case OPT_QUIET:
			quiet = 1; break;
		case 's':
			use_cgroup = DM_CNTPID;
			if (optarg != NULL) {
				cont_ppidc = optarg;
			} else if (optind<argc && atoi(argv[optind])) {
				cont_ppidc = argv[optind];
			}
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
		case 'v':
		case OPT_VERBOSE: 
			verbose = 1; break;
		case 'w':
		case OPT_WCET:
			update_wcet = atoi(optarg); break;
		case '?':
		case OPT_HELP:
			display_help(0); break;
		case OPT_POLICY:
			policy = handlepolicy(optarg); break;
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
	if (clocksel < 0 || clocksel > 3)
		error = 1;

	// check priority boundary
	if (priority < 0 || priority > 99)
		error = 1;

	// check priority and policy match
	if (priority && (policy != SCHED_FIFO && policy != SCHED_RR)) {
		warn("policy and priority don't match: setting policy to SCHED_FIFO\n");
		policy = SCHED_FIFO;
	}

	// check policy with priority match 
	if ((policy == SCHED_FIFO || policy == SCHED_RR) && priority == 0) {
		warn("defaulting realtime priority to %d\n",
		num_threads+1); // TODO: num threads and prio connection??
		priority = num_threads+1;
	}

	// num theads must be > 0 
	if (num_threads < 1)
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

//	(void)printf("Starting main PID: %d\n", getpid()); // TODO: duplicate main pid query?
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
	signal(SIGINT, inthand);
	signal(SIGTERM, inthand);
	signal(SIGUSR1, inthand);

	while (!stop && (t_stat1 > -1 || t_stat2 > -1)) {
		sleep (1);
	}

	// signal shutdown to threads
	if (t_stat1 > -1) // only if not already done
		t_stat1 = -1;
	if (t_stat2 > -1) // only if not already done
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

	restorekernvars(); // restore previous variables
    return 0;
}
