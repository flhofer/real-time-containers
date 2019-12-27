#pragma once
class OptParams;
void monitorDesiredFPS(const char *fifoName, int &desiredFPS, OptParams &params, int delaySecs);
void monitorFPSFileForTermination(const char *fifoName, int &desiredFPS, OptParams &params);

void timeBasedDesiredFPS(int &desiredFPS, int delaySecs, int maxTests, int firstFPS, int lastFPS);
