#include "schedstat.h" // main settings and header file

#include "update.h"
#include "manage.h"

#include <fcntl.h> 
#include <sys/utsname.h>

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
	KV_30
};


static int check_kernel(void)
{
	struct utsname kname;
	int maj, min, sub, kv, ret;

	ret = uname(&kname);
	if (ret) {
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
		else if (sub < 28) {
			kv = KV_26_33;
			// toto if 
		} else {
			kv = KV_26_33;
			// toto if 
		}
	} else if (maj >= 3) {
		kv = KV_30;
		// toto if 

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

static void setkernvar(const char *name, char *value)
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
	if (kernvar(O_WRONLY, name, value, strlen(value)))
		printDbg(KRED "Error!" KNRM " could not set %s to %s\n", name, value);

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
/// Return value: 
///
void prepareEnvironment() {

	
	kernelversion = check_kernel();
	fileprefix = procfileprefix; // set working prefix for vfs

	if (kernelversion == KV_NOT_SUPPORTED)
		printDbg( KMAG "Warn!" KNRM " Running on unknown kernel version...YMMV\n");

	// Kernel RT-bandwidth management must be disabled to allow deadline+affinity

	// disable bandwidth control and realtime throttle
	setkernvar("sched_rt_runtime_us", "-1");

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

	// TODO: ADD check for SYS_NICE
	// TODO: ADD check for task prio

	// gather actual information at startup, prepare environment
	prepareEnvironment();

	pthread_t thread1, thread2;
	int32_t t_stat1 = 0; // we control thread status 32bit to be sure read is atomic on 32 bit -> sm on treads
	int32_t t_stat2 = 0; 
	int  iret1, iret2;

	/* Create independent threads each of which will execute function */
	iret1 = pthread_create( &thread1, NULL, thread_manage, (void*) &t_stat1);
	iret2 = pthread_create( &thread2, NULL, thread_update, (void*) &t_stat2);
	// TODO: set thread prio and sched to RR -> maybe 

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
