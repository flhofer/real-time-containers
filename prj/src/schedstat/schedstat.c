// main settings and header file
#include "schedstat.h" 

// TODO: fix free allocation release

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
#include <cpuid.h>			// cpu information


#if (defined(__i386__) || defined(__x86_64__))
#define ARCH_HAS_SMI_COUNTER
#endif

#define MSR_SMI_COUNT		0x00000034
#define MSR_SMI_COUNT_MASK	0xFFFFFFFF

static void display_help(int); // declaration for compat

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

// parameters
int priority=0;				// priority parameter for FIFO and RR
int clocksel = 0;			// selected clock 
int policy = SCHED_OTHER;	// default policy if not specified
int quiet = 0;				// quiet enabled
int affother = 0;			// set affinity of parent as well
int setdflag = 0;			// set deadline overrun flag
int interval = TSCAN;		// scan interval
int update_wcet = TWCET;	// wcet for sched deadline
int loops = TDETM;			// determinism
int runtime = 0;			// total orchestrator runtime, 0 is infinite
int psigscan = 0;			// scan for child threads, -n option only
struct bitmask *affinity_mask = NULL; // default bitmask allocation of threads!!
char *cpusetdfileprefix = NULL; // file prefix for Docker's Cgroups, default = [CGROUP/]docker/
int trackpids = 0;			// keep track of left pids, do not delete from list
//int negiszero = 0;

static char *fileprefix; // Work variable for local things -> procfs & sysfs
static unsigned long * smi_counter = NULL; // points to the list of SMI-counters
static int * smi_msr_fd = NULL; // points to file descriptors for MSR readout
static char * cont_cgrp = CONT_DCKR; // CGroup subdirectory configuration for container detection
static int dryrun = 0; // test only, no changes to environment

/* -------------------------------------------- DECLARATION END ---- CODE BEGIN -------------------- */

/// inthand(): interrupt handler for infinite while loop, help 
/// this function is called from outside, interrupt handling routine
/// Arguments: - signal number of interrupt calling
///
/// Return value: -
void inthand ( int signum ) {
	stop = 1;
}

#ifdef ARCH_HAS_SMI_COUNTER
static int open_msr_file(int cpu)
{
	int fd;
	char pathname[32];

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

static int get_smi_counter(int fd, unsigned long *counter)
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
static int has_smi_counter(void)
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
static int check_kernel(void)
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
	if (dryrun) // suppress system changes
		return 0;

	if (kernvar(O_WRONLY, name, value, strlen(value))){
		printDbg(KRED "Error!" KNRM " could not set %s to %s\n", name, value);
		return -1;
	}
	
	return 0;

}

// FIXME: WARN, can only be used one at a time. Main OR manage
int setkernvar_ex(const char *prefix, const char *name, char *value)
{
	// FIXME: temporary, until kernel functions are changed
	fileprefix = prefix;

	return setkernvar(name, value);

}

static int getkernvar(const char *name, char *value, int size)
{

	if (kernvar(O_RDONLY, name, value, size)){
		printDbg(KRED "Error!" KNRM " could not get %s\n", name);
		return -1;
	}
	
	return 0;

}

// -------------- LOCAL variables for all the threads and programms ------------------

static int lockall = 0;
static int numa = 0;
static int force = 0;
static int smi = 0;
static int rrtime = 0;

// TODO:  implement fifo thread as in cycictest for readout
static int use_fifo = 0;
//static pthread_t fifo_threadid;
static char fifopath[MAX_PATH];

static int num_threads = 1; // TODO: extend number of scan threads??
static int smp = 0;  // TODO: add special configurations for SMP modes

enum {
	AFFINITY_UNSPECIFIED,	// use default settings
	AFFINITY_SPECIFIED,	 	// user defined settings
	AFFINITY_USEALL			// go for all!!
};

static int setaffinity = AFFINITY_UNSPECIFIED;
static char * affinity = NULL; // default split, 0-0 SYS, Syscpus to end rest

/// parse_cpumask(): checks if the cpu bitmask is ok
///
/// Arguments: - pointer to bitmask
/// 		   - max number of cpus
///
/// Return value: -
///
static void parse_cpumask(const char *option, const int max_cpus)
{
	affinity_mask = numa_parse_cpustring_all(option);
	if (affinity_mask) {
		if (0==numa_bitmask_weight(affinity_mask)) {
			numa_bitmask_free(affinity_mask);
			affinity_mask = NULL;
		}
	}
	if (!affinity_mask)
		display_help(1);

	if (verbose) {
		info("%s: Using %u cpus.\n", __func__,
			numa_bitmask_weight(affinity_mask));
	}
}

/// parse_bitmask(): returns the matching bitmask string
///
/// Arguments: - pointer to bitmask
/// 		   - returning string
///
/// Return value: error code if present
///
static int parse_bitmask(struct bitmask *mask, char * str){
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

/// prepareEnvironment(): gets the list of active pids at startup, sets up
/// a CPU-shield if not present, prepares kernel settings for DL operation
/// and populates initial state of pid list
///
/// Arguments: 
///
/// Return value: error code if present
///
static int prepareEnvironment() {

	/// --------------------
	/// verify 	cpu topology and distribution
	int maxcpu = get_nprocs();	
	int maxccpu = numa_num_configured_cpus(); //get_nprocs_conf();	

	fileprefix = cpusystemfileprefix;
	char cpus[10]; // cpu allocation string
	char str[100]; // generic string... 

	info("This system has %d processors configured and "
        "%d processors available.\n",
        maxccpu, maxcpu);

	if (numa_available()){
		err_msg( "NUMA is not available but mandatory for the orchestration\n");		
		return -1;
	}

	// verify if SMT is disabled -> now force = disable, TODO: may change to disable only concerned cores
	if (!getkernvar("smt/control", str, sizeof(str))){
		// value read ok
		if (!strcmp(str, "on")) {
			// SMT - HT is on
			if (!force) {
				err_msg("SMT is enabled. Set -f (focre) flag to authorize disabling\n");
				return -1;
			}
			if (setkernvar("smt/control", "off")){
				err_msg("SMT is enabled. Disabling was unsuccessful!\n");
				return -1;
			}
			cont("SMT is now disabled, as required. Refresh configurations..\n");
			sleep(1); // leave time to refresh conf buffers -> immediate query fails
			maxcpu = get_nprocs();	// update
		}
		else
			cont("SMT is disabled, as required\n");
	}

	// no mask specified, generate default
	if (AFFINITY_UNSPECIFIED == setaffinity){
		affinity = malloc(10);
		sprintf(affinity, "%d-%d", SYSCPUS+1, maxcpu-1);
		info("using default setting, affinity '%d' and '%s'.\n", SYSCPUS, affinity);
	}
	// prepare bitmask, no need to do it before
	parse_cpumask(affinity, maxccpu);

	smi_counter = calloc (sizeof(long), maxccpu);
	smi_msr_fd = calloc (sizeof(int), maxccpu);

	struct bitmask * con;
	struct bitmask * naffinity = numa_bitmask_alloc((maxccpu/sizeof(long)+1)*sizeof(long)); 

	// get online cpu's
	if (!getkernvar("online", str, sizeof(str)))
		con = numa_parse_cpustring_all(str);

	// mask affinity and invert for system map / readout of smi of online CPUs
	for (int i=0;i<maxccpu;i++) {

		if (numa_bitmask_isbitset(con, i)){ // filter by online/existing

			char fstring[50]; // cpu string
			char poss[50]; // cpu string

			// verify if cpu-freq is on performance -> set it
			(void)sprintf(fstring, "cpu%d/cpufreq/scaling_available_governors", i);
			if (!getkernvar(fstring, poss, sizeof(poss))){
				// value possible read ok
				(void)sprintf(fstring, "cpu%d/cpufreq/scaling_governor", i);
				if (!getkernvar(fstring, str, sizeof(str))){
					// value act read ok
					if (strcmp(str, CPUGOVR)) {
						// SMT - HT is on
						cont("Possible CPU-freq scaling governors \"%s\" on CPU%d.\n", poss, i);
						if (!force) {
							err_msg("CPU-freq is set to \"%s\" on CPU%d. Set -f (focre) flag to authorize change to \"" CPUGOVR "\"\n", str, i);
							return -1;
							}

						if (setkernvar(fstring, CPUGOVR)){
							err_msg("CPU-freq change unsuccessful!\n");
							return -1;
						}
						cont("CPU-freq on CPU%d is now set to \"" CPUGOVR "\" as required\n", i);
					}
					else
						cont("CPU-freq on CPU%d is set to \"" CPUGOVR "\" as required\n", i);
				}
			}

			// TODO: cpu-idle

			// if smi is set, read SMI counter
			if(smi) {
				*(smi_msr_fd+i) = open_msr_file(i);
				if (*(smi_msr_fd+i) < 0)
					fatal("Could not open MSR interface, errno: %d\n",
						errno);
				// get current smi count to use as base value 
				if (get_smi_counter(*smi_msr_fd+i, smi_counter+i))
					fatal("Could not read SMI counter, errno: %d\n",
						0, errno);
			}

			// invert affinity for avaliable cpus only -> for system
			if (!numa_bitmask_isbitset(affinity_mask, i))
				numa_bitmask_setbit(naffinity, i);
		}

		// if CPU not online
		else if (numa_bitmask_isbitset(affinity_mask, i)) {
			// disabled processor set to affinity
			err_msg("Unavailable CPU set for affinity.\n");
			return -1;
		}

	}

	// parse to string	
	if (parse_bitmask (naffinity, cpus)){
		err_msg ("can not determine inverse affinity mask!\n");
		return -1;
	}

	/// --------------------
	/// verify executable permissions	
	info( "Verifying for process capabilities..\n");
	cap_t cap = cap_get_proc(); // get capability map of proc
	if (!cap) {
		err_msg_n(errno, "Can not get capability map");
		return errno;
	}
	cap_flag_value_t v = 0; // flag to store return value
	if (cap_get_flag(cap, CAP_SYS_NICE, CAP_EFFECTIVE, &v)) {// check for effective NICE cap
		err_msg_n(errno, "Capability test failed");
		return errno;
	}

	if (!CAP_IS_SUPPORTED(CAP_SYS_NICE) || (0==v)) {
		err_msg("CAP_SYS_NICE capability mandatory to operate properly!\n");
		return -1;
	}

	v=0;
	if (cap_get_flag(cap, CAP_SYS_RESOURCE, CAP_EFFECTIVE, &v)) {// check for effective RESOURCE cap
		err_msg_n(errno, "Capability test failed");
		return errno;
	}

	if (!CAP_IS_SUPPORTED(CAP_SYS_RESOURCE) || (0==v)) {
		err_msg("CAP_SYS_RESOURCE capability mandatory to operate properly!\n");
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

	if (SCHED_RR == policy && 0 < rrtime) {
		cont( "Set round robin interval to %dms..\n", rrtime);
		(void)sprintf(str, "%d", rrtime);
		if (setkernvar("sched_rr_timeslice_ms", str)){
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

	if (AFFINITY_USEALL != setaffinity){ // set affinity only if not useall
		if (numa_sched_setaffinity(mpid, naffinity))
			warn("could not set orchestrator affinity: %s\n", strerror(errno));
		else
			cont("Orchestrator's PID reassigned to CPU's %s\n", cpus);
	}

	/// -------------------- DOCKER & CGROUP CONFIGURATION
	// create Docker CGroup prefix
	cpusetdfileprefix = malloc(strlen(cpusetfileprefix) + strlen(cont_cgrp)+1);
	*cpusetdfileprefix = '\0'; // set first chat to null
	(void)strcat(strcat(cpusetdfileprefix, cpusetfileprefix), cont_cgrp);		

	/// --------------------
	/// Docker CGROUP setup - detection if present
	if (DM_CGRP == use_cgroup) { // option enabled, test for it
		struct stat s;

		// no memory has been allocated yet
		fileprefix = cpusetdfileprefix; // set to docker directory

		int err = stat(fileprefix, &s);
		if(-1 == err) {
			// Docker Cgroup not found, force enabled = try creating
			if(ENOENT == errno && force) {
				warn("CGroup '%s' does not exist. Is the daemon running?\n", cont_cgrp);
				if (0 != mkdir(fileprefix, ACCESSPERMS))
				{
					err_msg("Can not create container group: %s\n", strerror(errno));
					return -1;
				}
				// if it worked, stay in Container mode	
			} else {
			// Docker Cgroup not found, force not enabled = try switching to pid
				if(ENOENT == errno) 
					err_msg("Can not create container group: %s\n", strerror(errno));

				else {
					perror("Stat encountered an error");

					// exists -> goto PID detection, but first..
					// check for sCHED_DEADLINE first-> stop!
					if (SCHED_DEADLINE == policy) {
						err_msg("SCHED_DEADLINE does not allow forking. Can not switch to PID modes!\n");
						return -1;
					}
					// otherwise switch to next mode
					use_cgroup = DM_CNTPID;
				}
			}

		} else {
			// CGroup found, but is it a dir?
			if(S_ISDIR(s.st_mode)) {
				// it's a dir 
				cont("using CGroups to detect processes..\n");
			} else {
				// exists but is no dir -> goto PID detection
				// check for sCHED_DEADLINE first-> stop!
				if (SCHED_DEADLINE == policy) {
					err_msg("SCHED_DEADLINE does not allow forking. Can not switch to PID modes!\n");
					return -1;
				}
				// otherwise switch to next mode
				use_cgroup = DM_CNTPID;
			}
		}
	}

	// Display message according to detection mode set
	if (DM_CNTPID == use_cgroup)
		cont( "will use PIDs of '%s' to detect processes..\n", cont_ppidc);
	if (DM_CMDLINE == use_cgroup)
		cont( "will use PIDs of command signtaure '%s' to detect processes..\n", cont_pidc);
	if (DM_CGRP != use_cgroup)
		cont( "affinity only will be used to set PID execution..\n");

	/// --------------------
	/// detect numa configuration TODO: adapt for full support
	char * numastr = "0"; // default numa string
	if (-1 != numa_available()) {
		int numanodes = numa_max_node();
		numastr = calloc (5, 1); // WARN -> not unallocated
		sprintf(numastr, "0-%d", numanodes);
	}
	else
		warn("Numa not enabled, defaulting to memory node '0'\n");

	/// --------------------
	/// cgroup present, fix cpu-sets of running containers
	if (DM_CGRP == use_cgroup) {

		if (AFFINITY_USEALL != setaffinity){ // useall = ignore setting of exclusive

			cont( "reassigning Docker's CGroups CPU's to %s exclusively\n", affinity);

			DIR *d;
			struct dirent *dir;
			d = opendir(fileprefix);// -> pointing to global
			fileprefix = NULL; // clear pointer
			if (d) {

				/// Reassigning preexisting containers?
				while ((dir = readdir(d)) != NULL) {
				// scan trough docker cgroups, find them?
					if ((strlen(dir->d_name)>60) && // container strings are very long!
							(fileprefix=realloc(fileprefix,strlen(cpusetdfileprefix)
							+ strlen(dir->d_name)+1))) {
						fileprefix[0] = '\0';   // ensures the memory is an empty string
						// copy to new prefix
						strcat(fileprefix,cpusetdfileprefix);
						strcat(fileprefix,dir->d_name);

						if (setkernvar("/cpuset.cpus", affinity)){
							warn("Can not set cpu-affinity\n");
						}


					}
				}
				if (fileprefix)
					free (fileprefix);

				// Docker CGroup settings and affinity
				fileprefix = cpusetdfileprefix; // set to docker directory

				if (setkernvar("cpuset.cpus", affinity)){
					warn("Can not set cpu-affinity\n");
				}
				if (setkernvar("cpuset.mems", numastr)){
					warn("Can not set numa memory nodes\n");
				}
				if (setkernvar("cpuset.cpu_exclusive", "1")){
					warn("Can not set cpu exclusive\n");
				}

				closedir(d);
			}
		}
	}

	//------- CREATE NEW CGROUP AND MOVE ALL ROOT TASKS TO IT ------------
	// system CGroup, possible tasks are moved -> do for all

	fileprefix = NULL;

	cont("creating cgroup for system on %s\n", cpus);

	if ((fileprefix=realloc(fileprefix,strlen(cpusetfileprefix)+strlen("system/")+1))) {
		fileprefix[0] = '\0';   // ensures the memory is an empty string
		// copy to new prefix
		strcat(fileprefix,cpusetfileprefix);
		strcat(fileprefix,"system/");
		// try to create directory
		if(0 != mkdir(fileprefix, ACCESSPERMS) && EEXIST != errno)
		{
			// error otherwise
			warn("Can not set cpu system group: %s\n", strerror(errno));
			goto sysend; // skip all system things 
			// FIXME: might create unexpected behaviour
		}

		if (setkernvar("cpuset.cpus", cpus)){ 
			warn("Can not set cpu-affinity\n");
		}
		if (setkernvar("cpuset.mems", numastr)){
			warn("Can not set numa memory nodes\n");
		}
		if (setkernvar("cpuset.cpu_exclusive", "1")){
			warn("Can not set cpu exclusive\n");
		}

		cont( "moving tasks..\n");

		char * nfileprefix = NULL;
		if ((nfileprefix=realloc(nfileprefix,strlen(cpusetfileprefix)+strlen("tasks")+1))) {
			nfileprefix[0] = '\0';   // ensures the memory is an empty string
			// copy to new prefix
			(void)strcat(strcat(nfileprefix,cpusetfileprefix),"tasks");

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
						if (setkernvar("tasks", pid)){
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
				
				// some unmoveable tasks?
				if (mtask_old != mtask)
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
		if (fileprefix)
			free (fileprefix);

		if (nfileprefix)
			free (nfileprefix);

	}
	else
		return -1; //realloc issues



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
//	       "-u       --unbuffered      force unbuffered output for live processing (FIFO)\n"
#ifdef NUMA
//	       "-U       --numa            force numa distribution of memory nodes, RR\n"
#endif
//	       "-v       --verbose         output values on stdout for statistics\n"
	       "-w       --wcet=TIME       WCET runtime for deadline policy in us, default=%d\n"
			, CONT_DCKR, TSCAN, TDETM, CONT_PID, TWCET
		);
	if (error)
		exit(EXIT_FAILURE);
	exit(EXIT_SUCCESS);
}

enum option_values {
	OPT_AFFINITY=1, OPT_BIND, OPT_CLOCK, OPT_DFLAG,
	OPT_FIFO, OPT_INTERVAL, OPT_LOOPS, OPT_MLOCKALL,
	OPT_NSECS, OPT_PRIORITY, OPT_QUIET, OPT_THREADS, 
	OPT_SMP, OPT_RTIME, OPT_RRTIME, OPT_UNBUFFERED, OPT_NUMA, 
	OPT_SMI, OPT_VERBOSE, OPT_WCET, OPT_POLICY, OPT_HELP,
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
	int optargs = 0;

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
			{"verbose",          no_argument,       NULL, OPT_VERBOSE },
			{"policy",           required_argument, NULL, OPT_POLICY },
			{"wcet",             required_argument, NULL, OPT_WCET },
			{"help",             no_argument,       NULL, OPT_HELP },
			{NULL, 0, NULL, 0}
		};
		int c = getopt_long(argc, argv, "a:bc:C:dDfFi:kl:mn::p:Pqr:s::t:uUvw:?",
				    long_options, &option_index);
		if (-1 == c)
			break;
		switch (c) {
		case 'a':
		case OPT_AFFINITY:
			option_affinity = 1;
//			if (smp || numa)
//				break;
			if (NULL != optarg) {
				affinity = optarg;
				setaffinity = AFFINITY_SPECIFIED;
			} else if (optind<argc && atoi(argv[optind])) {
				affinity = argv[optind];
				optargs++;
				setaffinity = AFFINITY_SPECIFIED;
			} else {
				affinity = malloc(10);
				sprintf(affinity, "0-%d", max_cpus-1);
				setaffinity = AFFINITY_USEALL;
			}
			break;
		case 'b':
		case OPT_BIND:
			affother = 1; break;
		case 'c':
		case OPT_CLOCK:
			clocksel = atoi(optarg); break;
		case 'C':
			use_cgroup = DM_CGRP;
			if (NULL != optarg) {
				cont_cgrp = optarg;
			} else if (optind<argc) {
				cont_cgrp = argv[optind];
				optargs++;
			}
			break;
		case 'd':
		case OPT_DFLAG:
			setdflag = 1; break;
		case 'D':
			dryrun = 1; break;
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
		case 'k':
			trackpids = 1; break;
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
			} else if (optind<argc) {
				cont_pidc = argv[optind];
				optargs++;
			}
			break;
		case 'p':
		case OPT_PRIORITY:
			priority = atoi(optarg);
			if (SCHED_FIFO != policy && SCHED_RR != policy)
				warn(" policy and priority don't match: setting policy to SCHED_FIFO\n");
				policy = SCHED_FIFO;
			break;
		case 'P':
			psigscan = 1; break;
		case 'q':
		case OPT_QUIET:
			quiet = 1; break;
		case 'r':
		case OPT_RTIME:
			if (NULL != optarg) {
				runtime = atoi(optarg);
			} else if (optind<argc && atoi(argv[optind])) {
				runtime = atoi(argv[optind]);
				optargs++;
			}
			break;
		case OPT_RRTIME:
			if (NULL != optarg) {
				rrtime = atoi(optarg);
			} else if (optind<argc && atoi(argv[optind])) {
				rrtime = atoi(argv[optind]);
				optargs++;
			}
			break;
		case 's':
			use_cgroup = DM_CNTPID;
			if (NULL != optarg) {
				cont_ppidc = optarg;
			} else if (optind<argc) {
				cont_ppidc = argv[optind];
				optargs++;
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
				optargs++;
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
		case '?':
		case OPT_HELP:
			display_help(0); break;
		case OPT_POLICY:
			policy = string_to_policy(optarg); break;
		case OPT_SMI:
#ifdef ARCH_HAS_SMI_COUNTER
			smi = 1;
#else
			fatal("--smi is not available on your arch\n");
#endif
			break;
		}
	}

	// option mismatch verification
/*	if (option_affinity) {
		if (smp) {
			warn("-a ignored due to --smp\n");
		} else if (numa) {
			warn("-a ignored due to --numa\n");
		}
	}
*/

	if (smi) { // TODO: verify this statements, I just put them all
		if (setaffinity == AFFINITY_UNSPECIFIED)
			fatal("SMI counter relies on thread affinity\n");

		if (!has_smi_counter())
			fatal("SMI counter is not supported "
			      "on this processor\n");
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
	if (SCHED_DEADLINE == policy && interval < 1000 && !force) {
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
		warn("defaulting realtime priority to %d\n", 10);
		priority = 10;
	}

	// num theads must be > 0 
	if (1 > num_threads)
		error = 1;

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
		err_msg_n (iret1, "could not start update thread");
		t_stat1 = -1;
	}
	if (iret2 = pthread_create( &thread2, NULL, thread_update, (void*) &t_stat2)) {
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

	if(smi) {

		unsigned long smi_old;
		int maxccpu = numa_num_configured_cpus();
		info("SMI counters for the CPUs\n");
		// mask affinity and invert for system map / readout of smi of online CPUs
		for (int i=0;i<maxccpu;i++) 
			// if smi is set, read SMI counter
			if (*(smi_msr_fd+i)) {
				/* get current smi count to use as base value */
				if (get_smi_counter(*smi_msr_fd+i, &smi_old))
					fatal("Could not read SMI counter, errno: %d\n",
						0, errno);
				cont("CPU%d: %ld\n", i, smi_old-*(smi_counter+i));
			}
	}

	/* unlock everything */
	if (lockall)
		munlockall();

    return 0;
}
