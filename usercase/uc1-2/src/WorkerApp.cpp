#include <cstdlib>
#include <stdio.h>
#include <string.h>
#include <string>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <sys/resource.h>
#include <getopt.h>
#include <algorithm>
#include "SimulatedMsg.h"
#include "PipeStruct.h"
#include "DataGenerator.h"

using namespace std;

int instance = -1;   //instance number
std::string progName("Worker");
std::string basePipeName("/tmp/Worker");
int sleepSecs = 1; //For use when debugging only
int innerloops=25;
int outerloops=150;
int pollPeriod=0;
int timedloops=0;
bool bDbg = false;
int maxTests = 8;
bool terminateProcess = false;

void testFunc(int oloops, int iloops, timespec &durationSpec, bool bDbg)
{
	char delim = ' ';
    timespec startSpec;

	clock_gettime(CLOCK_REALTIME, &startSpec);
	static string s("a b c d e f g h i j k l m n o p q r s t u v w x y z");
	for (int i=0; i<oloops; ++i) {
	    string s2 = s + s + s + s;
		for (int j=0; j<iloops; ++j) 
        { 
			reverse(begin(s2), end(s2));
            sort(s2.begin(), s2.end());
		}
	}

	clock_gettime(CLOCK_REALTIME, &durationSpec);
    durationSpec.tv_sec -= startSpec.tv_sec;
    durationSpec.tv_nsec -= startSpec.tv_nsec;
    if (durationSpec.tv_nsec < 0)
    {
        --durationSpec.tv_sec;
        durationSpec.tv_nsec += 1e9;
    }
    if (bDbg)
    {
        fprintf(stderr, "%s: %ld.%09ld Duration for %d outerloops and %d innerloops\n", progName.c_str(), durationSpec.tv_sec, durationSpec.tv_nsec, oloops, iloops); 
    }
}

void updateExpectedEndSpec(timespec &endspec, long pollPeriod)
{
    endspec.tv_nsec += pollPeriod*1000;
    if (endspec.tv_nsec >= 1e9)
    {
        ++endspec.tv_sec;
        endspec.tv_nsec -= 1e9;
    }
}

void workerFunc(int tloops, int iloops, int oloops, int pollPeriod)
{
    PipeStruct readPipe;
    int result;
    if (pollPeriod == 0)
        result = readPipe.createPipe(instance, basePipeName + "_" + std::to_string(instance), progName);
    else
        result = readPipe.createPipe(instance, basePipeName, progName);

    if (result == 0 || readPipe.openPipe(O_RDONLY, progName) == -1)
    {
        exit(-1);
    }
    
    SimulatedMsg simulatedMsg(maxTests, 0,  1000, 1000, std::string("usecs"), 0, 0);

    struct timespec durationSpec;
    double inMsg;
    const int maxMsg = 200;
    double msg[maxMsg+1];
    int i = 0;
    int loopCt = 0;
    double totalDuration = 0.0;
    struct timespec startSpec, endSpec, delaySpec;
    clock_gettime(CLOCK_REALTIME, &startSpec);
    if (pollPeriod != 0)
    {
        sched_yield();
        clock_gettime(CLOCK_REALTIME, &endSpec);
    }
    if (bDbg)
    {
        fprintf(stderr, "WorkerFunc initial time (CLOCK_REALTIME) = %lu\n", startSpec.tv_sec * 1e9 + startSpec.tv_nsec);
    }

    while (!terminateProcess)
    {
        if (pollPeriod == 0)
        {
            //Use Case 1 - compute apparent FPS and collect Tw - Tg delay stats
            //Use Case 2 - event driven collect Tw - Tg delay stats (FPS is collected but useless)
            if (!simulatedMsg.nextMsg(inMsg, &readPipe))
            {
                break;
            }
        }
        else 
        {
            //Use Case 2 Polling-Driven
            if (!simulatedMsg.readDouble(inMsg, &readPipe) || inMsg == terminationMsg)
            {
                terminateProcess = true;
                fprintf(stderr, "Ending Test\n");
                clock_gettime(CLOCK_REALTIME, &endSpec);
                simulatedMsg.getTimingStats()->setEndTime(endSpec);
                break;
            }
        }
        
        if (oloops != 0 && iloops != 0)
        {
            testFunc(oloops, iloops, durationSpec, bDbg);
            totalDuration += (1e9*durationSpec.tv_sec + durationSpec.tv_nsec);
        } 
        else if (sleepSecs > 0)
        {
            sleep(sleepSecs);  //Debugging only
        }

        if (  tloops && ++loopCt >= tloops ) 
        {
            if (bDbg)
            {
    	        clock_gettime(CLOCK_REALTIME, &durationSpec);
                durationSpec.tv_sec -= startSpec.tv_sec;
                durationSpec.tv_nsec -= startSpec.tv_nsec;
                if (durationSpec.tv_nsec < 0)
                {
                    --durationSpec.tv_sec;
                    durationSpec.tv_nsec += 1e9;
                }
                fprintf(stderr, "%s: Duration for %d testFunc calls = %ld.%09ld seconds\n", progName.c_str(), loopCt, durationSpec.tv_sec, durationSpec.tv_nsec);
            }
            if (oloops != 0 && iloops != 0)
            {
                fprintf(stderr, "%s: Average runtime of testFunc over %d calls is %ld usecs\n", 
                    progName.c_str(), loopCt, (long)(totalDuration/(1000*loopCt)) );
            }
	        if (bDbg)
                clock_gettime(CLOCK_REALTIME, &startSpec);
            loopCt = 0;
            totalDuration = 0.0;
        }

        if (pollPeriod != 0)
        {
            updateExpectedEndSpec(endSpec, pollPeriod);
            sched_yield();
        	clock_gettime(CLOCK_REALTIME, &delaySpec);
            long delayUsec = (delaySpec.tv_nsec - endSpec.tv_nsec) * 1000 + (delaySpec.tv_sec - endSpec.tv_sec) * 1e6;
            simulatedMsg.getTimingStats()->update(delayUsec);
        }
    }
 
    fprintf(stderr, "WorkerApp exiting\n");
    simulatedMsg.printStats(cerr);
}

void printHelp(std::string msg)
{
    if (msg.length() > 0)
    {
            printf("%s\n",msg.c_str());
    }
    printf("\n%s\n",progName.c_str());
    printf("USAGE: [--help] --instnum <n> [--outerloops <n>] [--innerloops <n>] [--dbg] [--readpipe <name>] [--timedloops <n>] \n");
    printf("       --instnum <n>    : ZERO-BASED instance number (required)\n");
    printf("       --innerloops <n> : testFunc innerloops (default %d)\n", innerloops);
    printf("       --outerloops <n> : testFunc outerloops (default %d)\n", outerloops);
    printf("       --maxTests   <n> : number of tests (default %d)\n", maxTests);
    printf("       --pollPeriod <n> : usec polling period (default %d)\n", pollPeriod);
    printf("       --readpipe<name>     : base name of communication pipe (default %s)\n", basePipeName.c_str());
    printf("       --timedloops <n> : turn on logging of n-loop duration (default 0=disabled)\n");
    printf("       --help           : print Usage \n");
    printf("       --dbg            : produce extra output; sleep %d seconds rather than running inner & outer loops\n", sleepSecs);
}
    
int main(int argc, char *argv[])
{

    int optargs = 0;
    const char * const short_opts = "b:dhi:m:n:o:p:t:";
    const option long_opts[] = {
        {"basePipeName",required_argument, nullptr, 'b'},
        {"dbg",        no_argument,        nullptr, 'd'},
        {"help",       no_argument,        nullptr, 'h'},
        {"innerloops", required_argument,  nullptr, 'i'},
        {"maxTests",   required_argument,  nullptr, 'm'},
        {"instnum",    required_argument,  nullptr, 'n'},
        {"outerloops", required_argument,  nullptr, 'o'},
        {"pollPeriod", required_argument,  nullptr, 'p'},
        {"timedloops", required_argument,  nullptr, 't'}
    };

    while (true) {
        const auto  opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);
        if (opt == -1)
            break;
        switch(opt)
        {
            case 'b':
                basePipeName = optarg;
                break;
            case 'd':
                bDbg = true;
                break;
            case 'i':
                innerloops = stoi(optarg);
                if (innerloops < 0) 
                {
                    printHelp(std::string("\nERROR: Invalid innerloops value: ") + optarg);
                    exit(-1);
                }
                break;
            case 'm':
                maxTests = std::stoi(optarg);
                break;
            case 'n':
                instance = std::stoi(optarg);
                if (instance < 0) 
                {
                    printHelp(std::string("\nInvalid instnum value: ") + optarg);
                    exit(-1);
                }
                break;
            case 'o':
                outerloops = stoi(optarg);
                if (outerloops < 0) 
                {
                    printHelp(std::string("\nInvalid outerloops value: ") + optarg);
                    exit(-1);
                }
                break;
            case 'p':
                pollPeriod = stoi(optarg);
                break;
            case 't':
                timedloops = std::stoi(optarg);
                if (timedloops < 0) 
                {
                    printHelp(std::string("\nInvalid timedloops value: ") + optarg);
                    exit(-1);
                }
                break;
            case 'h':
                printHelp("");
                exit(0);
            case '?':
            default:
                printHelp("");
                exit(-1);
        }
    }
    if (instance==-1)
    {
        printHelp(std::string("\nMissing required argument \'--instnum <instance number>\'\n"));
        exit(-1);
    }
    progName = progName + "_" + std::to_string(instance);

    fprintf(stderr, "%s: Running with %d innerloops, %d outerloops, %d maxTests, %s_%d readPipe\n", 
           progName.c_str(), innerloops, outerloops, maxTests, basePipeName.c_str(), instance);
    workerFunc(timedloops, innerloops, outerloops, pollPeriod);
    fprintf(stderr, "%s: exiting\n", progName.c_str());
}
