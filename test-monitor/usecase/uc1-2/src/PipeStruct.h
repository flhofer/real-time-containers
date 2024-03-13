#pragma once
#include <thread>
#include <string>

class PipeStruct {
public:
    int instance;
    int fd;
    std::string fifoName;
    std::thread t;
    PipeStruct();
    int createPipe(int i, std::string pipeName, std::string &progName);
    int openPipe(int openMode, std::string &progName);
    ~PipeStruct(); 
};
