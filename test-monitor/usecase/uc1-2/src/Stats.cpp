#include "Stats.h"
#include <stdio.h>
#include <ostream>
#include <limits.h>
#include <iostream>


Histogram::Histogram(unsigned long minValue, unsigned long maxValue, unsigned long ct, const std::string &units, const std::string &statName, int dbg):
    expMinValue(minValue),
    expMaxValue(maxValue),
    interval((maxValue-minValue)/ct),
    hUnits(units),
    bDbg(dbg)
{
    unsigned long k = minValue;
    for (unsigned long i = 0; i<ct; ++i, k+=interval)
    {
        entries.insert( std::pair<unsigned long, unsigned long>(k,0) );
    }
    if (entries.rbegin()->first < maxValue)
    {
        entries.insert( std::pair<unsigned long, unsigned long>(maxValue,0) );
        expMaxValue = entries.rbegin()->first; 
    }

    bDbg && fprintf(stderr, "\nHistogram %s created with minExpected %lu, maxExpected %lu, %ld entries at interval of %d %s\n",
                statName.c_str(), expMinValue, expMaxValue, entries.size(), interval, hUnits.c_str());
    (bDbg>1) && bDbg && fprintf(stderr, "First key = %lu, last key = %lu %s\n", entries.begin()->first, expMaxValue, hUnits.c_str());
} 

void Histogram::update(unsigned long v)
{
    if (v > expMaxValue)
    {
        unsigned long newExpMaxValue = v - v%interval + interval;    //Create a new bin one interval above
        unsigned long downOne = newExpMaxValue - interval;
        unsigned long downHalf = newExpMaxValue - .5 * (newExpMaxValue-expMaxValue);
        
        entries.insert(std::pair<unsigned long, unsigned long>(newExpMaxValue,0));
        entries.insert(std::pair<unsigned long, unsigned long>(downOne, 0));
        entries.insert(std::pair<unsigned long, unsigned long>(downHalf, 0));
        (bDbg>1) && fprintf(stderr, "Histogram::update(%lu) adding new entries at %lu, %lu, %lu\n", v, downHalf, downOne, newExpMaxValue);
        expMaxValue = newExpMaxValue; 
    }
    entries.lower_bound(v)->second++;
}

std::ostream & Histogram::histPrint(std::ostream &os, double scalingFactor, std::string &units) 
{
    fprintf(stderr, "Histogram(minExpected %f, maxExpected %f) contains %ld bins at intervals of %f %s\n",
                (float)(expMinValue*scalingFactor), (float)(expMaxValue*scalingFactor), entries.size(), (float)(interval*scalingFactor), units.c_str());
    HistogramEntries::iterator iter = entries.begin();
    double prevKey = 0;
    for (; iter != entries.end(); ++iter)
    {
        if (iter->second != 0)
            os << iter->second << " >" << prevKey << " and <=" << (iter->first)*scalingFactor << " " << units <<"\n";
        prevKey = iter->first*scalingFactor;
    }
    return os;
}

Stats::Stats(const char *sName, unsigned long hMin, unsigned long hMax, unsigned long hCt, const std::string &statUnits, int numWorkers, int dbg)
    : statName(sName),
      minValue(ULONG_MAX),
      maxValue(0),
      totalValue(0),
      ctr(0),
      units(statUnits),
      scaling(1),
      scaledUnits(" "),
      histogram(hMin,hMax, hCt, statUnits, statName, dbg ),
      maxWorkers(numWorkers),
      samples(numWorkers,0)
{ }


void Stats::update(unsigned long v)
{
    ++ctr;
    totalValue += v;
    histogram.update(v);
    maxValue = std::max(maxValue, v);
    minValue = std::min(minValue, v);
}

void Stats::setStatsPrintScaling(double s, const std::string &u)
{
    scaling=s;
    scaledUnits = u;
}

void Stats::setStartTime(struct timespec &t)
{
    startTime = t;
}

void Stats::setEndTime(struct timespec &t)
{
    endTime = t;
}

unsigned long Stats::getCtr()
{   
    return ctr; 
}

std::ostream & Stats::statsPrint(std::ostream &os) 
{
    long avgValue = -1;
    char buf[1000];

    if (ctr == 0)
    {
        os << statName << " Stats::statsPrint no Data\n\n";
        return os;
    }

    sprintf(buf, "StartTime: %ld.%09ld", startTime.tv_sec, startTime.tv_nsec);
    os << "** " << statName << " Statistics over " << ctr << " Samples:\n";
    os << buf << "\n";

    avgValue = totalValue/ctr;

    if (scaling == 1)
    {
        sprintf(buf, "Min= %lu %s\n", minValue, units.c_str());
        os << buf;
    
        sprintf(buf, "Max= %lu %s\n", maxValue, units.c_str());
        os << buf;
    
        if (ctr > 0)
        {
            sprintf(buf, "Avg= %lu %s\n", avgValue, units.c_str());
            os << buf;
        }
    }
    else
    {
        sprintf(buf, "Min= %lf %s\n", scaling*minValue, scaledUnits.c_str());
        os << buf;
    
        sprintf(buf, "Max= %lf %s\n",scaling*maxValue, scaledUnits.c_str());
        os << buf;
    
        if (ctr != 0)
        {
            sprintf(buf, "Avg= %lf %s\n", scaling*avgValue, scaledUnits.c_str());
            os << buf;
        }
    }
    os << endl;
    for (int i=0; i<maxWorkers; ++i)
    {
        if (samples[i] != 0)
            os << "Data to Worker " << i << ":" << samples[i] << "\n";
    }
    os << endl;

    histogram.histPrint(os, scaling, (scaling==1 ? units : scaledUnits) ) ;
    sprintf(buf, "\nEndTime: %ld.%09ld", endTime.tv_sec, endTime.tv_nsec);
    os << buf << "\n";

    return os;
}

void Stats::newSample(int workerInstance)
{
    if (workerInstance < maxWorkers)
        ++samples[workerInstance];
    else
        fprintf(stderr, "ERROR Stats::newSample - workerInstance %d exceeds maxWorkers %d\n",workerInstance, maxWorkers);
}
    

std::ostream & operator<<(std::ostream &os, Stats &stats)
{
    return stats.statsPrint(os);
}

#ifdef TEST_STATS
int main(int argc, char **argv)
{
    Stats * statsSet[10];
    for (int i=0; i<10; ++i)
    {
        statsSet[i] = new Stats(i, i+2, 3, "FPS");        
    }
    int sz = sizeof(statsSet)/sizeof(Stats);
    cout << "sz=" << sz << endl;

    for (int i=0; i<sz; ++i)
    {
        //statsSet[i].setStatsPrintScaling(1, "");
        cout << "statSet[" << i << "]: " << "\n";
        cout << statsSet[i] << "\n";
    }
    cout << endl;
}
#endif
        
