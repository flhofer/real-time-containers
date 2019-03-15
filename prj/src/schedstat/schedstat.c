#include "schedstat.h" // main settings and header file

#include "update.h"
#include "manage.h"

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
///
void inthand ( int signum ) {
	stop = 1;
}


/// Kernel variable management (ct extract)

#define KVARS			32
#define KVARNAMELEN		32
#define KVALUELEN		32

static int kernelversion;

static char *fileprefix;

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
				printDbg(KRED "Error!" KNRM " could not backup %s (%s)\n",
					name, oldvalue);
		}
	}
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
			else
				printDbg( KMAG "Warn!" KNRM " Can not set cpu system group\n");

			printDbg( "... moving tasks..\n");

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
							printDbg( KMAG "Warn!" KNRM " Can not move task %s\n", pid);
						}
						pid = strtok (NULL,"\n");	

					}
					
				}

				close(path);
			}

			// free string buffers
			if (fileprefix)
				free (fileprefix);

			if (nfileprefix)
				free (nfileprefix);
		}

	}

	return 0;
}

/// configureThreads(): configures running parameters for orchestrator's threads
///
/// Arguments: thread to manage
///
/// Return value: errors
///
static int configureThreads(pthread_t * thread) {

	// TODO: add thread configuration, RR <= 1ms? 
	// for now, keep em standard free-run, inherited from main process 
	
	return 0;
}


/// main(): mein program.. setup threads and keep loop for user/system break
///
/// Arguments: - Argument values not defined yet
///
/// Return value: Exit code - 0 for no error - EXIT_SUCCESS
int main(int argc, char **argv)
{
	
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

	// TODO: set thread prio and sched to RR -> maybe 
	(void) configureThreads (&thread1);
	(void) configureThreads (&thread2);

	// set interrupt sig hand
	signal(SIGINT, inthand);

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

	restorekernvars(); // restore previous variables
    return 0;
}
