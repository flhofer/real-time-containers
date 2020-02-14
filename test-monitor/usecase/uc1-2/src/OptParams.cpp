#include <stdio.h>
#include <string>
#include <getopt.h>
#include "OptParams.h"
#include "DataGenerator.h"

using namespace std;

OptParams::OptParams() :
    dbg(0),             //0=>no debug messages; 1=>minimal; 2=>all
    loops( -1),         //debugging only
    mininterval( 0),
    maxinterval( 0),
	startWritePipes( 0),
    maxWritePipes( 10),
    bThreaded( false),  //Use Case 2 only
    bTimeSleep( true),
    bReadPipe( false),
    readPipeName("none"),
    baseWritePipeName("/tmp/worker"),
    timingHistMinValue(1),		 // TODO: unused. what is this for?
    timingHistMaxValue(5000000), // TODO: unused. what is this for?
    histCount(10000),		// TODO: unused. what is this for?
    maxTests(8),
    testSecs(6666660),		// TODO: unused. what is this for?
    endInSeconds(0),
    firstFPS(24),			// debugging only? TODO: unused. what is this for?
    lastFPS(64),       		// debugging only? TODO: unused. what is this for?
    datagenerator(1)
{}

void OptParams::printHelp(std::string msg)
{
    if (msg.length() > 0)
    {
        printf("\n%s\n",msg.c_str());
    }
    printf("USAGE: %s [--help] | [ [--BaseWritePipeName <name>] [generator <n>] [--dbg] [--flag] [--histMin <n>] [--histMax <n>] [--histCount <-n>] [--loops <n>] [--mininterval <usecs> [--maxinterval <usecs>] [--maxTests <n>] [--maxWritePipes <n>] [--readpipe <name>] [--sleeptimer] [--threaded] [--testSecs <n> ] \n", progName.c_str());
    printf("\n");
    printf("       --help        (-h)        : help\n");
    printf("       --baseWritePipeName (-w) <name>: base name of pipes to be written (default %s)\n", baseWritePipeName.c_str());
    printf("       --dbg         (-d) <n>    : produce more output (default %d, 1=>minimal, 2=>all\n", dbg);
    printf("       --firstFPS    (-i) <n>    : (testing ONLY) firstFPS (default %d)\n", firstFPS );
    printf("       --generator   (-g) <n>    : 0->UseCase1 distributor, 1=>UseCase 1 Event generator, 2=>UseCase2 Polling-Driven Generator (default %d)\n",datagenerator );
    printf("       --histMin     (-a)        : minimum usecs for delay/duration histogram (default %lu)\n", timingHistMinValue);
    printf("       --histMax     (-b)        : maximum usecs for delay/duration histogram (default %lu\n", timingHistMaxValue);
    printf("       --histCount   (-c)        : bin count for delay/duration histogram (default %lu\n", histCount);
    printf("       --lastFPS     (-j) <n>    : (testing ONLY) lastFPS (default %d)\n", lastFPS );
    printf("       --loops       (-l) <n>    : maximum number of inputs per pipe (default unlimited)\n" );
    printf("       --mininterval (-m) <usec> : if NOT reading from a pipe, minimum time between simulated inputs (usecs) (default %d)\n", mininterval );
    printf("       --maxinterval (-x) <usec> : if NOT reading from a pipe, maximum time between simulated inputs (usecs) (default %d)\n", maxinterval);
    printf("       --maxTests    (-n) <n>    : number of tests (default %d)\n", maxTests);
    printf("       --maxWritePipes(-p) <n>   : number of write pipes (default %d)\n", maxWritePipes);
    printf("       --readpipe    (-r) <name> : name of pipe from which to read (default none)\n");
    printf("       --sleeptimer  (-s)        : log the actual duration of sleep interval between simulated inputs (default %s)\n", (bTimeSleep?"true":"false") );
    printf("       --startWritePipes(-S) <n> : offset for of write pipes (default %d)\n", startWritePipes);
    printf("       --threaded    (-t)        : multi-threaded (one thread per write pipe) (default false)\n");
    printf("       --testSecs    (-n) <n>    : (testing only) seconds between FPS changes by timeBasedDesiredFPS (default %d)\n", testSecs);
    printf("       --endInSeconds(-n) <n>    : controls lifetime of Process, value must be in seconds (default %lu)\n", endInSeconds);
    printf("\n");
    printf("NOTE: short form of all options is also accepted, eg -h for --help\n");
    printf("NOTE: if mininterval == maxinterval, time between simulated inputs is constant\n");
}

int OptParams::processOptions(int argc, char **argv)
{

    int optargs = 0;
    const char * const short_opts = "a:b:d:g:hi:j:l:m:n:p:r:sS:tw:x:z:e:?";
    const option long_opts[] = {
    {"histMin",     required_argument,  nullptr, 'a'},
    {"histMax",     required_argument,  nullptr, 'b'},
    {"histCount",   required_argument,  nullptr, 'c'},
    {"dbg",         required_argument,  nullptr, 'd'},
    {"generator",   required_argument, nullptr, 'g'},
    {"help",        no_argument,       nullptr, 'h'},
    {"firstFPS",    required_argument, nullptr, 'i'},
    {"lastFPS",     required_argument, nullptr, 'j'},
    {"loops",       required_argument, nullptr, 'l'},
    {"mininterval", required_argument, nullptr, 'm'},
    {"maxTests",    required_argument, nullptr, 'n'},
    {"maxWritePipes", required_argument, nullptr, 'p'},
    {"readpipe",    required_argument, nullptr, 'r'},
    {"sleeptimer",  no_argument,       nullptr, 's'},
    {"startWritePipes", required_argument, nullptr, 'S'},
    {"threaded",    no_argument,       nullptr, 't'},
    {"baseWritePipeName",   required_argument, nullptr, 'w'},
    {"maxinterval", required_argument, nullptr, 'x'},
    {"testSecs", required_argument, nullptr, 'z'},
    {"endInSeconds", required_argument, nullptr, 'e'},
    {0,0,0,0}
    };

    while (true) {
        const auto  opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);
        if (opt == -1)
            break;
        switch(opt)
        {
        case 'a':
            timingHistMinValue = std::stoul(optarg);
            break;
        case 'b':
            timingHistMaxValue = std::stoul(optarg);
            break;
        case 'c':
            histCount = std::stoul(optarg);
            break;
        case 'd':
            dbg = std::stoi(optarg);
            break;
        case 'g':
            datagenerator = std::stoi(optarg);
            break;
        case 'i':
            firstFPS = std::stoi(optarg);
            break;
        case 'j':
            lastFPS = std::stoi(optarg);
            break;
        case 'l':
            loops = std::stoi(optarg);
            break;
        case 'm':
            mininterval = std::stoi(optarg);
            break;
        case 'n':
            maxTests = std::stoi(optarg);
            break;
        case 'p':
            maxWritePipes = std::stoi(optarg);
            break;
        case 'r':
            bReadPipe = true;
            readPipeName = optarg;
            break;
        case 's':
            bTimeSleep = true;
            break;
        case 'S':
        	startWritePipes = std::stoi(optarg);
            break;
        case 't':
            bThreaded = true;
            break;
        case 'w':
            baseWritePipeName = optarg;
            fprintf(stderr, "baseWritePipeName changed to %s\n", baseWritePipeName.c_str());
            break;
        case 'x':
            maxinterval = std::stoi(optarg);
            break;
        case 'z':
            testSecs = std::stoi(optarg);
            break;
        case 'e':
            endInSeconds = std::stol(optarg);
            break;
        case 'h':
            printHelp("\n");
            exit(0);
        case '?':
        default:
            printHelp(std::string(argv[0]));
            exit(-1);
        }
    }
    if (bReadPipe)
    {
        mininterval = maxinterval = 0;
    }
    fprintf(stderr, "%s started (generator %d) with maxTests=%d, maxWritePipes=%d offset %d, baseWritePipe=%s, readPipe=%s timeSleep option=%s, timingHistMin/Max/Count=%lu/%lu/%lu\n",progName.c_str(), datagenerator, maxTests, maxWritePipes, startWritePipes, baseWritePipeName.c_str(), readPipeName.c_str(), (bTimeSleep?"True":"False"),timingHistMinValue,timingHistMaxValue,histCount);
    return 0;
}
