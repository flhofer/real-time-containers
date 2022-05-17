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
unsigned long pollPeriod=0;
unsigned long deadline = 0;
unsigned long runtime = 0;
unsigned long endInSeconds = 0;

int timedloops=0;
bool bDbg = false;
int maxTests = 8;
volatile bool terminateProcess = false;

timespec calculateTimeDifference(const timespec& first, const timespec& second){

    timespec diff = {};
    diff.tv_sec = first.tv_sec - second.tv_sec;
    diff.tv_nsec = first.tv_nsec - second.tv_nsec;
    
    if (diff.tv_nsec < 0)
    {
        --diff.tv_sec;
        diff.tv_nsec += 1e9;
    }

    return diff;
}

timespec addTime(timespec& spec, unsigned long addtime){

    spec.tv_nsec += addtime;

    if (spec.tv_nsec >= 1e9)
    {
        ++spec.tv_sec;
        spec.tv_nsec -= 1e9;
    }
    else if (spec.tv_nsec < 0)
    {
        --spec.tv_sec;
        spec.tv_nsec += 1e9;
    }

    return spec;
}

timespec keepProcessorBusy(int oloops, int iloops, bool bDbg)
{
    struct timespec startSpec = {0};

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

    struct timespec durationSpec = {0};

	clock_gettime(CLOCK_REALTIME, &durationSpec);

    durationSpec = calculateTimeDifference(durationSpec, startSpec);    

    if (bDbg)
    {
        fprintf(stderr, "%s: %ld.%09ld Duration for %d outerloops and %d innerloops\n", progName.c_str(), durationSpec.tv_sec, durationSpec.tv_nsec, oloops, iloops); 
    }

    return durationSpec;
}

void updateExpectedSpecs(timespec &periodSpec, unsigned long period, timespec &deadlineSpec, unsigned long deadline)
{

    // calculate first for deadline, then for period
    deadlineSpec = periodSpec;

    deadlineSpec.tv_nsec += deadline;
    if (deadlineSpec.tv_nsec >= 1e9)
    {
        ++deadlineSpec.tv_sec;
        deadlineSpec.tv_nsec -= 1e9;
    }

    periodSpec.tv_nsec += period;
    if (periodSpec.tv_nsec >= 1e9)
    {
        ++periodSpec.tv_sec;
        periodSpec.tv_nsec -= 1e9;
    }
}

void workerFunc(int tloops, int iloops, int oloops, unsigned long pollPeriod, ulong deadline, ulong runtime, ulong endInSeconds)
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

    UC2Log uc2Log(runtime, deadline, tloops);

    double inMsg;
    const int maxMsg = 200;
//    double msg[maxMsg+1];	// thought as future receive buffer
//    int i = 0;
    int loopCt = 0;
    ulong totalLoops = 0;
    long double totalDuration = 0.0, totalPerInterval = 0.0;
    struct timespec startTime = {0}, executionActualStartSpec = {0}, executionActualEndSpec = {0}, periodExpectedSpec = {0}, periodDeadlineSpec = {0};
    struct timespec keepBusyDuration = {0};

    if (pollPeriod != 0)
    {
        sched_yield();
        clock_gettime(CLOCK_REALTIME, &periodExpectedSpec);

        periodDeadlineSpec = periodExpectedSpec;
        addTime(periodDeadlineSpec, deadline);

        fprintf(stderr, "Starting value of periodExpectedSpec = %ld.%09ld, endInSeconds %lu\n", periodExpectedSpec.tv_sec, periodExpectedSpec.tv_nsec, endInSeconds);
    }

    clock_gettime(CLOCK_REALTIME, &startTime);

    while (!terminateProcess)
    {
        clock_gettime(CLOCK_REALTIME, &executionActualStartSpec);

        totalLoops++;

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
                clock_gettime(CLOCK_REALTIME, &periodExpectedSpec);
                break;
            }
        }

        // see if time duration is set, exit after the time, 
        if(endInSeconds > 0){

            struct timespec currentTime = {0};
            clock_gettime(CLOCK_REALTIME, &currentTime);

            currentTime.tv_sec -= endInSeconds;

            currentTime = calculateTimeDifference(currentTime, startTime);
    
            // when the current time is more than duration, 
            if(currentTime.tv_sec > 0){
                fprintf(stderr, "Exiting due to duration end.  Setting terminateProcess\n");
                terminateProcess = true;
                break;
            }
        }   
        
        if (oloops != 0 && iloops != 0)
        {
            keepBusyDuration = keepProcessorBusy(oloops, iloops, bDbg);
            totalDuration += (1e9 * keepBusyDuration.tv_sec + keepBusyDuration.tv_nsec);
        }

        if (  tloops && ++loopCt >= tloops ) 
        {
            if (oloops != 0 && iloops != 0 && pollPeriod == 0)
            {
                fprintf(stderr, "%s: Average runtime of testFunc over %d calls is %ld nsecs\n", 
                    progName.c_str(), loopCt, (long)(totalDuration/(loopCt)) );
            }
            
            uc2Log.addAverateRuntime((ulong)(totalPerInterval/(loopCt)));

            loopCt = 0;
            totalDuration = 0.0;
            totalPerInterval = 0.0;
        }

        if (pollPeriod != 0)
        {
        	clock_gettime(CLOCK_REALTIME, &executionActualEndSpec);

            struct timespec actualRuntime = calculateTimeDifference(executionActualEndSpec, executionActualStartSpec);
            long double actualrunNsec = actualRuntime.tv_nsec + actualRuntime.tv_sec * 1e9;

            totalPerInterval += actualrunNsec;

            if(actualrunNsec > runtime)
            {
                long double keepBusyNSec = keepBusyDuration.tv_nsec + keepBusyDuration.tv_sec * 1e9;
                // Overrun situation
                uc2Log.addOverrun(Overrun((ulong) actualrunNsec, (ulong) keepBusyNSec, executionActualEndSpec));
            }

            // Execution must start before executionExpectedStartSpec or else its period violation
            timespec executionExpectedStartSpec = periodExpectedSpec;
            addTime(executionExpectedStartSpec, deadline - runtime);

            long double periodNsec = (executionActualStartSpec.tv_nsec - executionExpectedStartSpec.tv_nsec) + (executionActualStartSpec.tv_sec - executionExpectedStartSpec.tv_sec) * 1e9;

            if(periodNsec > 0) {
                // Period violation situation
                uc2Log.addViolatedPeriod(ViolatedPeriod(executionExpectedStartSpec, executionActualStartSpec, periodNsec));
            }

            addTime(periodExpectedSpec, pollPeriod);
            periodDeadlineSpec = periodExpectedSpec;
            addTime(periodDeadlineSpec, deadline);

            sched_yield();

        }
    }
 
    fprintf(stderr, "WorkerApp exiting\n");

    if(pollPeriod != 0)
    {
        uc2Log.printSummary(cerr, totalLoops);        
    }
    else
    {
        simulatedMsg.printStats(cerr);
    }
    
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
    printf("       --pollPeriod <n> : nsec polling period (default %ld)\n", pollPeriod);
    printf("       --dline      <n> : nsec deadline for use case 2 (default %lu)\n", deadline);
    printf("       --rtime      <n> : nsec runtime for use case 2 (default %lu)\n", runtime);
    printf("       --readpipe<name> : base name of communication pipe (default %s)\n", basePipeName.c_str());
    printf("       --timedloops <n> : turn on logging of n-loop duration (default 0=disabled)\n");
    printf("       --endInSeconds<n>: time in seconds which decides lifetime of execution (default %lu)\n", endInSeconds);
    printf("       --help           : print Usage \n");
    printf("       --dbg            : produce extra output; sleep %d seconds rather than running inner & outer loops\n", sleepSecs);
}
    
int main(int argc, char *argv[])
{

    const char * const short_opts = "b:dhi:m:n:o:p:l:r:t:e:";
    const option long_opts[] = {
        {"basePipeName",required_argument, nullptr, 'b'},
        {"dbg",        no_argument,        nullptr, 'd'},
        {"help",       no_argument,        nullptr, 'h'},
        {"innerloops", required_argument,  nullptr, 'i'},
        {"maxTests",   required_argument,  nullptr, 'm'},
        {"instnum",    required_argument,  nullptr, 'n'},
        {"outerloops", required_argument,  nullptr, 'o'},
        {"pollPeriod", required_argument,  nullptr, 'p'},
        {"dline",      required_argument,  nullptr, 'l'},
        {"rtime",      required_argument,  nullptr, 'r'},
        {"endInSeconds",required_argument,  nullptr, 'e'},
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
            case 'l':
                deadline = stoul(optarg);
                break;
            case 'r':
                runtime = stoul(optarg);
                break;                
            case 't':
                timedloops = std::stoi(optarg);
                if (timedloops < 0) 
                {
                    printHelp(std::string("\nInvalid timedloops value: ") + optarg);
                    exit(-1);
                }
                break;
            case 'e':
                endInSeconds = stol(optarg);
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

    fprintf(stderr, "Printing...");

    fprintf(stderr, "%s: Running with %d innerloops, %d outerloops, %d maxTests, %s_%d readPipe, pollPeriod %lu, deadline %lu, runtime %lu, endInSeconds %lu\n",
           progName.c_str(), innerloops, outerloops, maxTests, basePipeName.c_str(), instance, pollPeriod, deadline, runtime, endInSeconds);
    workerFunc(timedloops, innerloops, outerloops, pollPeriod, deadline, runtime, endInSeconds);
    fprintf(stderr, "%s: exiting\n", progName.c_str());
}
