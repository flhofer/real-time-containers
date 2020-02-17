#include "kernutil.h"

#include <stdio.h>
#include <string.h> // used for string parsing
#include <unistd.h> // used for POSIX and XOPEN constants
#include <fcntl.h>	// file control, new open/close functions
#include <errno.h>	// error numbers and strings
#include <cpuid.h>	// cpu information
#include <sys/wait.h>		// for waitpid in pipe operations
#include <sys/utsname.h>	// kernel info
#include <wordexp.h>	// for posix word expansion

#include "error.h"		// error and strerr print functions

#if (defined(__i386__) || defined(__x86_64__))
	#define ARCH_HAS_SMI_COUNTER
#endif

#define MSR_SMI_COUNT		0x00000034
#define MSR_SMI_COUNT_MASK	0xFFFFFFFF

#ifdef ARCH_HAS_SMI_COUNTER
int open_msr_file(int cpu)
{
	int fd;
	char pathname[24]; // path + 10 max for number + \0

	/* SMI needs thread affinity */
	sprintf(pathname, "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		warn("%s open failed, try modprobe msr, chown or chmod +r "
		       "/dev/cpu/*/msr, or run as root\n", pathname);

	return fd;
}

static int get_msr(int fd, off_t offset, unsigned long long *msr)
{
	ssize_t retval;

	retval = pread(fd, msr, sizeof *msr, offset);

	if (retval != sizeof *msr)
		return 1;

	return 0;
}

int get_smi_counter(int fd, unsigned long *counter)
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
int has_smi_counter(void)
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


/// check_kernel(): check the kernel version,
///
/// Arguments: - 
///
/// Return value: detected kernel version (enum)
int check_kernel(void)
{
	struct utsname kname;
	int maj, min, sub, kv;

	if (uname(&kname)) {
		err_msg_n (errno, "Assuming not 2.6. uname failed");
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


/// kernvar(): sets/gets a kernel virtual fs parameter, internal
///
/// Arguments: - filesystem prefix -> folder
/// 		   - parameter name to write read
///			   - input value
///			   - variable buffer size
///
/// Return value: return num of written/read chars.
///					-1= error and errno is set
///
static int kernvar(int mode, const char *prefix, const char *name, char *value, size_t sizeofvalue)
{
	char filename[128];
	int path;

	if (!prefix || !name || !value) {
		errno = EINVAL;
		return -1;
	}

	size_t len_prefix = strlen(prefix), len_name = strlen(name);

	if (len_prefix + len_name + 1 > sizeof(filename)) {
		errno = ENOMEM;
		return -1;
	}

	errno = 0; // reset global error number

	memcpy(filename, prefix, len_prefix);
	memcpy(filename + len_prefix, name, len_name + 1);

	path = open(filename, mode);
	if (0 <= path) {
		if (O_RDONLY == mode) {
			int got;
			// if = 0, no change
			if ((got = read(path, value, sizeofvalue)) > 0) {
				value[got-1] = '\0';
				close(path);
				return got;
			}
		} else if (O_WRONLY == mode) {
			if (write(path, value, sizeofvalue) == sizeofvalue){
				close(path);
				return sizeofvalue;				
			}
			
		}
		close(path);
	}
//  errno = ... pass errno from open, read or write
	return -1;  // return number read/written => -1 = error
}

/// setkernvar(): sets a kernel virtual fs parameter
///
/// Arguments: - filesystem prefix -> folder
/// 		   - parameter name to write
///			   - input value
///			   - dry run? 1 = do nothing
///
/// Return value: return num of written chars.
///					-1= error and errno is set
///
int setkernvar(const char *prefix, const char *name, char *value, int dryrun)
{
	if (!value) {
		errno = EINVAL;
		return -1;
	}

	if (dryrun) // suppress system changes
		return strlen(value);

	return kernvar(O_WRONLY, prefix, name, value, strlen(value));

}

/// getkernvar(): reads a kernel virtual fs parameter
///
/// Arguments: - filesystem prefix -> folder
/// 		   - parameter name to read
///			   - value storage location
///			   - storage buffer size
///
/// Return value: return num of read chars.
///					-1= error and errno is set
///
int getkernvar(const char *prefix, const char *name, char *value, int size)
{

	return (kernvar(O_RDONLY, prefix, name, value, size));

}

/// parse_cpumask(): parses and checks if the CPU bit-mask is OK
///		w/ similar to numa_parse_cpustring_all()
///
/// Arguments: - pointer to bitmask
///
/// Return value: the parsed bitmask, returns null if empty cpuset
///
struct bitmask *parse_cpumask(const char *option)
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

/// parse_bitmask(): returns the matching bitmask string
///
/// Arguments: - pointer to bitmask
/// 		   - returning string
///
/// Return value: error code if present
///
// TODO: size of string!
int parse_bitmask(struct bitmask *mask, char * str){
	// base case check
	if (!mask || !str)
		return -1;
 
	int sz= numa_bitmask_nbytes(mask) *8,rg =-1,fd = -1;
	str [0] = '\0';

	for (int i=0; i<sz; i++){
		if (numa_bitmask_isbitset(mask, i)){
			// set bit found

			if (fd<0) {
				// first of sequence?

				fd = i; // found and start range bit number
				if (!strlen(str))
					// first at all?
					sprintf(str, "%d", fd);
				else
					sprintf(str, "%s,%d", str,fd);
				rg = fd; // end range bit number 
			}
			else 
				// not first in sequence, add to end-range value
				rg++;
		}
		else {
			if (rg != fd)
				// end of 1-bit sequence, print end of range
				sprintf(str, "%s-%d", str,rg);
			fd = rg = -1; // reset range
		}
	}
	printDbg("Parsed bitmask: %s\n", str);
	return 0;
}

/// popen2(): customized pipe open command
///
/// Arguments: - command string
/// 		   - read or write (r/w) and non blocking with 'x'
/// 		   - address of pid_t variable to store launched PID
///
/// Return value: returns file descriptor
///

#define READ   0 // pipe position
#define WRITE  1 

FILE * popen2(const char * command, const char * type, pid_t * pid)
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

		// TODO: fix this
		if (strstr(command, "&&")){ // temporay, if we have a && combination, use sh
	        setpgid(child_pid, child_pid); //Needed so negative PIDs can kill children of /bin/sh
		    execl("/bin/sh", "/bin/sh", "-c", command, NULL);
		}
		else {
			// parse string according to posix expansion
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

/// pclose2(): customized pipe close command
///
/// Arguments: - pipe file descriptor
/// 		   - associated process PID
/// 		   - (optional) signal to send to task before closing pipe
///
/// Return value: returns last known PID status (waitpid), -1 for error
///
int pclose2(FILE * fp, pid_t pid, int killsig)
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
