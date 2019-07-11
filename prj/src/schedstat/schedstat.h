// Default stuff, needed form main operation
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // used for string parsing
#include <pthread.h>// used for thread management
#include <unistd.h> // used for POSIX XOPEN constants

#include <sched.h>			// scheduler functions
#include <linux/sched.h>	// linux specific scheduling
#include <linux/types.h>	// data structure types, short names and linked list
#include <signal.h> 		// for SIGs, handling in main, raise in update
#include <fcntl.h>			// file control, new open/close functions
#include <dirent.h>			// dir enttry structure and expl
#include <errno.h>			// error numbers and strings

#include "orchdata.h"	// memory structure to store information
#include "rt-utils.h"	// trace and other utils
#include "kernutil.h"	// generic kernel utilities
#include "error.h"		// error and strerr print functions

#ifndef __SCHEDSTAT_
	#define __SCHEDSTAT_

	#define PRGNAME "DC static orchestrator"

#endif
