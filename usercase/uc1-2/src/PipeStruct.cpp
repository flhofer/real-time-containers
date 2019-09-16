#include "PipeStruct.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

PipeStruct::PipeStruct() :
    instance(-1),
    fd(-1),
    fifoName("None")
{}

PipeStruct::~PipeStruct() 
{
    if (fd != -1)
        close(fd);
}

int PipeStruct::createPipe(int i, const std::string pipeName, std::string& progName)
{
    instance = i;
    int rval = 1;
    fifoName = pipeName;
    if (mkfifo(fifoName.c_str(), 0666) && errno != EEXIST)
    {
        fprintf(stderr, "ERROR %s: mkfifo failed to create fifo %s (errno=%d)\n", progName.c_str(), fifoName.c_str(), errno);
        rval = 0;
    }
    fprintf(stderr, "%s: Created pipe %s\n", progName.c_str(), fifoName.c_str());
    return rval;
}

int PipeStruct::openPipe(int openMode, std::string & progName)
{
    if (-1 != (fd = open(fifoName.c_str(), openMode) ) ) //open in blocking mode
    {
        fprintf(stderr, "%s: Successfully opened fifo %s for %s. File Descriptor is %d\n", progName.c_str(), fifoName.c_str(), ((openMode & O_WRONLY) ? "Writing" : "Reading"), fd);
    }
    else if (errno != EINTR)
    {
        fprintf(stderr, "ERROR %s: Failed to open fifo %s for %s. \n", progName.c_str(), fifoName.c_str(), ((openMode & O_WRONLY) ? "Writing" : "Reading"));
    }
    else
    {
        fprintf(stderr, "NOTE %s: PipeStruct::openPipe was interrupted\n", progName.c_str());
    }
    return fd;
}
