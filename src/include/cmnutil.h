/*
 * cmnutil.h
 *
 *  Created on: May 3, 2020
 *      Author: Florian Hofer
 */

#ifndef _CMNUTIL_H_
	#define _CMNUTIL_H_

	#include <limits.h>		// Posix char limits

	#define USEC_PER_SEC		1000000
	#define NSEC_PER_SEC		1000000000
	#define TIMER_RELTIME		0

	#define CPUSTRLEN			64	// max length of a bit-mask CPU string

	#define MAX(x, y) (((x) > (y)) ? (x) : (y))
	#define MIN(x, y) (((x) < (y)) ? (x) : (y))

	// EX RT-UTILS
	#define _STR(x) #x
	#define STR(x) _STR(x)

	// Included in kernel 4.13
	#ifndef SCHED_FLAG_RECLAIM
		#define SCHED_FLAG_RECLAIM		0x02
	#endif

	// Included in kernel 4.16
	#ifndef SCHED_FLAG_DL_OVERRUN
		#define SCHED_FLAG_DL_OVERRUN		0x04
	#endif

	// for MUSL based systems
	#ifndef RLIMIT_RTTIME
		#define RLIMIT_RTTIME 15
	#endif
	#ifndef pthread_yield
		#define pthread_yield sched_yield
	#endif
	#ifndef _POSIX_PATH_MAX
		#define _POSIX_PATH_MAX 1024
	#endif

	/// tsnorm(): verifies timespec for boundaries + fixes it
	///
	/// Arguments: pointer to timespec to check
	///
	/// Return value: -
	static inline void tsnorm(struct timespec *ts)
	{
		while (ts->tv_nsec >= NSEC_PER_SEC) {
			ts->tv_nsec -= NSEC_PER_SEC;
			ts->tv_sec++;
		}
	}

#endif /* _CMNUTIL_H_ */
