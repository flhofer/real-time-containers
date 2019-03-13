#include "schedstat.h" // main settings and header file

#include "update.h"
#include "manage.h"

#include <fcntl.h> 
#include <sys/utsname.h>
#include <sys/capability.h>


// Global variables for all the threads and programms

// signal to keep status of triggers ext SIG
volatile sig_atomic_t stop;
// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;

// head of pidlist
node_t * head = NULL;

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

static char *procfileprefix = "/proc/sys/kernel/";
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
int prepareEnvironment() {

	/// prerequisites

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
		return errno;
	}

	/// --------------------
	/// Kernel variables, disable bandwidth management and RT-throttle
	/// Kernel RT-bandwidth management must be disabled to allow deadline+affinity
	kernelversion = check_kernel();
	fileprefix = procfileprefix; // set working prefix for vfs

	if (kernelversion == KV_NOT_SUPPORTED)
		printDbg( KMAG "Warn!" KNRM " Running on unknown kernel version...YMMV\nTrying generic configuration..");

	printDbg( "Info: Set realtime bandwith limit to (unconstrained)..\n");
	// disable bandwidth control and realtime throttle
	if (setkernvar("sched_rt_runtime_us", "-1")){
		printDbg( KMAG "Warn!" KNRM " RT-throttle still enabled. Limitations apply.\n");
	}

	/// running settings for scheduler


	return 0;
}

/// configureThreads(): configures running parameters for orchestrator's threads
///
/// Arguments: thread to manage
///
/// Return value: errors
///
int configureThreads(pthread_t * thread) {


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
	
	printDbg("Starting main PID: %d\n", getpid());
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
