# Use Cases #

To test the orchestrator two use cases has been developed: **Use-Case-1** and **Use-Case-2**

These use cases test orchestrator features like:
- Capability of working with different scheduling policies: SCHED_FIFO, SCHED_DEADLINE and SCHED_RR
- Capability of actively  monitoring for newly started processes

and attempt to benchmark the target system in term of:
- Scalability
- Overall Real-Time Quality
- Limitations

All containers/processes are started with policy SCHED_FIFO and priority set to 1 and are then left to orchestrator for final configuration as specified in the config.json file.

## How to Build 

For building both UseCase-1 and UseCase-2 navigate to this folder and execute:

```
sudo ./buildDockerImages.sh 
```

This script will build the binaries as well as create docker images for DataGenerator, DataDistributor and WorkerApp.

## Use-Case-1

### Description
Use-Case-1 simulates a distributed image processing scenario, in which several processes are spawned and allocated across multiple CPU cores (ideally).

A **Data Generator** simulates the generation of the "frames" to be processed. Data is generated at regular intervals, interleaved by sleeps, that are defined by the current FPS rate. The initial FPS rate is 24 and is then increased throughout the test by multiples of 8. For the purpose of benchmarking real-time quality, the data is not an actual image frame but a timestamp (**Tg**), taken at the time the data was generated. 

Generated data is then passed through a named pipe to a **Data Distributor**. The Data Distributor receives the data, takes a timestamp and distributes it to a set of workers following a round-robin algorithm.

A **Worker**, lastly, reads the incoming data from the pipe, takes a timestamp and executes some workload that keeps the CPU busy for a specified amount of time.


### Hot to Run
To run UseCase-1 execute the script 
```
sudo ./runDockerUC1.sh
```
The test runs for **3 hours** following a dynamic schedule (the containers can dynamically transition to next stage) shown in the table

| Time                  | Workers   | FPS   |
| -------------         |:-------------:| -----:|
| 0-30 Minutes          | 3             | 24    |
| 30-60 Minutes         | 4             | 32    |
| 60-90 Minutes         | 5             | 40    |
| 90-120 Minutes        | 6             | 48    |
| 120-150 Minutes       | 7             | 56    |
| 150-180 Minutes       | 8             | 64    |

*Data Generator* is scheduled with **SCHED_FIFO 98**, *Data Distributor* with **SCHED_FIFO 97** and *Workers* with **SCHED_FIFO 99**

At test completion, full results are available under **/opt/usecase/logs**

## Use-Case-2
### Description
Use-Case-2 simulates a scenario in which worker containers periodically pull sensors data. 

A **Data Generator** continuously  generates data (simulated sensor data) and push it on a pipe. **Worker** apps read from the pipe and simulate a busy loop. Workers are scheduled using a DEADLINE policy mechanism. Meaning that differently from Use-Case-1, where an event driven scheduling occurs, here we expect perfect periodicity.

### Hot to Run
To run UseCase-2 execute the script 
```
sudo ./runDockerUC2.sh
```
The test runs for **3 hours** following a static schedule (the containers are killed and restarted with new parameters) shown in the table:

| Time                   | Workers   |
| -------------          | -------------:|
| 0-30 Minutes           | 3    |
| 30-60 Minutes          | 4    |
| 60-90 Minutes          | 5    |
| 90-120 Minutes         | 6    |
| 120-150 Minutes        | 7    |
| 150-180 Minutes        | 8    |

*Data Generator* is scheduled with **SCHED_FIFO 99** while *Workers* are scheduled using **SCHED_DEADLINE** policy.

At test completion, full results are available under **/opt/usecase/logs**
