#include "DataGenerator.h"
#include "GenerateData.h"

GenerateData::GenerateData(PipeStruct *wPipes, PipeStruct *rPipe, OptParams &optParams, std::atomic_int &nPipes, int maxWPipes)
  : pWritePipes(wPipes),
    pReadPipe(rPipe),
    params(optParams),
    numPipes(nPipes),
    maxWritePipes(maxWPipes),
    generator(optParams.datagenerator),
    initialFPS(true),
    prevDesiredFPS(-1),
    constIntervalUsec(0),
	sleepIntervalRange(0),
	pSimulatedMsg(nullptr),
	mMsg(0.0)
{}

void GenerateData::initializeSimulatedMsg()
{
    clock_gettime(CLOCK_REALTIME, &nowSpec);
    fprintf(stderr, "generator = %d\n", generator);
    /**************************************
    ****** Initialize SimulatedMsg *******
    **************************************/
    switch(generator) {
    case 0 :
        /*****************************************
        * Data Distributor - 1 read pipe, multiple write pipes
        * fps stats and timing for frame arrival delay, one Stats instance/test
        * Transition from test to test recognized in SimulatedMsg based upon 
        * multi-second delay since the last msg received.
        * No external initialization of start time needed.
        *****************************************/
        pSimulatedMsg = new SimulatedMsg(params.maxTests, 0, 1000, 1000, std::string("usec"), params.maxWritePipes, params.dbg);    //Up to 1 msec in 1 usec intervals
        break;

    case 1 :
    case 2 :
        pSimulatedMsg = new SimulatedMsg(params.maxTests, params.dbg);
        pSimulatedMsg->configureTiming(0,1000,1000,std::string("usec"), params.dbg);    //Up to 1msec in 1 usec intervals

        pSimulatedMsg->getTimingStats()->setStartTime(nowSpec);
        break;

    default:
        fprintf(stderr, "\nUnimplemented value of generator %d\n", generator);
        terminateProcess = true;
        break;
    }   
}

void GenerateData::initializeSleepSpec()
{
    SimulatedMsg & simulatedMsg = *pSimulatedMsg;

    /**************************************
    **** Initialize fps and sleepSpec ****
    **************************************/
    sleepIntervalRange = params.maxinterval - params.mininterval;
    switch (generator)
    {
    case 0:
        if (pReadPipe == nullptr)
        {
            fprintf(stderr, "ERROR: data distributor read pipe not specified\n");
            return;
        }
        else 
            fprintf(stderr, "generateData thread: type=distributor, read from pipe, maximum write Pipes=%d\n",
                params.maxWritePipes);
        break;

    case 1:
        /**********************************************************
        * Use Case 1 Data Generator - 1 write pipe
        * Timing stats for wakeup latency, one Stats instance/test
        * Transition to next test recognized in generateData from 
        * change in desiredFPS.
        * DoSleep computes wakeup latency and updates timing stats.
        **********************************************************/
        if (sleepIntervalRange == 0 && params.mininterval != 0 )
        {
            // This case is not currently in use
            constIntervalUsec = params.mininterval;
            /********************************************
            * periodic operation
            *******************************************/
            sleepSpec.tv_sec = constIntervalUsec/1e6;
            sleepSpec.tv_nsec = 1000 * (constIntervalUsec-sleepSpec.tv_sec*1e6);
            fprintf(stderr, "generateData thread: type=generator 1, constant period %d usec: mininterval=%d usec, maxinterval=%d usec, sleepIntervalRange=%d usec, max Write Pipes=%d\n",
                constIntervalUsec, params.mininterval, params.maxinterval, sleepIntervalRange, params.maxWritePipes);
        }
        else
        {
            //sleep interval is determined from the current FPS setting
            while (desiredFPS == -1 || desiredFPS == 0)
            {
                sleep(1);
            }
            int fps = desiredFPS;
            prevDesiredFPS = fps;
            std::string s(std::to_string(fps));
            s += " FPS";
            simulatedMsg.setTestName(s);
            setSleepSpecFromFPS(fps);
            fprintf(stderr, "Location a: FPS initialized- fps=%d, prevDesiredFPS=%d\n", fps, prevDesiredFPS);
            initialFPS = false;
        }
        break;

    case 2:
        /**********************************************************
        * Use Case 2 Data Generator
        **********************************************************/
        if (sleepIntervalRange != 0)
        {
            /********************************************
            * Data Generator for Event Driven
            * Random interval between writes of auto-generated data
            *******************************************/
            // Seed the random number generator with the current timespec nsec value
            clock_gettime(CLOCK_REALTIME, &nowSpec);
            unsigned int seed = (unsigned int)(nowSpec.tv_nsec);
            fprintf(stderr, "generateData thread: type=generator 2, seeding Random Number generator with seed %u (tv_nsec=%ld), num Write Pipes=%d\n", seed, nowSpec.tv_nsec, params.maxWritePipes);
            srand(seed);
        }
        break;
    }
}

void GenerateData::useCase1Sleep()
{
    if (constIntervalUsec == 0 )   //This is the normal case
    {
        int fps = desiredFPS;   //desired frames/second
        params.dbg>1 && fprintf(stderr, "Location b: initialFPS flag=%s, fps=%d, prevDesiredFPS=%d\n", (initialFPS?"true":"false"), fps, prevDesiredFPS);
        if (fps == (int)terminationMsg)
        {
            terminateProcess=true;  //This should be redundant, because monitorDesiredFPS should already have done it
            return;
        }
        if (fps != prevDesiredFPS)
        {
            timespec statsStartSpec;
            clock_gettime(CLOCK_REALTIME, &statsStartSpec);
            fprintf(stderr, "At %ld.%09ld: initialFPS flag=%s, new fps=%d, prevDesiredFPS=%d\n", 
                statsStartSpec.tv_sec, statsStartSpec.tv_nsec, (initialFPS?"true":"false"), fps, prevDesiredFPS);
            while (desiredFPS == 0)
            {
                sleep(1);
            }

            fps = desiredFPS;
            clock_gettime(CLOCK_REALTIME, &statsStartSpec);
            fprintf(stderr, "At %ld.%09ld: fps is no longer 0.  initialFPS flag=%s, new fps=%d, prevDesiredFPS=%d\n", 
                statsStartSpec.tv_sec, statsStartSpec.tv_nsec, (initialFPS?"true":"false"), fps, prevDesiredFPS);

            params.dbg && fprintf(stderr, "Location d: Calling startNewTest (new fps=%d, prevDesiredFPS=%d)\n", fps, prevDesiredFPS);
            pSimulatedMsg->startNextTest(statsStartSpec);
            std::string testName(std::to_string(fps));
            testName += " FPS";
            pSimulatedMsg->setTestName(std::to_string(fps));
            /********************************************
            * periodic operation
            *******************************************/
            setSleepSpecFromFPS(fps);
            prevDesiredFPS = fps;
            clock_gettime(CLOCK_REALTIME, &statsStartSpec);
            prevTimeSpec = statsStartSpec;
        }    
    }
    sleeper.doSleep(params, sleepSpec, prevTimeSpec, pSimulatedMsg->getTimingStats());
}

void GenerateData::useCase2Sleep()
{
    if (sleepIntervalRange != 0)
    {
        //Event Driven - Calculate random sleep
        long sleepUsec = rand() % sleepIntervalRange + params.mininterval;
        sleepSpec.tv_sec = time_t(sleepUsec/1e6);
        sleepSpec.tv_nsec = 1000*(sleepUsec - (sleepSpec.tv_sec*1e6));
        sleeper.doSleep(params, sleepSpec, prevTimeSpec, pSimulatedMsg->getTimingStats());
    }
}

void GenerateData::mainLoop()
{
    SimulatedMsg &simulatedMsg = *pSimulatedMsg;

    while (numPipes.load() == 0 && !terminateProcess) 
    {
        sleep(1);
    }

    /****************************************************************
    * Main DataGeneration Loop
    ****************************************************************/
    PipeStruct *p = pWritePipes;
    clock_gettime(CLOCK_REALTIME, &nowSpec);
    prevTimeSpec = nowSpec; //Reinitialize so we delay the next from this time

    fprintf(stderr, "%s GenerateData::mainLoop\n", progName.c_str());
    for (int n = 0, loopct=0, cnt=0; !terminateProcess; )
    {
        /**************************************************************
        * If Distributor (generator type 0), reads from pipe.  
        * Updates FPS and timing statistics type 0.
        *
        * If Generator (generator type 1) creates MSG.
        **************************************************************/
        if (!simulatedMsg.nextMsg(mMsg, pReadPipe)) 
        {
            fprintf(stderr, "generateData: simulatedMsg returned false.  breaking out of primary loop.\n");
            break;
        }

    	cnt=0;
        // -- Pipe is not open, loop through until one is
        while ((!terminateProcess) && p->fd == -1 )
        {
            // so, are we at the end?
        	if (n+1 >= maxWritePipes)
            {
                n = 0;
                p = pWritePipes;

                if (params.dbg) 
                {
                    clock_gettime(CLOCK_REALTIME, &nowSpec);
                    fprintf(stderr, "%s: At %ld.%09ld secs, completed write to %d pipes\n",
                        (generator==0 ? "DataDistributor" : "DataGenerator"), nowSpec.tv_sec, nowSpec.tv_nsec, numPipes.load());
                }
                if (params.loops != -1)
                {
                    //For debugging purposes, this allows running just loops passes through all open pipes for the first FPS before terminating
                    if (++loopct >= params.loops)
                    {
                        terminateProcess = true;
                        break;
                    }
                }
            }
            else
            {
                ++p;
                ++n;
                ++cnt;
                // tried all pipes, yield to avoid low priority lock
                if (cnt >= maxWritePipes){
                	cnt=0;
                    usleep(1000);	// WARN: hard coded
                }
            }
        }

        if (terminateProcess)
            break;

        if ( p->fd != -1)
        {
            write(p->fd, &mMsg, sizeof(mMsg));
            if (generator == 0)
            {
                //data distributor
                simulatedMsg.getFPSStats()->newSample(n);
            }
        }
        switch (generator)
        {
            case 1:
                useCase1Sleep();
                break;
            case 2:
                useCase2Sleep();
               break;
            default:
                break;
        }
        if (terminateProcess)
            break;

        if (++n >= maxWritePipes)  
        {
            n = 0;
            if (params.loops != -1)
            {
                if (params.dbg)
                {
                    clock_gettime(CLOCK_REALTIME, &nowSpec);
                    fprintf(stderr, "%s: At %ld.%09ld secs, completed write to %d pipes\n",
                        (generator==0 ? "DataDistributor" : "DataGenerator"), nowSpec.tv_sec, nowSpec.tv_nsec, numPipes.load());
                }

            	//For debugging purposes, this allow running just loops passes through all open pipes for the first FPS before terminating
                if (++loopct >= params.loops)
                {
                    terminateProcess = true;
                }
            }
        }
        p = pWritePipes + n;
    }
}

void GenerateData::endProcessing()
{
    SimulatedMsg &simulatedMsg = *pSimulatedMsg;

    clock_gettime(CLOCK_REALTIME, &nowSpec);
    simulatedMsg.getTimingStats()->setEndTime(nowSpec);
    if (generator == 1 || (generator==2 && sleepIntervalRange != 0) )
    {
        sleeper.printSleepLatency(cerr);
    }

    fprintf(stderr, "%s generateData - exited main loop - terminateProcess=%s.  Sending terminationMsg (%d) to all open write pipes \n", 
        progName.c_str(), (terminateProcess ? "true" : "false"), (int)terminationMsg );
    PipeStruct *p;
    for (int n = 0; n < maxWritePipes; ++n)
    {
        p = pWritePipes + n;
        if (p->fd != -1)
        {
            write(p->fd, &terminationMsg, sizeof(terminationMsg));
        }
    }

    fprintf(stderr, "%s generateData - calling simulatedMsg.printStats(cerr)\n", progName.c_str());
    simulatedMsg.printStats(cerr);
    sleep(30);  //Give plenty of time for processes at the other end of the write pipes to receive the termination message and quit.
    delete pSimulatedMsg;
}

void GenerateData::setSleepSpecFromFPS(int fps)
{
    long sleepNsec = 1e9/fps;
    sleepSpec.tv_sec = sleepNsec/1e9;
    sleepSpec.tv_nsec = (sleepNsec - sleepSpec.tv_sec * 1e9);
    fprintf(stderr, "generateData thread: period set from FPS (%d), sleepSpec=%ld.%09ld\n",
                   fps, sleepSpec.tv_sec, sleepSpec.tv_nsec);
}

/**************************************
* generateData: a single thread which writes data in sequence to one or more already open named pipes 
*  - number of open pipes can change dynamically
*  - test PipeStruct::fd to identify open pipe
*  - for each named pipe, a separate thread will have performed a blocking open and incremented the atomic numPipes 
*    after storing the file descriptor in the corresponding fd
***************************************/
void generateData(PipeStruct *pWritePipes, PipeStruct * pReadPipe, OptParams &params,  std::atomic_int &numPipes, int maxWritePipes)
{
    GenerateData cGenData(pWritePipes, pReadPipe, params, numPipes, maxWritePipes);
    cGenData.initializeSimulatedMsg();
    cGenData.initializeSleepSpec();
    cGenData.mainLoop();
    cGenData.endProcessing();
}

