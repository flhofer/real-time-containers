#include <limits.h>
#include "DoSleep.h"
#include "Stats.h"
#include "OptParams.h"

DoSleep::DoSleep()
    : minLatency(LONG_MAX),
      maxLatency(0)
{}

bool DoSleep::doSleep(OptParams &params, timespec &sleepSpec, timespec &prevTimeSpec, Stats *pStats)
{
    struct timespec afterSpec, delaySpec, wakeupTimeSpec;
    /************************************************************* 
     * Always use ABS_TIMER, compute wakeup time from time of previous
     *************************************************************/
    wakeupTimeSpec = prevTimeSpec;
    wakeupTimeSpec.tv_sec += sleepSpec.tv_sec;
    wakeupTimeSpec.tv_nsec += sleepSpec.tv_nsec;
    if (wakeupTimeSpec.tv_nsec >= 1e9)
    {
        wakeupTimeSpec.tv_sec += 1;
        wakeupTimeSpec.tv_nsec -= 1e9;
    }

    int ret = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &wakeupTimeSpec, NULL);
    if (0 != ret)
        fprintf(stderr, "ERROR: could not set sleep timer (errno=%d)\n", ret);

    if (params.bTimeSleep )
    {
        clock_gettime(CLOCK_REALTIME, &afterSpec);
        
        delaySpec.tv_sec  = afterSpec.tv_sec - wakeupTimeSpec.tv_sec;
        delaySpec.tv_nsec = afterSpec.tv_nsec - wakeupTimeSpec.tv_nsec;
        if (delaySpec.tv_nsec < 0 )
        {
            delaySpec.tv_sec -= 1;
            delaySpec.tv_nsec += 1e9;
        }

        unsigned long wakeupLatencyUsec = delaySpec.tv_sec * 1e6 + delaySpec.tv_nsec/1e3;

#ifdef LOG_EXCEPTIONAL_DELAYS
        if (wakeupLatencyUsec > 1000)
        {
            fprintf(stderr, 
                "ExceptionalWakeupLatency: Expected Wakeup was %ld.%09ld secs; Actual Wakeup was %ld.%09ld secs; Computed wakeup delay was %ld.%09ld; Computed wakeup_latency was %ld usecs\n", 
            wakeupTimeSpec.tv_sec, wakeupTimeSpec.tv_nsec,
            afterSpec.tv_sec, afterSpec.tv_nsec,
            delaySpec.tv_sec, delaySpec.tv_nsec,
            wakeupLatencyUsec);
        }
#endif
        if (pStats != nullptr)
        {
            pStats->update(wakeupLatencyUsec);
        }
        minLatency = std::min(minLatency, wakeupLatencyUsec);
        maxLatency = std::max(maxLatency, wakeupLatencyUsec);
    }
    prevTimeSpec = wakeupTimeSpec;    //Caller will preserve to insure regular intervals

    return (ret == 0);
}

void DoSleep::printSleepLatency(std::ostream & os)
{
    os << "DoSleep: Overall Min Sleep Latency was " << minLatency << "usec.  Max Sleep Latency: " << maxLatency << "usec." << endl;
}
