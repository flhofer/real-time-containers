#include "kernutil.h"


#ifdef ARCH_HAS_SMI_COUNTER
int open_msr_file(int cpu)
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

static int kernvar(int mode, const char *prefix, const char *name, char *value, size_t sizeofvalue)
{
	char filename[128];
	int retval = 1;
	int path;
	size_t len_prefix = strlen(prefix), len_name = strlen(name);

	if (len_prefix + len_name + 1 > sizeof(filename)) {
		errno = ENOMEM;
		return 1;
	}

	memcpy(filename, prefix, len_prefix);
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

int setkernvar(const char *prefix, const char *name, char *value)
{
	if (dryrun) // suppress system changes
		return 0;

	if (kernvar(O_WRONLY, prefix, name, value, strlen(value))){
		printDbg(KRED "Error!" KNRM " could not set %s to %s\n", name, value);
		return -1;
	}
	
	return 0;

}

int getkernvar(const char *prefix, const char *name, char *value, int size)
{

	if (kernvar(O_RDONLY, prefix, name, value, size)){
		printDbg(KRED "Error!" KNRM " could not get %s\n", name);
		return -1;
	}
	
	return 0;

}

/// parse_cpumask(): checks if the cpu bitmask is ok
///
/// Arguments: - pointer to bitmask
/// 		   - max number of cpus
///
/// Return value: a bitmask containing the required values
///
struct bitmask *parse_cpumask(const char *option, const int max_cpus)
{
	struct bitmask * mask = numa_parse_cpustring_all(option);
	if (mask) {
		if (0==numa_bitmask_weight(mask)) {
			numa_bitmask_free(mask);
			mask = NULL;
		}
		else
		{
		info("%s: Using %u cpus.\n", __func__,
			numa_bitmask_weight(mask));
		}
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
