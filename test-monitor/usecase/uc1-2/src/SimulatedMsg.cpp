#include "SimulatedMsg.h"
#include "PipeStruct.h"
#include "DataGenerator.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>

using namespace std;

SimulatedMsg::SimulatedMsg(int n, unsigned long histMin, unsigned long histMax, unsigned long histCt, const std::string &units, int numWorkers, int dbg/*=0*/)   
    :   testNum(0),
        maxTests(n),
        firstMsgEver(true),
        lastArrivalSecs(-1.0),
        prevSourceSecs(-1.0),
        fpsStatsInitialized(false),
        timingStatsInitialized(false),
        debug(dbg),
        statsPrinted(0),
        fpsStats(n),
        timingStats(n),
        testNames(n),
        pfpsStats(nullptr),
        ptimingStats(nullptr)
{
    unsigned long expectedFPS = 24;

    for (int i=0; i<maxTests; ++i)
    {
        fpsStats[i] = new Stats("FPS", 0, 64000, 640, std::string(" "), numWorkers, dbg);        
        fpsStats[i]->setStatsPrintScaling(.001, "FPS");
        testNames[i] = std::to_string(expectedFPS) + " FPS";
        timingStats[i] = new Stats("Delay", histMin, histMax, histCt, units, 0, dbg);
        expectedFPS+=8;
    }
    pfpsStats = fpsStats[0];
    fpsStatsInitialized = true;
    ptimingStats = timingStats[0];
    timingStatsInitialized = true;
}

SimulatedMsg::SimulatedMsg(int n, int dbg /*=0*/)
    :   testNum(0),
        maxTests(n),
        firstMsgEver(true),
        lastArrivalSecs(-1.0),
        prevSourceSecs(-1.0),
        fpsStatsInitialized(false),
        timingStatsInitialized(false),
        debug(dbg),
        statsPrinted(0),
        fpsStats(n),
        timingStats(n),
        testNames(n),
        pfpsStats(nullptr),
        ptimingStats(nullptr)
{}

SimulatedMsg::~SimulatedMsg()
{
    if (fpsStatsInitialized)
    {
        for (int i=0; i<maxTests; ++i)
        {
            delete fpsStats[i];
        }
    }
    if (timingStatsInitialized)
    {
        for (int i=0; i<maxTests; ++i)
        {
            delete timingStats[i];
        }
    }
}

void SimulatedMsg::configureTiming(unsigned long histMin, unsigned long histMax, unsigned long histCt, const std::string & units, int dbg)   
{
    for (int i=0; i<maxTests; ++i)
    {
        timingStats[i] = new Stats("Delay", histMin,histMax,histCt,units, 0, dbg);
    }
    testNum = 0;
    ptimingStats = timingStats[0];
    timingStatsInitialized = true;
}

bool SimulatedMsg::readDouble(double &mMsg, PipeStruct *pReadPipe)
{
    static const int expectBytes = sizeof(double);
    int nbytes = read(pReadPipe->fd, &mMsg, expectBytes);
    bool rval = true;

    if (nbytes < expectBytes)
    {
        if (nbytes == -1) 
        {
            fprintf(stderr, "ERROR: nextMsg - pipe read failed (errno=%d)\n", errno);
        }
        else 
        {
            fprintf(stderr, "ERROR: SimulatedMsg::nextMsg read %d bytes, not expected %d bytes\n", nbytes, expectBytes);
        }
        mMsg = terminationMsg;
        rval = false;
    }
    return rval;
}

bool SimulatedMsg::nextMsg(double &mMsg, PipeStruct *pReadPipe )
{
    //Auto-generated data
    bool rval = true;
    struct timespec nowSpec;
    double currSecs;

    if (pReadPipe)
    {
        //data distributor (generator type 0) and WorkerApp
        if (!readDouble(mMsg, pReadPipe))
        {
            rval = false;
            terminateProcess = true;
        }
        if (!terminateProcess)
        {
            clock_gettime(CLOCK_REALTIME, &nowSpec);
            currSecs = nowSpec.tv_sec + (double)nowSpec.tv_nsec/1e9;
            if ( mMsg == terminationMsg ) 
            {
                    /* The generator can signal the end of the last test by 
                     * sending the terminationMsg (see DataGeneration.h) 
                     * rather than the time.  
                     * The terminationMsg will be passed on 
                     * through the write pipes to signal end of test there as well.
                     */
                    fprintf(stderr, "SimulatedMsg::nextMsg - recognized %d mMsg signaling conclusion of test\n", (int)terminationMsg );
                    terminateProcess = true;
            }     
            else if (firstMsgEver)
            {
                if (fpsStatsInitialized)
                    pfpsStats->setStartTime(nowSpec);
                if (timingStatsInitialized)
                    ptimingStats->setStartTime(nowSpec);
                firstMsgEver = false;
                prevSourceSecs = mMsg;
                lastArrivalSecs = currSecs;
            }
            else
            {
                double sourceSecsSincePrev = mMsg - prevSourceSecs;
                double secsSinceLastArrival = currSecs - lastArrivalSecs;
                prevSourceSecs = mMsg;
                lastArrivalSecs = currSecs;
                if (sourceSecsSincePrev >  30.0) 
                {
                    startNextTest(nowSpec);
                }
                else if (secsSinceLastArrival != 0) //should never happen but can't divide by it if it does
                {
                    unsigned long fps = 1000/secsSinceLastArrival; //This is FPS scaled x1000
#ifdef MIT_DBG2
                    debug && fprintf(stderr, "SimulatedMsg::nextMsg computed fps=%f from %lf secsSinceLastArrival\n", (float)fps/1000.0, secsSinceLastArrival); 
#endif
                    pfpsStats->update(fps);
                    unsigned long delayUsecs = (currSecs-mMsg)*1e6;
                    ptimingStats->update(delayUsecs);
#ifdef LOG_EXCEPTIONAL_DELAYS
                    if (delayUsecs > 1000)  
                    {
                        //longer than 1 msec
                        fprintf(stderr, "Exceptional delayUsecs at sample %d of Test %s: Tg=%lf secs, current=%lf secs, computed delayUsecs = %lu\n",
                            pfpsStats->getCtr(), testNames[testNum].c_str(), mMsg, currSecs, delayUsecs);
                    } 
#endif
                }
            }
        }
        if (terminateProcess)
        {
            fprintf(stderr, "Ending Test %d\n", testNum);
            pfpsStats->setEndTime(nowSpec);
            ptimingStats->setEndTime(nowSpec);
        }
    }
    else 
    {
        //data generator (generator type 1)
        if (terminateProcess)   //In the data generator, it's set by monitorDesiredFPS
        {
            mMsg = terminationMsg;  //Signal end of test
        }
        else
        {   
            clock_gettime(CLOCK_REALTIME, &nowSpec);
            currSecs = (double)nowSpec.tv_sec + (double)nowSpec.tv_nsec/1e9;
            mMsg = currSecs;
        }
    }
    return rval;
}

void SimulatedMsg::startNextTest(timespec &t)
{
    fprintf(stderr, "At %ld%.09ld SimulatedMsg Ending test %d\n", t.tv_sec, t.tv_nsec, testNum);
    if (fpsStatsInitialized)
        pfpsStats->setEndTime(t);
    if (timingStatsInitialized)
        ptimingStats->setEndTime(t);
    
    if (testNum+1 < maxTests)
    {
        ++testNum;
        if (fpsStatsInitialized)
        {
            pfpsStats = fpsStats[testNum];
            pfpsStats->setStartTime(t);
        }
        if (timingStatsInitialized)
        {
            ptimingStats = timingStats[testNum];
            ptimingStats->setStartTime(t);
        }
    }
    else
    {
        fprintf(stderr, "SimulatedMsg - setting terminateProcess - completed maxTests (%d).\n", maxTests);
        terminateProcess = true;
    }
}

Stats * SimulatedMsg::getTimingStats()
{
    return ptimingStats;
}

Stats * SimulatedMsg::getFPSStats()
{
    return pfpsStats;
}

std::ostream & SimulatedMsg::printStats(std::ostream &os)
{
    if (++statsPrinted == 1)
    {
        os << "\n\n";
        for (int i=0; i<=testNum; ++i)
        {
            //statsSet[i].setStatsPrintScaling(1, "");
            os << "Test " << i << " (" << testNames[i] << "):\n";
            if (fpsStatsInitialized)
                fpsStats[i]->statsPrint(os) << "\n";
            os << "===========================\n\n";
            os << "Test " << i << " (" << testNames[i] << "):\n";
            if (timingStatsInitialized)
                timingStats[i]->statsPrint(os) << "\n" << endl;
            os << "==================================================================\n\n";
        }
        os << endl;
    }
    else
    {
        fprintf(stderr, "SimulatedMsg::printStats - skipping duplicate print of statistics\n");
    }
    return os;
}

void SimulatedMsg::setTestName(const std::string & nm)
{
    testNames[testNum] = nm;
}

std::ostream& Overrun::print(std::ostream& os, ulong co)
{
    char buffer[50] = {0};
    memset(buffer, 0, sizeof(buffer));
    sprintf(buffer, "%ld.%09ld", happenedOn.tv_sec, happenedOn.tv_nsec);
    os << "Configured runtime(nsec): " << co 
        << ", Actual runtime: " << overrun 
        << ", Keeping Busy for (nsec): " << keepBusyNSec 
        << ", Happened at (time): " << buffer
        << std::endl;
    return os;
}

std::ostream& ViolatedPeriod::print(std::ostream& os, ulong cr)
{
    char buffer[50] = {0};
    os << "Configured runtime(nsec): " << cr;
    os << ", Period violated by(nsec): " << violation;

    memset(buffer, 0, sizeof(buffer));
    sprintf(buffer, "%ld.%09ld", expectedPeriodStart.tv_sec, expectedPeriodStart.tv_nsec);
    os << ", Expected Period Start (time): " << buffer;

    memset(buffer, 0, sizeof(buffer));
    sprintf(buffer, "%ld.%09ld", actualPeriodStart.tv_sec, actualPeriodStart.tv_nsec);
    os << ", Actual Period Start (time): " << buffer << std::endl;

    return os;
}

UC2Log::UC2Log(ulong rtime, ulong dline, ulong li): 
        configuredDeadline(dline), 
        configuredRuntime(rtime),
        loopInterval(li),
        overruns(0),
        periods(0),
        averageRuntimes(0)
{

}

void UC2Log::addViolatedPeriod(ViolatedPeriod period)
{
    periods.push_back(period);
}

void UC2Log::addOverrun(Overrun overun)
{
    overruns.push_back(overun);
}

void UC2Log::addAverateRuntime(ulong ar)
{
    averageRuntimes.push_back(ar);
}

std::ostream& UC2Log::printSummary(std::ostream &os, ulong totalLoops)
{
    os << "\n\nSummary of Test\n\n";
    os << "Total Number of Periods: " << totalLoops << std::endl;
    os << "\tTotal Overruns: " << overruns.size() << std::endl;
    os << "\tTotal Violated Periods: " << periods.size() << std::endl;

    // Print average runtimes with specific duration
    if(averageRuntimes.size() > 0)
    {
        os << "--------------- Average runtime per " << loopInterval << " loops -------------------" << std::endl;
        
        for (auto it = averageRuntimes.begin(); it != averageRuntimes.end(); it++) 
        {
            os << "Average runtime: " << *it << " nsecs" << std::endl;
        }
    }

    // Print only if overruns are available
    if(overruns.size() > 0)
    {
        os << "--------------- Details of overrun -------------------" << std::endl;

        for (auto it = overruns.begin(); it != overruns.end(); it++) 
        {
            (*it).print(os, configuredRuntime);
        }
    }

    // Print only if periods are available
    if(periods.size() > 0)
    {
        os << "--------------- Details of period violations -------------------" << std::endl;

        for (auto it = periods.begin(); it != periods.end(); it++) 
        {
            (*it).print(os, configuredRuntime);
        }

    }

    return os;
}

UC2Log::~UC2Log()
{

}
