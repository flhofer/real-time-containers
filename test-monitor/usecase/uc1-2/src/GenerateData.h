#pragma once
#include <fcntl.h>
#include <string>
#include <atomic>
#include "SimulatedMsg.h"
#include "OptParams.h"
#include "PipeStruct.h"
#include "DoSleep.h"

class GenerateData 
{
    PipeStruct *    pWritePipes;
    PipeStruct *    pReadPipe;
    SimulatedMsg *	pSimulatedMsg;
    OptParams       params;
    std::atomic_int &numPipes;
    int             maxWritePipes;
    int             generator;
    bool            initialFPS;
    int             prevDesiredFPS;
    int             constIntervalUsec;
    struct timespec prevTimeSpec;
    struct timespec nowSpec;
    struct timespec sleepSpec;
    int 			sleepIntervalRange;
    DoSleep         sleeper;
    double 			mMsg;

    void setSleepSpecFromFPS(int fps);
    void useCase1Sleep();
    void useCase2Sleep();

public:
    GenerateData(PipeStruct *wPipes, PipeStruct *rPipe, OptParams &optParams, std::atomic_int &nPipes, int maxWPipes);

    void initializeSimulatedMsg();
    void initializeSleepSpec();
    void mainLoop();
    void endProcessing();
};

void generateData(PipeStruct *pWritePipes, PipeStruct * pReadPipe, OptParams &params,  std::atomic_int &numPipes, int maxWritePipes);
