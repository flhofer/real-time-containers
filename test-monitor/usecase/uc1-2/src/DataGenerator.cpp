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
volatile bool terminateProcess{false};
int desiredFPS{-1};
const char * fpsName = "/tmp/fps";

#define STRINGIFY(s) #s
#define FILELINE(line) __FILE__ ":" STRINGIFY(line)

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

        std::string fifoName = optParams.baseWritePipeName + "_" + std::to_string(n+optParams.startWritePipes);

        // keep base name only for poll driven generator, UC2 non threaded
        if(optParams.datagenerator == 2 && !optParams.bThreaded){
            fifoName = optParams.baseWritePipeName;
        }

        if (!p->createPipe(n, fifoName, progName))
            return false;
    }
    return true;
}

/*********************************
 * openPipeAndGenerateData: a thread which writes simulated data to a single pipe.  
 * This is for UseCase 2, the threaded mode of operation,
 *     where we allocate one thread/pipe to execute this function, open its pipe and then write simulated data to it
 *********************************/
void openPipeAndGenerateData(PipeStruct *pWritePipe, PipeStruct * pReadPipe, OptParams &params, std::atomic<int> &numPipes)
{
    if (pWritePipe->openPipe(O_WRONLY, progName) != -1)
    {
        std::atomic_int n(1);
        ++numPipes; //main has to know how many pipes are open
        generateData(pWritePipe, pReadPipe, params, n, 1);
    }
    else
    {
        fprintf(stderr, FILELINE(__LINE__)" Error Opening pipe for %s\n", progName.c_str());
    }
    
}

//Thread function which just does a blocking open on a write pipe
// and increments the atomic variable numPipes when the call returns successfully
void updateOpenWritePipes(PipeStruct *p, std::atomic<int> &numPipes)
{
    if (p->openPipe(O_WRONLY, progName) != -1){
        ++numPipes;
    }
    else
    {
        fprintf(stderr, FILELINE(__LINE__)": Error Opening pipe for %s\n", progName.c_str());
    }
        
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
        (void)write(fd, &value, sizeof(value));
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
#ifdef LOCKALL
    int pm_qos_fd = -1;
#endif

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
    std::thread monitorThread;

    /* Delete and recreate FPS file */
    
    int fd = open(fpsName, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    close(fd);

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
                    fprintf(stderr, FILELINE(__LINE__)": Error Opening pipe for %s\n", progName.c_str());

                    exit(-1);
                }
            }

            fprintf(stderr, "Use Case 1: %s Creating singleThread generateData \n", 
                    (optParams.datagenerator == 0 ? "DataDistributor":"DataGenerator") );
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
                
                fprintf(stderr, "Creating monitorThread desiredFPS: %d\n", desiredFPS);
                monitorThread = std::thread(monitorDesiredFPS, fpsName, std::ref(desiredFPS), std::ref(optParams), 60 );
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
           
            fprintf(stderr, "Thread to control worker process lifetime.\n");
            monitorThread = std::thread(monitorFPSFileForTermination, fpsName, std::ref(desiredFPS), std::ref(optParams));

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

    if (optParams.datagenerator >= 1)
    {
        if (monitorThread.joinable())
        {
            monitorThread.join();
            fprintf(stderr, "%s: monitorThread has been joined\n", progName.c_str());
        }
        else
            optParams.dbg && fprintf(stderr, "%s: monitorThread is NOT joinable\n", progName.c_str());
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
            optParams.dbg && fprintf(stderr, "%s: Thread #%d for fifo %s is NOT joinable\n",progName.c_str(), p->instance,  p->fifoName.c_str());
        }
    }
    if (optParams.datagenerator == 0 && pReadPipe != nullptr)
    {
        fprintf(stderr, "%s: Closing fifo %s\n", progName.c_str(), pReadPipe->fifoName.c_str());
        close(pReadPipe->fd);
    }
#ifdef LOCKALL
    end_low_latency(pm_qos_fd);
#endif

    fprintf(stderr, "%s: Exiting\n", progName.c_str());
    exit(0);
}


