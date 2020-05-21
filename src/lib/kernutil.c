/*
 * Copyright (C) 2019 Florian Hofer <info@florianhofer.it>
 *
 * based on functions from rt-test with the following authors
 *
 * Copyright (C) 2009 Carsten Emde <carsten.emde@osadl.org>
 * Copyright (C) 2010 Clark Williams <williams@redhat.com>
 * Copyright (C) 2015 John Kacur <jkacur@redhat.com>
 *
 * based on functions from cyclictest with the following authors
 * (C) 2008-2009 Clark Williams <williams@redhat.com>
 * (C) 2005-2007 Thomas Gleixner <tglx@linutronix.de>
 *
 * https://github.com/clrkwllms/rt-tests
 */


#include "kernutil.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h> // used for string parsing
#include <unistd.h> // used for POSIX and XOPEN constants
#include <fcntl.h>	// file control, new open/close functions
#include <errno.h>	// error numbers and strings
#include <cpuid.h>	// cpu information
#include <sys/wait.h>		// for waitpid in pipe operations
#include <sys/utsname.h>	// kernel info
#include <wordexp.h>		// for POSIX word expansion

#include <stdarg.h>
#include <sched.h>
#include <linux/sched.h>	// Linux specific scheduling
#include <sys/stat.h>
#include <sys/syscall.h> /* For SYS_gettid definitions */

#include "rt-sched.h"
#include "error.h"		// error and strerr print functions
#include "orchdata.h"
#include "cmnutil.h"	// common definitions and functions

#undef PFX
#define PFX "[rt-utils] "

// Statically allocated prefix
static char debugfileprefix[_POSIX_PATH_MAX];

#if (defined(__i386__) || defined(__x86_64__))
	#define ARCH_HAS_SMI_COUNTER
#endif

#define MSR_SMI_COUNT		0x00000034
#define MSR_SMI_COUNT_MASK	0xFFFFFFFF

#ifdef ARCH_HAS_SMI_COUNTER

/*
 * open_msr_file: open file descriptor of MSR counters
 *
 * Arguments: - CPU number to test for
 *
 * Return value: fd, or error code
 */
int
open_msr_file(int cpu)
{
	int fd;
	char pathname[_POSIX_PATH_MAX];

	/* SMI needs thread affinity */
	sprintf(pathname, "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		warn("%s open failed, try modprobe msr, chown or chmod +r "
		       "/dev/cpu/*/msr, or run as root\n", pathname);

	return fd;
}

/*
 * get_msr: read from MSR
 *
 * Arguments: - MSR CPU file descriptor
 * 			  - offset to read from
 * 			  - address to write value to
 *
 * Return value: 0 on success, 1 on mismatch, error code otherwise
 */
static int
get_msr(int fd, off_t offset, unsigned long long *msr)
{
	ssize_t retval;

	// WARN, does not handle EINTR
	retval = pread(fd, msr, sizeof *msr, offset);

	if (retval != sizeof *msr)
		return 1;

	return 0;
}

/*
 * get_smi_counter: read MSR counter
 *
 * Arguments: - MSR CPU file descriptor
 * 			  - address to write counter to
 *
 * Return value: counter (positive) on success
 * 				 error code (negative) on failure
 */
int
get_smi_counter(int fd, unsigned long *counter)
{
	int retval;
	unsigned long long msr;

	retval = get_msr(fd, MSR_SMI_COUNT, &msr);
	if (retval)
		return retval;

	*counter = (unsigned long) (msr & MSR_SMI_COUNT_MASK);

	return 0;
}

/* Based on turbostat's check */

/*
 * has_smi_counter: check if CPU model has a MSR-SMI counter
 *
 * Arguments: -
 *
 * Return value: 1 if present, 0 otherwise
 */
int
has_smi_counter(void)
{
	unsigned int ebx, ecx, edx, max_level;
	unsigned int fms, family, model;

	fms = family = model = ebx = ecx = edx = 0;

	__get_cpuid(0, &max_level, &ebx, &ecx, &edx);

	/* check genuine intel */
	if (!(ebx == 0x756e6547 && edx == 0x49656e69 && ecx == 0x6c65746e))
		return 0;

	__get_cpuid(1, &fms, &ebx, &ecx, &edx);
	family = (fms >> 8) & 0xf;

	if (family != 6)
		return 0;

	/* no MSR */
	if (!(edx & (1 << 5)))
		return 0;

	model = (((fms >> 16) & 0xf) << 4) + ((fms >> 4) & 0xf);

	switch (model) {
	case 0x1A:      /* Core i7, Xeon 5500 series - Bloomfield, Gainstown NHM-EP */
	case 0x1E:      /* Core i7 and i5 Processor - Clarksfield, Lynnfield, Jasper Forest */
	case 0x1F:      /* Core i7 and i5 Processor - Nehalem */
	case 0x25:      /* Westmere Client - Clarkdale, Arrandale */
	case 0x2C:      /* Westmere EP - Gulftown */
	case 0x2E:      /* Nehalem-EX Xeon - Beckton */
	case 0x2F:      /* Westmere-EX Xeon - Eagleton */
	case 0x2A:      /* SNB */
	case 0x2D:      /* SNB Xeon */
	case 0x3A:      /* IVB */
	case 0x3E:      /* IVB Xeon */
	case 0x3C:      /* HSW */
	case 0x3F:      /* HSX */
	case 0x45:      /* HSW */
	case 0x46:      /* HSW */
	case 0x3D:      /* BDW */
	case 0x47:      /* BDW */
	case 0x4F:      /* BDX */
	case 0x56:      /* BDX-DE */
	case 0x4E:      /* SKL */
	case 0x5E:      /* SKL */
	case 0x8E:      /* KBL */
	case 0x9E:      /* KBL */
	case 0x55:      /* SKX */
	case 0x37:      /* BYT */
	case 0x4D:      /* AVN */
	case 0x4C:      /* AMT */
	case 0x57:      /* PHI */
	case 0x5C:      /* BXT */
		break;
	default:
		return 0;
	}

	return 1;
}
#else
// Dummy functions
static int open_msr_file(int cpu)
{
	return -1;
}

static int get_smi_counter(int fd, unsigned long *counter)
{
	return 1;
}
static int has_smi_counter(void)
{
	return 0;
}
#endif

/*
 *  check_kernel(): check the kernel version,
 *
 *  Arguments: -
 *
 *  Return value: detected kernel version (enum)
 */
int
check_kernel(void)
{
	struct utsname kname;
	int maj, min, sub;
	int kv = KV_NOT_SUPPORTED;

	if (uname(&kname)) {
		err_msg_n (errno, "Assuming not 2.6. uname failed");
		return kv;
	}

	if (3 == sscanf(kname.release, "%d.%d.%d", &maj, &min, &sub)){
		// sscanf successful

		if (3 == maj  && 14 <= min){
			// kernel 3.x standard LT kernel for embedded, EDF implemented
			kv = KV_314;
			// else EDF not implemented

		} else if (4 == maj) {
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
		} else if (5 == maj) {
			// full EDF -PA, newest kernel
			kv = KV_50;
		}
	}
	else
		warn("Error parsing kernel version.");

	return kv;
}

/*
 *  kernvar(): sets/gets a kernel virtual fs parameter, internal
 *
 *  Arguments: - file system prefix -> folder
 *  		   - parameter name to write read
 * 			   - input value
 * 			   - variable buffer size
 *
 *  Return value: return number of written/read chars.
 * 					-1= error and errno is set
 */
static int
kernvar(int mode, const char *prefix, const char *name, char *value, size_t sizeofvalue)
{
	char filename[_POSIX_PATH_MAX];
	int path;

	if (!prefix || !name || !value) {
		errno = EINVAL;
		return -1;
	}

	size_t len_prefix = strlen(prefix), len_name = strlen(name);

	if (len_prefix + len_name + 1 > _POSIX_PATH_MAX) {
		errno = ENOMEM;
		return -1;
	}

	errno = 0; // reset global error number

	memcpy(filename, prefix, len_prefix);
	memcpy(filename + len_prefix, name, len_name + 1);

	// Read and write on kernel /sys and /proc should always be successful and w/o EINTR
	// However, we do not deal with EINTR here, might be source of error
	path = open(filename, mode);
	if (0 <= path) {
		if (O_RDONLY == mode) {
			int got;
			// if = 0, no change
			if ((got = read(path, value, sizeofvalue)) >= 0) {
				value[MAX(got-1, 0)] = '\0';
				close(path);
				return got;
			}
			// if < 0 there is something wrong

		} else if (O_WRONLY == mode)
			if (write(path, value, sizeofvalue) == sizeofvalue){
				close(path);
				return sizeofvalue;
			}
		close(path);
	}
//  errno = ... pass errno from open, read or write
	return -1;  // return number read/written => -1 = error
}

 /*
 *  setkernvar(): sets a kernel virtual fs parameter
 *
 *  Arguments: - filesystem prefix -> folder
 *  		   - parameter name to write
 * 			   - input value
 * 			   - dry run? 1 = do nothing
 *
 *  Return value: return num of written chars.
 * 					-1= error and errno is set
 */
int
setkernvar(const char *prefix, const char *name, char *value, int dryrun)
{
	if (!value) {
		errno = EINVAL;
		return -1;
	}

	if (dryrun) // suppress system changes
		return strlen(value);

	return kernvar(O_WRONLY, prefix, name, value, strlen(value));

}

/*
 *  getkernvar(): reads a kernel virtual fs parameter
 *
 *  Arguments: - filesystem prefix -> folder
 *  		   - parameter name to read
 * 			   - value storage location
 * 			   - storage buffer size
 *
 *  Return value: return num of read chars.
 * 					-1= error and errno is set
 */
int
getkernvar(const char *prefix, const char *name, char *value, int size)
{

	return (kernvar(O_RDONLY, prefix, name, value, size));

}

/*
 *  parse_cpumask(): parses and checks if the CPU bit-mask is OK
 * 		w/ similar to numa_parse_cpustring_all()
 *
 *  Arguments: - pointer to bitmask
 *
 *  Return value: the parsed bitmask, returns null if empty cpuset
 */
struct bitmask *
parse_cpumask(const char *option)
{
	struct bitmask * mask = numa_parse_cpustring_all(option);

	if (mask) {
		// no CPU is set.. :/ Free
		if (0 == numa_bitmask_weight(mask)) {
			numa_bitmask_free(mask);
			mask = NULL;
		}
		else
			printDbg("%s: Using %u cpus.\n", __func__,
					numa_bitmask_weight(mask));
	}

	return mask;
}

/*
 *  parse_bitmask(): returns the matching bitmask string
 *
 *  Arguments: - pointer to bitmask
 *  		   - returning string
 * 			   - size of string
 *
 *  Return value: error code if present
 *
 *  WARN, assume 2 digit CPU numbers, no more than 100 cpus allowed
 */
int
parse_bitmask(struct bitmask *mask, char * str, size_t len){
	// base case check
	if (!mask || !str)
		return -1;
 
	char num[12];
	int mask_sz = numa_bitmask_nbytes(mask) *8;
	int first_found = -1;
	int last_found =-1;

	// init.. use len as counter for remaining space
	str [0] = '\0';
	len--;	// \0 needs 1 byte

	for (int i=0; i<mask_sz; i++){

		if (!len){
			err_msg("String too small for data");
			return -1;
		}

		if (numa_bitmask_isbitset(mask, i)){
			// set bit found

			if (0 > first_found) {
				// first of sequence?

				first_found = i; // found and start range bit number
				len-=2;	// remaining space decreases
				if (!strlen(str))
					// first at all?
					(void)sprintf(str, "%d", first_found);

				else{
					(void)sprintf(num, ",%d", first_found);
					(void)strcat(str, num);
					len--; // 1 decrease more, comma
				}
				last_found = first_found; // end range bit number 
			}
			else 

				// not first in sequence, add to end-range value
				last_found++;
		}
		else {
			// Do we have a range that we scanned.. then add '-last_found'
			if (last_found != first_found){
				// end of 1-bit sequence, print end of range
				(void)sprintf(num, "-%d",last_found);
				(void)strcat(str, num);
				len-=3; // 3 character string
			}
			first_found = last_found = -1; // reset range
		}
	}

	printDbg("Parsed bit-mask: %s\n", str);
	return 0;
}

/*
 *  popen2(): customized pipe open command
 *
 *  Arguments: - command string
 *  		   - read or write (r/w) and non blocking with 'x'
 *  		   - address of pid_t variable to store launched PID
 *
 *  Return value: returns file descriptor
 */

#define READ   0 // pipe position
#define WRITE  1 

FILE *
popen2(const char * command, const char * type, pid_t * pid)
{
    pid_t child_pid;
    int fd[2];
	FILE * pfl;
	if (pipe(fd))
        perror("Pipe creation");

	if (!command || !type || !pid){ // input parameters must be written
        perror("Invalid arguments");
		return NULL;
	}

	int write = (NULL != strstr(type, "w")); // write or read
	int nnblk = (NULL != strstr(type, "x")); // non blocking enabled?

    if(-1 == (child_pid = fork()))
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    /* child process */
    if (child_pid == 0)
    {
        if (write) {
            close(fd[WRITE]);    //Close the WRITE end of the pipe since the child's fd is read-only
            dup2(fd[READ], STDIN_FILENO);   //Redirect stdin to pipe
        }
        else {
            close(fd[READ]);    //Close the READ end of the pipe since the child's fd is write-only
            dup2(fd[WRITE], STDOUT_FILENO); //Redirect stdout to pipe
        }

		if (strstr(command, "&&")){ // Temporary, if we have a && combination, use sh
	        setpgid(child_pid, child_pid); //Needed so negative PIDs can kill children of /bin/sh
		    execl("/bin/sh", "/bin/sh", "-c", command, NULL);
		}
		else {
			// parse string according to POSIX expansion
			wordexp_t argvt;
			if (wordexp(command, &argvt, 0))
				err_exit("Error parsing POSIX 'sh' expansion!");

		    if (execvp(argvt.we_wordv[0], argvt.we_wordv))
				err_msg_n(errno, "Error when executing command");
			wordfree(&argvt);
		}
        exit(0);
    }

    *pid = child_pid;

    if (write){
        close(fd[READ]); //Close the READ end of the pipe since parent's fd is write-only
        pfl = fdopen(fd[WRITE], "w");
	}
    else {
        close(fd[WRITE]); //Close the WRITE end of the pipe since parent's fd is read-only
		pfl = fdopen(fd[READ], "r");
	}

	if (nnblk)
		fcntl(fileno(pfl), F_SETFL, O_NONBLOCK); // Set pipe to non-blocking

	return pfl;
}

/*
 *  pclose2(): customized pipe close command
 *
 *  Arguments: - pipe file descriptor
 *  		   - associated process PID
 *  		   - (optional) signal to send to task before closing pipe
 *
 *  Return value: returns last known PID status (waitpid), -1 for error
 */
int
pclose2(FILE * fp, pid_t pid, int killsig)
{
    int stat;

	if (killsig)
		kill(pid, killsig);

    fclose(fp);

    while (waitpid(pid, &stat, 0) == -1)
    {
        if (errno != EINTR)
        {
            stat = -1;
            break;
        }
    }

    return stat;
}

/*
 * get_debugfileprefix : Finds the tracing directory in a mounted debugfs
 *
 * Arguments: -
 *
 * Return Value: pointer to null-terminated string containing the path
 * 				Returns \0 on failure
 */
char *
get_debugfileprefix(void)
{
	char type[100];
	FILE *fp;
	int size;
	int found = 0;
	struct stat s;

	if (debugfileprefix[0] != '\0')
		goto out;

	/* look in the "standard" mount point first */
	if ((stat("/sys/kernel/debug/tracing", &s) == 0) && S_ISDIR(s.st_mode)) {
		strcpy(debugfileprefix, "/sys/kernel/debug/tracing/");
		goto out;
	}

	/* now look in the "other standard" place */
	if ((stat("/debug/tracing", &s) == 0) && S_ISDIR(s.st_mode)) {
		strcpy(debugfileprefix, "/debug/tracing/");
		goto out;
	}

	/* oh well, parse /proc/mounts and see if it's there */
	if ((fp = fopen("/proc/mounts", "r")) == NULL)
		goto out;

	while (fscanf(fp, "%*s %"
		      STR(_POSIX_PATH_MAX)
		      "s %99s %*s %*d %*d\n",
		      debugfileprefix, type) == 2) {
		if (strcmp(type, "debugfs") == 0) {
			found = 1;
			break;
		}
		/* stupid check for systemd-style autofs mount */
		if ((strcmp(debugfileprefix, "/sys/kernel/debug") == 0) &&
		    (strcmp(type, "systemd") == 0)) {
			found = 1;
			break;
		}
	}
	fclose(fp);

	if (!found) {
		debugfileprefix[0] = '\0';
		goto out;
	}

	size = sizeof(debugfileprefix) - strlen(debugfileprefix);
	strncat(debugfileprefix, "/tracing/", size);

out:
	return debugfileprefix;
}

/*
 * setevent: enable event trace-point for ftrace
 *
 * Arguments: - relative path to event, ex. 'events/sched_switch/enable'
 *
 * Return value: return num of written chars.
 * 					-1= error and errno is set
 */
static int
setevent(char *event, char *val)
{
	char *prefix = get_debugfileprefix();

	if (!event || !val) // null pointers
		return -1;

	printDbg(PFX "Setting event for tracer '%s' to '%s'\n", event, val);

	return setkernvar(prefix, event, val, 0); // no dryrun here. lib does not know about it
}

/*
 * event_enable_all: enable all trace events
 *
 * Arguments: -
 *
 * Return value: return num of written chars.
 * 					-1= error and errno is set
 */
int
event_enable_all(void)
{
	return setevent("events/enable", "1");
}

/*
 * event_disable_all: disable all trace events
 *
 * Arguments: -
 *
 * Return value: return num of written chars.
 * 					-1= error and errno is set
 */
int
event_disable_all(void)
{
	return setevent("events/enable", "0");
}

/*
 * event_getid: read out event value, id
 *
 * Arguments: - name of the event to access
 *
 * Return value: return num of read chars.
 * 					-1= error and errno is set
 */
int
event_getid(char *event)
{
	if (!event) // null pointers
		return -1;

	char *prefix = get_debugfileprefix();
	char path[_POSIX_PATH_MAX];
	char val[5];

	(void)sprintf(path, "events/%s/id", event);
	if (getkernvar(prefix, event, val, 5) > 0)
		return atoi(val);

	return -1;
}

/*
 * event_enable: enable trace event
 *
 * Arguments: - name of the event to access
 *
 * Return value: return num of written chars.
 * 					-1= error and errno is set
 */
int
event_enable(char *event)
{
	char path[_POSIX_PATH_MAX];

	sprintf(path, "events/%s/enable", event);
	return setevent(path, "1");
}

/*
 * event_disable: disable  trace event
 *
 * Arguments: - name of the event to access
 *
 * Return value: return num of written chars.
 * 					-1= error and errno is set
 */
int
event_disable(char *event)
{
	char path[_POSIX_PATH_MAX];

	sprintf(path, "events/%s/enable", event);
	return setevent(path, "0");
}

/*
 *  policy_to_string(): change policy code to string value
 *
 *  Arguments: - scheduling enumeration constant (int) identifying policy
 *
 *  Return value: returns a string identifying the scheduler
 */
const char *
policy_to_string(int policy)
{
	switch (policy) {
	case SCHED_OTHER:
		return "SCHED_OTHER";
	case SCHED_FIFO:
		return "SCHED_FIFO";
	case SCHED_RR:
		return "SCHED_RR";
	case SCHED_BATCH:
		return "SCHED_BATCH";
	case SCHED_IDLE:
		return "SCHED_IDLE";
	case SCHED_DEADLINE:
		return "SCHED_DEADLINE";
	}

	return "-UNKNOWN-";
}

/*
 *  policy_is_realtime(): verify if given policy is a real-time policy
 *
 *  Arguments: - scheduling enumeration constant (int) identifying policy
 *
 *  Return value: returns 1 if real-time, 0 otherwise
 */
const int
policy_is_realtime(int policy)
{
	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
	case SCHED_DEADLINE:
		return 1;
	case SCHED_OTHER:
	case SCHED_BATCH:
	case SCHED_IDLE:
	default:
		return 0;
	}

	return 0;
}

/*
 *  string_to_policy(): match string with a scheduling policy
 *
 *  Arguments: - string identifying policy
 *
 *  Return value: returns scheduling enumeration constant, -1 if failed
 *
 *  WARN! NO NULL-CHECK ON POLICY_NAME!!
 */
int
string_to_policy(const char *policy_name, uint32_t *policy)
{
	if (strcmp(policy_name, "SCHED_OTHER") == 0)
		*policy = SCHED_OTHER;
	else if (strcmp(policy_name, "SCHED_IDLE") == 0)
		*policy = SCHED_IDLE;
	else if (strcmp(policy_name, "SCHED_BATCH") == 0)
		*policy = SCHED_BATCH;
	else if (strcmp(policy_name, "SCHED_RR") == 0)
		*policy =  SCHED_RR;
	else if (strcmp(policy_name, "SCHED_FIFO") == 0)
		*policy =  SCHED_FIFO;
	else if (strcmp(policy_name, "SCHED_DEADLINE") == 0)
		*policy =  SCHED_DEADLINE;
	else if (strcmp(policy_name, "default") == 0) // No change to program settings
		*policy =  SCHED_NODATA;
	else
		return -1;
	return 0;
}

/*
 *  string_to_affinity(): match string with a affinity policy
 *
 *  Arguments: - string identifying affinity
 *
 *  Return value: returns affinity enumeration constant, 0 (other) if failed
 */
uint32_t
string_to_affinity(const char *str)
{
	if (!strcmp(str, "unspecified"))
		return AFFINITY_UNSPECIFIED;
	else if (!strcmp(str, "specified"))
		return AFFINITY_SPECIFIED;
	else if (!strcmp(str, "useall"))
		return AFFINITY_USEALL;
	warn("Unrecognized value '%s' for affinity setting", str);
	return 0; // default to other
}

