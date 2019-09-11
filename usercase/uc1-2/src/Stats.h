#pragma once
#include <map>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

typedef std::map<unsigned long, unsigned long>  HistogramEntries;

class Histogram {
    unsigned long expMinValue;
    unsigned long expMaxValue;
    int           interval;
    HistogramEntries entries;
    std::string hUnits;
    int        bDbg;
public:
    Histogram(unsigned long minValue, unsigned long maxValue, unsigned long ct, const std::string &units, const std::string &statName, int dbg );
    ~Histogram() {}
    void update(unsigned long value);
    std::ostream & histPrint(std::ostream &os, double scalingFactor, std::string &units);
};

class Stats {
protected:
    std::string statName;
    unsigned long minValue;
    unsigned long maxValue;
    double totalValue;
    unsigned long ctr;
    std::string units;
    double scaling;
    std::string scaledUnits;
    Histogram histogram;
    struct timespec startTime;
    struct timespec endTime;
    std::string testName;
    int maxWorkers;
    std::vector<int> samples;

public:
    Stats(const char *sName, unsigned long hMin, unsigned long hMax, unsigned long hCt, const std::string &statUnits, int numWorkers, int dbg);
    ~Stats() {}
    void update(unsigned long v);
    std::ostream & statsPrint(std::ostream &os);
    void setStatsPrintScaling(double scaling, const std::string &statUnits);
    void setStartTime(struct timespec &t);
    void setEndTime(struct timespec &t);
    unsigned long getCtr();
    void newSample(int workerInstance);
};

std::ostream & operator<<(std::ostream &os, Stats &stats);

