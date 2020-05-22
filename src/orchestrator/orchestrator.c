// main settings and header file
#include "orchestrator.h"

// header files of launched threads
#include "prepare.h" // environment and configuration
#include "update.h"
#include "manage.h"
#include "adaptive.h"

// Default stuff, needed form main operation
#include <stdio.h>
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants
#include <signal.h> // for SIGs, handling in main, raise in update

// Custom includes
#include "parse_config.h"	// header file of configuration parser
#include "kernutil.h"		// generic kernel utilities
#include "error.h"			// error and std error print functions

// Things that should be needed only here
#include <sys/mman.h>		// memory lock
#include <getopt.h>			// command line parsing

/* --------------------------- Global variables for all the threads and programs ------------------ */

containers_t * contparm; // container parameter settings
prgset_t * prgset; // program settings structure

#ifdef DEBUG
// debug output file
	FILE  * dbg_out;
#endif
// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;
// head of pidlist - PID runtime and configuration details
node_t * nhead = NULL;

// configuration read file
static char * config = "config.json";

// -------------- LOCAL variables for all the functions  ------------------

static int adaptive = 0;

// signal to keep status of triggers ext SIG
static volatile sig_atomic_t main_stop;

/* -------------------------------------------- DECLARATION END ---- CODE BEGIN -------------------- */


/// inthand(): interrupt handler for infinite while loop, help 
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: -
static void inthand (int sig, siginfo_t *siginfo, void *context){
	main_stop = 1;
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
	       "-A                         activate Adaptive Static Schedule (ASS)\n"
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
	OPT_INTERVAL, OPT_LOOPS, OPT_MLOCKALL,
	OPT_NSECS, OPT_NUMA, OPT_PRIORITY, OPT_QUIET, 
	OPT_RRTIME, OPT_RTIME, OPT_SMI, OPT_VERBOSE,
	OPT_WCET, OPT_POLICY, OPT_HELP, OPT_VERSION
};

/// process_options(): Process commandline options 
///
/// Arguments: - structure with parameter set
///			   - passed command line variables
///			   - number of CPUs
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
		/*
		 * Options for getopt
		 * Ordered alphabetically by single letter name
		 */
		static struct option long_options[] = {
			{"affinity",         required_argument, NULL, OPT_AFFINITY},
			{"bind",     		 no_argument,       NULL, OPT_BIND },
			{"clock",            required_argument, NULL, OPT_CLOCK },
			{"dflag",            no_argument,		NULL, OPT_DFLAG },
			{"interval",         required_argument, NULL, OPT_INTERVAL },
			{"loops",            required_argument, NULL, OPT_LOOPS },
			{"mlockall",         no_argument,       NULL, OPT_MLOCKALL },
			{"priority",         required_argument, NULL, OPT_PRIORITY },
			{"quiet",            no_argument,       NULL, OPT_QUIET },
			{"runtime",          required_argument, NULL, OPT_RTIME },
			{"rr",               required_argument, NULL, OPT_RRTIME },
			{"numa",             no_argument,       NULL, OPT_NUMA },
			{"smi",              no_argument,       NULL, OPT_SMI },
			{"version",			 no_argument,		NULL, OPT_VERSION},
			{"verbose",          no_argument,       NULL, OPT_VERBOSE },
			{"policy",           required_argument, NULL, OPT_POLICY },
			{"wcet",             required_argument, NULL, OPT_WCET },
			{"help",             no_argument,       NULL, OPT_HELP },
			{NULL, 0, NULL, 0}
		};
		int c = getopt_long(argc, argv, "a:Abc:C:dDfhi:kl:mn::p:Pqr:s:vw:",
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
				set->affinity = malloc(14);
				if (!set->affinity){
					err_msg("could not allocate memory!");
					exit(EXIT_FAILURE);
				}
				sprintf(set->affinity, "0-%d", max_cpus-1);
				set->setaffinity = AFFINITY_USEALL;
			}
			break;
		case 'A':
			adaptive = 1;
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
			if (optarg == NULL
				|| string_to_policy(optarg, &set->policy) == 0)
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

	if (set->smi) { // verify this statements, I just put them all
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

	{ // pre-processing and configuration readout
		prgset_t *tmpset;
		if (!(tmpset = malloc (sizeof(prgset_t))))
			err_exit("Unable to allocate memory");

		process_options(tmpset, argc, argv, max_cpus);

		// gather actual information at startup, prepare environment
		if (prepareEnvironment(tmpset))
			display_help(1); // if it returns with error code, display help

		// NOW all is public!
		prgset = tmpset;
	}

	if (adaptive){
		// adaptive scheduling active? Clean prepare, execute, free
		adaptPrepareSchedule();
		adaptExecute();
		adaptFreeTracer(); // free as we don't need it for the rest of the process
	}

	pthread_t thrManage, thrUpdate;
	int32_t t_stat1 = 0; // we control thread status 32bit to be sure read is atomic on 32 bit -> sm on treads
	int32_t t_stat2 = 0; 
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */ 
	if ((iret1 = pthread_create( &thrManage, NULL, thread_manage, (void*) &t_stat1))) {
		err_msg_n (iret1, "could not start update thread");
		t_stat1 = -1;
	}

	if ((iret2 = pthread_create( &thrUpdate, NULL, thread_update, (void*) &t_stat2))) {
		err_msg_n (iret2, "could not start management thread");
		t_stat2 = -1;
	}
#ifdef DEBUG
	(void)pthread_setname_np(thrManage, "manage");
	(void)pthread_setname_np(thrUpdate, "update");
#endif

	{ // setup interrupt handler block
		struct sigaction act;

		/* Use the sa_sigaction field because the handles has two additional parameters */
		/* The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler. */
		act.sa_handler = NULL; // On some architectures ---
		act.sa_sigaction = &inthand; // these are a union, do not assign both, -> first set null, then value
		act.sa_flags = SA_SIGINFO;

		/* blocking signal set during handler */
		sigemptyset(&act.sa_mask);
		sigaddset(&act.sa_mask, SIGINT);
		sigaddset(&act.sa_mask, SIGTERM);
		sigaddset(&act.sa_mask, SIGQUIT);
		sigaddset(&act.sa_mask, SIGHUP);

		act.sa_restorer = NULL;

		if ((sigaction(SIGINT, &act, NULL) < 0)		 // CTRL+C
			|| (sigaction(SIGTERM, &act, NULL) < 0)) // KILL termination or end of test
		{
			perror ("Setup of sigaction failed");
			exit(EXIT_FAILURE); // exit the software, not working
		}
	} // END interrupt handler block
	{ // signal blocking set

		 sigset_t set;
	   /* Block SIGQUIT and SIGHUP; other threads created by main()
		  will inherit a copy of the signal mask. */

	   // allow all to be handled by Main, except
	   if ( ((sigemptyset(&set)))
			   || ((sigaddset(&set, SIGQUIT)))	// used in manage thread
			   || ((sigaddset(&set, SIGHUP)))	// used in docker_link
			   || (0 != pthread_sigmask(SIG_BLOCK, &set, NULL))){
		   // INT signal, stop from main prg
			perror ("Setup of sigmask failed");
			exit(EXIT_FAILURE); // exit the software, not working
		}
	}

	// infinite loop until stop
	while (!main_stop && (t_stat1 > -1 || t_stat2 > -1)) {
		sleep (1);
	}

	// signal shutdown to threads if not set
	if (-1 < t_stat1) // only if not already done internally
		t_stat1 = -1;
	if (-1 < t_stat2) // only if not already done internally 
		t_stat2 = -1;

	// wait until threads have stopped
	if (!iret1) // thread started successfully
		iret1 = pthread_join( thrManage, NULL);
	if (!iret2)	// thread started successfully
		iret2 = pthread_join( thrUpdate, NULL); 

    // close and cleanup
    info("exiting safely");
    cleanupEnvironment(prgset);

    return EXIT_SUCCESS;
}
