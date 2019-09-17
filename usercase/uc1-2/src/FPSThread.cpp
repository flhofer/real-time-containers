#include <fcntl.h>
#include "DataGenerator.h"  //For terminateProcess  
#include "OptParams.h"
#include "FPSThread.h"

void monitorDesiredFPS(const char *fileName, int &desiredFPS, OptParams &params, int delaySecs=60)
{
//   PipeStruct fpsPipe;
    std::string funcName(progName + " monitorDesiredFPS");

    FILE * fpsIn = fopen(fileName, "r");
    if (fpsIn == nullptr)
    {
        fprintf(stderr, "ERROR %s failed to open %s\n", funcName.c_str(), fileName);
        terminateProcess = true;
        return;
    }

    int maxChars = 10; //Leave space for the newline and null
    char buf[maxChars];   
    int nxtFPS = 0;

    timespec spec;
    spec.tv_sec = 0;
    spec.tv_nsec = 250*1e6;  //sleep for 250 ms between checks
    bool initialFPS = true;
    int bytesRead = 0;
    while (!terminateProcess)
    {
        if (fgets(buf, maxChars, fpsIn) != buf )
        {
            if(feof(fpsIn))
                fseek(fpsIn, bytesRead, SEEK_SET);
            
            clock_nanosleep(CLOCK_REALTIME, 0, &spec, NULL);
            continue;
        }
        bytesRead+= strlen(buf);
        nxtFPS = std::stoi(buf);

        if (nxtFPS == (int)terminationMsg)
        {
            fprintf(stderr, "monitorDesiredFPS read nextFPS==-2.  Setting terminateProcess\n");
            terminateProcess = true;
            desiredFPS = (int)terminationMsg;    
        }
        else if (nxtFPS != 0  && nxtFPS != -1)
        {
            if (!initialFPS)
            {
                fprintf(stderr, "monitorDesiredFPS read nextFPS==%d.  Setting desiredFPS to 0 for %d seconds\n", nxtFPS, delaySecs);
                desiredFPS = 0;
                sleep(delaySecs);  //sleep to create very noticeable break in data
            }
            else 
            {
                initialFPS = false;
            }

            params.dbg && fprintf(stderr, "monitorDesiredFPS Setting desiredFPS to %d\n", nxtFPS);
            desiredFPS = nxtFPS;
        }
    }
    fclose(fpsIn);
}

/*************************
 * For Testing, where launching of new workers does not need to be synchronized with changes to the FPS
 ************************/
void timeBasedDesiredFPS(int &desiredFPS, int testSecs, int maxTests, int firstFPS, int lastFPS )
{
    bool initialFPS = true;

    int nxtFPS;
    int testNum;
    for(nxtFPS=firstFPS, testNum=0; nxtFPS < lastFPS+1 && testNum < maxTests; nxtFPS += 1, ++testNum )
    {
        if (!initialFPS)
        {
            fprintf(stderr, "\n=> timeBasedDesiredFPS nextFPS==%d.  Setting desiredFPS to 0 for 10 seconds\n", nxtFPS);
            desiredFPS = 0;
            sleep(10);  //sleep for 10 seconds to create very noticeable break in data
        }
        fprintf(stderr, "\n=> timeBasedDesiredFPS Setting desiredFPS to %d for %d sec\n\n", nxtFPS, testSecs);
        desiredFPS = nxtFPS;
        initialFPS = false;
        sleep(testSecs);
    }
    fprintf(stderr, "timeBasedDesiredFPS: Completed all %d tests or from %d FPS to %d FPS.  Setting terminateProcess\n", maxTests, firstFPS, lastFPS);
    terminateProcess = true;
}
