/*
 * Copyright (C) 2019 Florian Hofer <info@florianhofer.it>
 *
 * based on functions from rt-test that has
 * Copyright (C) 2009 Carsten Emde <carsten.emde@osadl.org>
 * Copyright (C) 2010 Clark Williams <williams@redhat.com>
 * Copyright (C) 2015 John Kacur <jkacur@redhat.com>
 *
 * based on functions from cyclictest that has
 * (C) 2008-2009 Clark Williams <williams@redhat.com>
 * (C) 2005-2007 Thomas Gleixner <tglx@linutronix.de>
 */

#include "rt-utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/sched.h>	// Linux specific scheduling
#include <sched.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syscall.h> /* For SYS_gettid definitions */

#include "rt-sched.h"
#include "error.h"
#include "orchdata.h"

static char debugfileprefix[MAX_PATH];

/*
 * Finds the tracing directory in a mounted debugfs
 */
char *get_debugfileprefix(void)
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
		      STR(MAX_PATH)
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

int mount_debugfs(char *path)
{
	char *mountpoint = path;
	char cmd[MAX_PATH];
	char *prefix;
	int ret;

	/* if it's already mounted just return */
	prefix = get_debugfileprefix();
	if (strlen(prefix) != 0) {
		info("debugfs mountpoint: %s\n", prefix);
		return 0;
	}
	if (!mountpoint)
		mountpoint = "/sys/kernel/debug";

	sprintf(cmd, "mount -t debugfs debugfs %s", mountpoint);
	ret = system(cmd);
	if (ret != 0) {
		fprintf(stderr, "Error mounting debugfs at %s: %s\n",
			mountpoint, strerror(errno));
		return -1;
	}
	return 0;
}

static char **tracer_list;
static char *tracer_buffer;
static int num_tracers;
#define CHUNKSZ   1024

/// get_tracers(): get available function tracers in kernel
///
/// Arguments: - pointer to the resulting string array
///
/// Return value: no of found traces, 0 = none
///					-1= error and errno is set
int get_tracers(char ***list)
{
	/* if we've already parse it, return what we have */
	if (tracer_list) {
		*list = tracer_list;
		return num_tracers;
	}

	int ret;
	FILE *fp;
	char buffer[CHUNKSZ];
	char *prefix = get_debugfileprefix(); //  find debug path TODO: integrate to system for file path detection

	errno = 0; // reset global errno

	/* open the tracing file available_tracers */
	sprintf(buffer, "%savailable_tracers", prefix);
	if ((fp = fopen(buffer, "r")) == NULL){
		err_msg_n(errno, "Can't open %s for reading", buffer);
		return -1; //  errno = ... pass errno from open, read or write
	}

	char *tmpbuf = NULL;
	char *ptr, *ptr_p;

	/* allocate initial buffer */
	if (!(ptr = tmpbuf = malloc(CHUNKSZ))){
		err_msg_n(errno, "error allocating initial space for tracer list");
		return -1; //  errno = ... pass errno from open, read or write
	}

	int tmpsz = 0;

	/* read in the list of available tracers */
	while ((ret = fread(buffer, sizeof(char), CHUNKSZ, fp))) {
		if ((ptr+ret+1) > (tmpbuf+tmpsz)) {
			if (!(tmpbuf = realloc(tmpbuf, tmpsz + CHUNKSZ))){
				err_msg("error allocating space for list of valid tracers");

			}
			tmpsz += CHUNKSZ;
		}
		strncpy(ptr, buffer, ret);
		ptr += ret;
	}
	fclose(fp);
	if (0 == tmpsz){
		err_msg("error reading available tracers. Empty buffer.");
		errno = EIO;
		return -1; //  errno = ... pass errno from open, read or write
	}

	// copy temporary buffer to local static - keeps string array
	tracer_buffer = tmpbuf;

	/* get a buffer for the pointers to tracers */
	if (!(tracer_list = malloc(sizeof(char *)))){
		err_msg("error allocating tracer list buffer");
		return -1; //  errno = ... pass errno from open, read or write
	}

	/* parse the buffer */
	ptr = strtok_r(tmpbuf, " \t\n\r", &ptr_p);
	do {
		tracer_list[num_tracers++] = ptr;
		tracer_list = realloc(tracer_list, sizeof(char*)*(num_tracers+1));
		tracer_list[num_tracers] = NULL;
	} while ((ptr = strtok_r(NULL, " \t\n\r", &ptr_p)) != NULL);

	/* return the list and number of tracers */
	*list = tracer_list;
	return num_tracers;
}


/*
 * return zero if tracername is not a valid tracer, non-zero if it is
 */

int valid_tracer(char *tracername)
{
	char **list;
	int ntracers;
	int i;

	ntracers = get_tracers(&list);
	if (ntracers == 0 || tracername == NULL)
		return 0;
	for (i = 0; i < ntracers; i++)
		if (strncmp(list[i], tracername, strlen(list[i])) == 0)
			return 1;
	return 0;
}

/*
 * enable event tracepoint
 */
int setevent(char *event, char *val)
{
	char *prefix = get_debugfileprefix();
	char buffer[MAX_PATH];
	int fd;
	int ret;

	sprintf(buffer, "%s%s", prefix, event);
	if ((fd = open(buffer, O_WRONLY)) < 0) {
		warn("unable to open %s\n", buffer);
		return -1;
	}
	if ((ret = write(fd, val, strlen(val))) < 0) {
		warn("unable to write %s to %s\n", val, buffer);
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int event_enable_all(void)
{
	return setevent("events/enable", "1");
}

int event_disable_all(void)
{
	return setevent("events/enable", "0");
}

int event_enable(char *event)
{
	char path[MAX_PATH];

	sprintf(path, "events/%s/enable", event);
	return setevent(path, "1");
}

int event_disable(char *event)
{
	char path[MAX_PATH];

	sprintf(path, "events/%s/enable", event);
	return setevent(path, "0");
}

const char *policy_to_string(int policy)
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

	return "unknown";
}
\
/// policy_is_realtime(): verify if given policy is a real-time policy
///
/// Arguments: - scheduling enumeration constant (int) identifying policy
///
/// Return value: returns 1 if real-time, 0 otherwise
///
const int policy_is_realtime(int policy)
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

/// string_to_policy(): match string with a scheduling policy
///
/// Arguments: - string identifying policy
///
/// Return value: returns scheduling enumeration constant, -1 if failed
///
int string_to_policy(const char *policy_name, uint32_t *policy)
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

/// string_to_affinity(): match string with a affinity policy
///
/// Arguments: - string identifying affinity
///
/// Return value: returns affinity enumeration constant, 0 (other) if failed
///
uint32_t string_to_affinity(const char *str)
{
	if (!strcmp(str, "unspecified"))
		return AFFINITY_UNSPECIFIED;
	else if (!strcmp(str, "specified"))
		return AFFINITY_SPECIFIED;
	else if (!strcmp(str, "useall"))
		return AFFINITY_USEALL;

	return 0; // default to other
}

/// gettid(): gets the thread id from scheduler/kernel
///
/// Arguments: - none
///
/// Return value: pid_t thread identifier, always successful
///
pid_t gettid(void)
{
	return syscall(SYS_gettid);
}