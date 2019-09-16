#include <fcntl.h>
#include <thread>
#include <sys/resource.h>
#include <functional>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <string>
#include <sys/mman.h>
#include "OptParams.h"
#include "SimulatedMsg.h"
#include "PipeStruct.h"
#include "DoSleep.h"
#include "FPSThread.h"
#include "DataGenerator.h"
#include "GenerateData.h"

std::string progName;
bool terminateProcess{false};
int desiredFPS{-1};
const char * fpsName = "/tmp/fps";

bool createPipes(PipeStruct * pWritePipes, PipeStruct **ppReadPipe, OptParams &optParams)
{
    /********************************************
     * Create read pipe if required
     *******************************************/
    if (optParams.bReadPipe)
    {
        /********************************************
         * Create read pipe if required
         *******************************************/
        PipeStruct * pReadPipe = new PipeStruct();
        if (!pReadPipe->createPipe(0, optParams.readPipeName, progName))
        {
            delete pReadPipe;
            return false;
        }
        *ppReadPipe = pReadPipe;
    }
    /********************************************
     * Create write pipes
     *******************************************/
    PipeStruct *p = pWritePipes;
    for (int n=0 ; n<optParams.maxWritePipes; ++n, ++p)
    {

        std::string fifoName = optParams.baseWritePipeName + "_" + std::to_string(n);

        if(optParams.datagenerator == 2){
            fifoName = optParams.baseWritePipeName;
        }

        if (!p->createPipe(n, fifoName, progName))
            return false;
    }
    return true;
}

/**************************************
 * generateData: a single thread which writes data in sequence to one or more already open named pipes 
 *  - number of open pipes can change dynamically
 *  - test PipeStruct::fd to identify open pipe
 *  - for each named pipe, a separate thread will have performed a blocking open and incremented the atomic numPipes 
 *    after storing the file descriptor in the corresponding fd
 ***************************************/
//void generateData(PipeStruct *pWritePipes, PipeStruct * pReadPipe, OptParams &params,  std::atomic_int &numPipes, int maxWritePipes)
//{
//    struct timespec sleepSpec, nowSpec, prevTimeSpec;
//    int constIntervalUsec(0);
//    int sleepIntervalRange;
//    int prevDesiredFPS = -1;
//    bool initialFPS = true;
//
//    DoSleep sleeper;
//
//    SimulatedMsg *pSimulatedMsg;
//    clock_gettime(CLOCK_REALTIME, &nowSpec);
//    fprintf(stderr, "params.datagenerator = %d\n", params.datagenerator);
//    /**************************************
//     ****** Initialize SimulatedMsg *******
//     **************************************/
//    switch(params.datagenerator) {
//    case 0 :
//        /*****************************************
//         * Data Distributor - 1 read pipe, multiple write pipes
//         * fps stats and timing for frame arrival delay, one Stats instance/test
//         * Transition from test to test recognized in SimulatedMsg based upon 
//         * multi-second delay since the last msg received.
//         * No external initialization of start time needed.
//         *****************************************/
//#ifdef MIT_DBG
//            pSimulatedMsg = new SimulatedMsg(params.maxTests,1,5000000,50000, std::string("usec"), params.maxWritePipes, params.dbg);  //Up to 5 seconds in 100 usec intervals
//#else
//            pSimulatedMsg = new SimulatedMsg(params.maxTests, 0, 1000, 1000, std::string("usec"), params.maxWritePipes, params.dbg);    //Up to 1 msec in 1 usec intervals
//#endif
//
//        break;
//
//    case 1 :
//    case 2 :
//        pSimulatedMsg = new SimulatedMsg(params.maxTests, params.dbg);
//        pSimulatedMsg->configureTiming(0,1000,1000,std::string("usec"), params.dbg);    //Up to 1msec in 1 usec intervals
//
//        pSimulatedMsg->getTimingStats()->setStartTime(nowSpec);
//        break;
//
//    default:
//        fprintf(stderr, "\nUnimplemented value of generator %d\n", params.datagenerator);
//        terminateProcess = true;
//        break;
//    }
//    SimulatedMsg & simulatedMsg = *pSimulatedMsg;
//
//    /**************************************
//     **** Initialize fps and sleepSpec ****
//     **************************************/
//    sleepIntervalRange = params.maxinterval - params.mininterval;
//    switch (params.datagenerator)
//    {
//        case 0:
//        {
//            if (pReadPipe == nullptr)
//            {
//                fprintf(stderr, "ERROR: data distributor read pipe not specified\n");
//                return;
//            }
//            else 
//                fprintf(stderr, "generateData thread: type=distributor, read from pipe, maximum write Pipes=%d\n",
//                            params.maxWritePipes);
//            break;
//        }
//        case 1:
//        {
//            /**********************************************************
//             * Use Case 1 Data Generator - 1 write pipe
//             * Timing stats for wakeup latency, one Stats instance/test
//             * Transition to next test recognized in generateData from 
//             * change in desiredFPS.
//             * DoSleep computes wakeup latency and updates timing stats.
//             **********************************************************/
//            if (sleepIntervalRange == 0 && params.mininterval != 0 )
//            {
//                constIntervalUsec = params.mininterval;
//                /********************************************
//                 * periodic operation
//                 *******************************************/
//                sleepSpec.tv_sec = constIntervalUsec/1e6;
//                sleepSpec.tv_nsec = 1000 * (constIntervalUsec-sleepSpec.tv_sec*1e6);
//                fprintf(stderr, "generateData thread: type=generator 1, constant period %d usec: mininterval=%d usec, maxinterval=%d usec, sleepIntervalRange=%d usec, max Write Pipes=%d\n",
//                            constIntervalUsec, params.mininterval, params.maxinterval, sleepIntervalRange, params.maxWritePipes);
//            }
//            else
//            {
//                //sleep interval is determined from the current FPS setting
//                while (desiredFPS == -1 || desiredFPS == 0)
//                {
//                    sleep(1);
//                }
//                int fps = desiredFPS;
//                prevDesiredFPS = fps;
//                std::string s(std::to_string(fps));
//                s += " FPS";
//                simulatedMsg.setTestName(s);
//                setSleepSpecFromFPS(fps, sleepSpec);
//                fprintf(stderr, "Location a: FPS initialized- fps=%d, prevDesiredFPS=%d\n", fps, prevDesiredFPS);
//                initialFPS = false;
//            }
//            break;
//        }
//        case 2:
//        {
//            /**********************************************************
//             * Use Case 2 Data Generator
//             **********************************************************/
//            if (sleepIntervalRange != 0)
//            {
//                /********************************************
//                 * Data Generator for Event Driven
//                 * Random interval between writes of auto-generated data
//                *******************************************/
//                // Seed the random number generator with the current timespec nsec value
//                clock_gettime(CLOCK_REALTIME, &nowSpec);
//                uint seed = (uint)(nowSpec.tv_nsec);
//                fprintf(stderr, "generateData thread: type=generator 2, seeding Random Number generator with seed %u (tv_nsec=%ld), num Write Pipes=%d\n", seed, nowSpec.tv_nsec, params.maxWritePipes);
//                srand(seed);
//            }
//            //else
//                /****************************************
//                 * Data Generator for Polling Driven 
//                 * Keep the pipe filled, so no delay between
//                 * writes.
//                 ****************************************/
//            break;
//        }
//    }
//
//    while (numPipes.load() == 0 && !terminateProcess) 
//    {
//        sleep(1);
//    }
//    
//    /****************************************************************
//     * Main DataGeneration Loop
//     ****************************************************************/
//    PipeStruct *p = pWritePipes;
//    clock_gettime(CLOCK_REALTIME, &nowSpec);
//    prevTimeSpec = nowSpec; //Reinitialize so we delay the next from this time
//
//    fprintf(stderr, "%s generateData - starting main loop\n", progName.c_str());
//    for (int n = 0, loopct=0; !terminateProcess; )
//    {
//        double mMsg;
//        /**************************************************************
//         * If Distributor (generator type 0), reads from pipe.  
//         * Updates fps and timing stats type 0.
//         *
//         * If Generator (generator type 1) creates msg.
//         **************************************************************/
//        if (!simulatedMsg.nextMsg(mMsg, pReadPipe)) 
//        {
//            fprintf(stderr, "generateData: simulatedMsg returned false.  breaking out of primary loop.\n");
//            break;
//        }
//        
//        while ((!terminateProcess) && p->fd == -1 )
//        {
//            if (n+1 >= maxWritePipes)
//            {
//                n = 0;
//                p = pWritePipes;
//
//                if (params.dbg) 
//                {
//                    clock_gettime(CLOCK_REALTIME, &nowSpec);
//                    fprintf(stderr, "%s: At %ld.%09ld secs, completed write to %d pipes\n",
//                        (params.datagenerator==0 ? "DataDistributor" : "DataGenerator"), nowSpec.tv_sec, nowSpec.tv_nsec, numPipes.load());
//                }
//                if (params.loops != -1)
//                {
//                    //For debugging purposes, this allows running just loops passes through all open pipes for the first FPS before terminating
//                    if (++loopct >= params.loops)
//                    {
//                        terminateProcess = true;
//                        break;
//                    }
//                }
//            }
//            else 
//            {
//                ++p;
//                ++n;
//            }
//        }
//        if (terminateProcess)
//            break;
//
//        if ( p->fd != -1)
//        {
//            write(p->fd, &mMsg, sizeof(mMsg));
//            if (params.datagenerator == 0)
//            {
//                //data distributor
//                simulatedMsg.getFPSStats()->newSample(n);
//            }
//        }
//
//        switch (params.datagenerator)
//        {
//            case 0: //data distributor Use Case 1
//                break;
//
//            case 1: //data generator Use Case 1
//            {
//                if (constIntervalUsec == 0 )   //This is the normal case
//                {
//                    int fps = desiredFPS;   //desired frames/second
//                    params.dbg>1 && fprintf(stderr, "Location b: initialFPS flag=%s, fps=%d, prevDesiredFPS=%d\n", (initialFPS?"true":"false"), fps, prevDesiredFPS);
//                    if (fps == (int)terminationMsg)
//                    {
//                        terminateProcess=true;  //This should be redundant, because monitorDesiredFPS should already have done it
//                        break;
//                    }
//                    if (fps != prevDesiredFPS)
//                    {
//                        timespec statsStartSpec;
//                        clock_gettime(CLOCK_REALTIME, &statsStartSpec);
//                        fprintf(stderr, "At %ld.%09ld: initialFPS flag=%s, new fps=%d, prevDesiredFPS=%d\n", 
//                                statsStartSpec.tv_sec, statsStartSpec.tv_nsec, (initialFPS?"true":"false"), fps, prevDesiredFPS);
//                        while (desiredFPS == 0)
//                        {
//                            sleep(1);
//                        }
//    
//                        fps = desiredFPS;
//                        clock_gettime(CLOCK_REALTIME, &statsStartSpec);
//                        fprintf(stderr, "At %ld.%09ld: fps is no longer 0.  initialFPS flag=%s, new fps=%d, prevDesiredFPS=%d\n", 
//                               statsStartSpec.tv_sec, statsStartSpec.tv_nsec, (initialFPS?"true":"false"), fps, prevDesiredFPS);
//    
//                        params.dbg && fprintf(stderr, "Location d: Calling startNewTest (new fps=%d, prevDesiredFPS=%d)\n", fps, prevDesiredFPS);
//                        simulatedMsg.startNextTest(statsStartSpec);
//                        std::string testName(std::to_string(fps));
//                        testName += " FPS";
//                        simulatedMsg.setTestName(std::to_string(fps));
//                        /********************************************
//                         * periodic operation
//                         *******************************************/
//                        setSleepSpecFromFPS(fps, sleepSpec);
//                        prevDesiredFPS = fps;
//                        clock_gettime(CLOCK_REALTIME, &statsStartSpec);
//                        prevTimeSpec = statsStartSpec;
//                    }    
//                }
//                break;
//            }
//        case 2: //data generator Use Case 2, Event Driven and polling driven
//            {
//                if (sleepIntervalRange != 0)
//                {
//                    //Event Driven - Calculate random sleep
//                    long sleepUsec = rand() % sleepIntervalRange + params.mininterval;
//                    sleepSpec.tv_sec - time_t(sleepUsec/1e6);
//                    sleepSpec.tv_nsec - 1000*(sleepUsec - (sleepSpec.tv_sec*1e6));
//                }
//                break;
//            }
//        }
//        
//        if (terminateProcess)
//        {
//            fprintf(stderr, "%s generateData - terminateProcess is true.  Exiting primary loop\n", progName.c_str());
//            break;
//        }
//
//        if (params.datagenerator == 1 || (params.datagenerator == 2 && sleepIntervalRange != 0) )
//            sleeper.doSleep(params, sleepSpec, prevTimeSpec, simulatedMsg.getTimingStats());
//    
//        if (++n >= maxWritePipes)  
//        {
//            n = 0;
//            if (params.loops != -1)
//            {
//                //For debugging purposes, this allow running just loops passes through all open pipes for the first FPS before terminating
//                if (++loopct >= params.loops)
//                {
//                    terminateProcess = true;
//                }
//            }
//        }
//        p = pWritePipes + n;
//    }
//    
//    /*************************************************
//     * Print the accumulated statistics
//     *************************************************/
//    clock_gettime(CLOCK_REALTIME, &nowSpec);
//    simulatedMsg.getTimingStats()->setEndTime(nowSpec);
//    if (params.datagenerator == 1)
//    {
//        sleeper.printSleepLatency(cerr);
//    }
//
//    fprintf(stderr, "%s generateData - exited main loop - terminateProcess=%s.  Sending terminationMsg (%d) to all open write pipes \n", 
//               progName.c_str(), (terminateProcess ? "true" : "false"), (int)terminationMsg );
//    for (int n = 0; n < maxWritePipes; ++n)
//    {
//        if (p->fd != -1)
//        {
//            write(p->fd, &terminationMsg, sizeof(terminationMsg));
//        }
//    }
//
//    fprintf(stderr, "%s generateData - calling simulatedMsg.printStats(cerr)\n", progName.c_str());
//    simulatedMsg.printStats(cerr);
//    delete pSimulatedMsg;
//}

/*********************************
 * openPipeAndGenerateData: a thread which writes simulated data to a single pipe.  
 * This is for UseCase 2, the threaded mode of operation,
 *     where we allocate one thread/pipe to execute this function, open its pipe and then write sumulated data to it
 *********************************/
void openPipeAndGenerateData(PipeStruct *pWritePipe, PipeStruct * pReadPipe, OptParams &params, std::atomic<int> &numPipes)
{
    if (pWritePipe->openPipe(O_WRONLY, progName) != -1)
    {
        std::atomic_int n(1);
        ++numPipes; //main has to know how many pipes are open
        generateData(pWritePipe, pReadPipe, params, n, 1);
    }
}

//Thread function which just does a blocking open on a write pipe
// and increments the atomic variable numPipes when the call returns successfully
void updateOpenWritePipes(PipeStruct *p, std::atomic<int> &numPipes)
{
    if (p->openPipe(O_WRONLY, progName) != -1)
        ++numPipes;
}

void signal_handler(int signal)
{
    fprintf(stderr, "\nSignal %d received.  Setting terminateProcess true.\n",signal);
    terminateProcess = true;
}

void start_low_latency(int &fd)
{
    if (fd < 0)
    {
        int value = 0;
        fd = open("/dev/cpu_dma_latency", O_RDWR);
        if (fd < 0)
        {
            fprintf(stderr, "start_low_latency: Failed to open PM QOS file: %s\n", strerror(errno));
            exit(errno);
        } 
        write(fd, &value, sizeof(value));
    }
}

void end_low_latency(int &fd)
{
    if (fd >= 0)
        close(fd);
    fd = -1;
}

int main(int argc, char *argv[])
{
    progName = argv[0];
    size_t index = progName.rfind('/');
    int pm_qos_fd = -1;

    if (index != std::string::npos)
        progName = progName.substr(index+1);

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGPIPE, signal_handler);
    signal(SIGQUIT, signal_handler);

    OptParams optParams;
    optParams.processOptions(argc, argv);

    PipeStruct * pWritePipes = new PipeStruct[optParams.maxWritePipes]; 
    PipeStruct * pReadPipe = nullptr;

    if (!createPipes(pWritePipes, &pReadPipe, optParams))
    {
        fprintf(stderr, "ERROR: %s Unable to create all required pipes - exiting\n", progName.c_str());
        exit(-1);
    }

    sleep(1);    //Sleep to allow reader(s) to open fifo

    std::atomic_int numPipes(0);
    std::thread singleThread;
    std::thread fpsMonitorThread;

    /********************************************
     * Create threads to generate/read data, publish,
     *  and monitor FPS (Use Case 1 Data Generator only)
     *******************************************/
    switch (optParams.datagenerator)
    {
    case 0: 
    case 1:
        {
            /****
             * DataGenerator(1 writer, no reader) 
             * or DataDistributor(1 read pipe, multiple write pipes)
             ****/
            if (optParams.bReadPipe)
            {
                //Data Distributor only
                if (pReadPipe->openPipe(O_RDONLY, progName) == -1)
                {
                    exit(-1);
                }
            }

            fprintf(stderr, "Use Case 1: %s Creating singleThread generateData \n", 
                    (optParams.datagenerator ? "DataDistributor":"DataGenerator") );
            singleThread = std::thread(generateData, pWritePipes, pReadPipe, std::ref(optParams), std::ref(numPipes), optParams.maxWritePipes );
        
            if (optParams.datagenerator == 1)
            {
                //Data Generator only
#ifdef LOCKALL
                if (mlockall(MCL_CURRENT|MCL_FUTURE) == -1)
                {
                    fprintf(stderr, "%s: mlockall failed (errno=%d)!\n",progName.c_str(), errno);
                    exit(-1);
                }
                start_low_latency(pm_qos_fd);
                fprintf(stderr, "%s: successfully executed mlockall and start_low_latency\n", progName.c_str());
#endif
                
                fprintf(stderr, "Creating fpsMonitorThread\n");
                fpsMonitorThread = std::thread(monitorDesiredFPS, fpsName, std::ref(desiredFPS), std::ref(optParams), 60 );
            }
        
            PipeStruct *p = pWritePipes;
            for (int i=0; i<optParams.maxWritePipes; ++i, ++p)
            {
                p->t = std::thread(updateOpenWritePipes, p, std::ref(numPipes));
            }
            break;
        }
    case 2:
        {
            /**** Use Case 2 - no reader ****/
            if (optParams.bThreaded) 
            {
                /****
                 * Data Generator for event driven: multiple independent write threads
                 * random timing of event generation
                 ****/
                PipeStruct *p = pWritePipes;
                for (int i=0; i<optParams.maxWritePipes; ++i, ++p)
                {
                    fprintf(stderr, "Use Case 2 Event-Driven: Creating generateData thread for write pipe #%d \n", i);
                    p->t = std::thread(openPipeAndGenerateData, p, pReadPipe, std::ref(optParams), std::ref(numPipes));
                }
                sleep(60);
                while (numPipes.load() == 0)
                {
                    sleep(10);
                }
                fprintf(stderr, "Use Case 2 Event Driven: %d pipes have been opened\n", numPipes.load());
            }
            else
            {
                /**** 
                 * Data Generator for polling driven: probably just one write thread, no delay
                 ****/
                optParams.mininterval = optParams.maxinterval = 0;
                PipeStruct *p = pWritePipes;
                for (int i=0; i<optParams.maxWritePipes; ++i, ++p)
                {
                    p->t = std::thread(updateOpenWritePipes, p, std::ref(numPipes));
                }
                fprintf(stderr, "Use Case 2 Polling: Creating singleThread generateData \n");
                singleThread = std::thread(generateData, pWritePipes, pReadPipe, std::ref(optParams), std::ref(numPipes), optParams.maxWritePipes );
            }
            break;
        }
    default:
        {
            fprintf(stderr, "WARNING %s: generator=%d not implemented\n", progName.c_str(), optParams.datagenerator);
        }
        break;
    }

    if (singleThread.joinable())
    {
        singleThread.join();
        fprintf(stderr, "%s: singleThread has been joined\n", progName.c_str());
    }
    else
        optParams.dbg && fprintf(stderr, "%s: singleThread is NOT joinable\n", progName.c_str());

    if (optParams.datagenerator == 1)
    {
        if (fpsMonitorThread.joinable())
        
        {
            fpsMonitorThread.join();
            fprintf(stderr, "%s: fpsThread has been joined\n", progName.c_str());
        }
        else
            optParams.dbg && fprintf(stderr, "%s: fpsMonitorThread is NOT joinable\n", progName.c_str());
    }    

    PipeStruct *p = pWritePipes;
    for (int n=0; n<optParams.maxWritePipes; ++n, ++p)
    {
        if (p->fd != -1 && p->t.joinable())
        {
            p->t.join();
            fprintf(stderr, "%s: Thread for fifo %s has been joined\n", progName.c_str(), p->fifoName.c_str());
            fprintf(stderr, "%s: Closing fifo %s\n", progName.c_str(), p->fifoName.c_str());
            close(p->fd);
        }
        else
        {
            optParams.dbg && fprintf(stderr, "%d: Thread #%d for fifo %s is NOT joinable\n",progName.c_str(), p->instance,  p->fifoName.c_str());
        }
    }
    if (optParams.datagenerator == 0 && pReadPipe != nullptr)
    {
        fprintf(stderr, "%s: Closing fifo %s\n", progName.c_str(), pReadPipe->fifoName.c_str());
        close(pReadPipe->fd);
    }

    fprintf(stderr, "%s: Exiting\n", progName.c_str());
    exit(0);
}


