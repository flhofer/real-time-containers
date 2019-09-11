#pragma once
#include "DataGenerator.h"
#include <iostream>

class Stats;
class OptParams;

class DoSleep {
    unsigned long minLatency;
    unsigned long maxLatency;
public:
    DoSleep();
    bool doSleep(OptParams &params, timespec & sleepSpec, timespec &prevTimeSpec, Stats *pStats =nullptr);
    void printSleepLatency(std::ostream & os);
};
