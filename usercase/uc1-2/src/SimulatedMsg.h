#pragma once
#include <string>
#include <vector>
#include <atomic>
#include "Stats.h"

using ulong = unsigned long;
struct timespec;

class PipeStruct;

class SimulatedMsg {
    int testNum;
    int maxTests;
    bool firstMsgEver;
    double lastArrivalSecs;
    double prevSourceSecs;
    bool fpsStatsInitialized;
    bool timingStatsInitialized;
    int debug;
    std::atomic<int> statsPrinted;
    std::vector<Stats *> fpsStats;
    std::vector<Stats *> timingStats;
    std::vector<std::string> testNames;
    Stats * pfpsStats;
    Stats * ptimingStats;
public:
    SimulatedMsg(int n, int dbg=0);
    SimulatedMsg(int n, unsigned long histMin, unsigned long histMax, unsigned long histCt, const std::string &units, int numWorkers, int dbg=0);
    ~SimulatedMsg();
    void configureTiming(unsigned long histMin, unsigned long histMax, unsigned long histCt, const std::string &units, int dbg );
    bool nextMsg(double &mMsg, PipeStruct *pReadPipe );
    bool readDouble(double &mMsg, PipeStruct *pReadPipe);
    Stats * getTimingStats();
    Stats * getFPSStats();
    std::ostream & printStats(std::ostream &os);
    void startNextTest(timespec &t);
    void setTestName(const std::string &nm);
};

class Overrun
{
    ulong overrun = 0;
    ulong keepBusyNSec = 0;
    timespec happenedOn;
public:
    Overrun(){}
    Overrun(ulong orr, ulong kb, timespec ho) : overrun(orr), keepBusyNSec(kb), happenedOn(ho){}
    std::ostream& print(std::ostream& os, ulong co);
};

class ViolatedPeriod
{
    timespec expectedPeriodStart = {0};
    timespec actualPeriodStart = {0};
    ulong violation = 0;

public:
    ViolatedPeriod(){}
    ViolatedPeriod(timespec ep, timespec ap, ulong vl): expectedPeriodStart(ep), actualPeriodStart(ap), violation(vl){}

    std::ostream& print(std::ostream& os, ulong cr);   
};

class UC2Log
{
    std::vector<Overrun> overruns;
    std::vector<ViolatedPeriod> periods;
    ulong configuredRuntime = 0;
    ulong configuredDeadline = 0;
    ulong loopInterval = 0;
    std::vector<ulong> averageRuntimes;

public:
    UC2Log(ulong rtime, ulong dline, ulong li);
    std::ostream& printSummary(std::ostream& os, ulong totalLoops);
    void addOverrun(Overrun orr);
    void addViolatedPeriod(ViolatedPeriod vp);
    void addAverateRuntime(ulong ar);

    ~UC2Log();
};
