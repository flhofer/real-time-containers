#pragma once
#include <cstdlib>
#include <stdio.h>
#include <string>

struct OptParams {
    int dbg;
    int loops;
    int mininterval;    //usecs
    int maxinterval;    //usecs
    int maxWritePipes;
    bool bThreaded;
    bool bTimeSleep;
    bool bReadPipe;
    std::string readPipeName;
    std::string baseWritePipeName;
    ulong timingHistMinValue;
    ulong timingHistMaxValue;
    ulong histCount;
    int  maxTests;
    int  testSecs;
    int  firstFPS;
    int  lastFPS;
    ulong endInSeconds;
    int  datagenerator;     //0=>data distributor
                            //1=>usecase1 data generator
                            //2=>usecase2 data generator

    OptParams();
    int processOptions(int argc, char **argv);
    void printHelp(std::string msg);
};

