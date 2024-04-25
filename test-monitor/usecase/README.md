# Use cases 

This part of the repository contains use cases for the `orchestrator` and the `polena` real-time environment. I've included a short description of the use cases below.

## Use cases 1 and 2 - Siemens Corporate Technology, USA

These first two use cases were created with Siemens to simulate typical application scenarios for real-time containers in industrial environments.


<img src="resources/uc0-graph.png" width="700">


### Use case 1 - product quality control

In this first example, we consider image processing cameras on a production line, which feed the container infrastructure with an image flow. The images are then processed to look for product defects.

<img src="resources/uc1-belt.png" width="500">

In the case of two 8FPS cameras on one resource, this would produce a task scheduling schema like the following. Note that the processing time can not be considered constant as it depends on the complexity of the image and the number of detectable objects. However, this also means that a scheduled resource can be oversubscribed.

<img src="resources/uc1-schedule.png" width="700">

As the first guarantee, we need the timely execution and delivery of results. With 8FPS, we require that the computation take no longer than 125ms. If we share computation resources with another camera, this time half. You can see the trend here.

Ideally, `polenaRT` will create an environment with minimal jitter and delays. This is a test case where we can verify this.

<img src="resources/uc1-graph.png" width="700">

The use case will simulate this scenario with three components: the generator, i.e., the camera, which produces our images at a settable speed; the distributor, which is in charge of load-balancing incoming frames; and the workers. The test will use 8FPS generation per running worker and run from 3 to 8 workers. Ideally, the logs will confirm the reactivity and determinism of our environment.

![Task configuration 1](resources/uc1-tasks.png)*Task dependency graph*

### Use case 2 - IoT and telemetry processing server
 
The second use case describes a server that processes incoming data from sensors—a typical scenario for IoT. These sensors can be of two types: polling, i.e., the server asks the sensor regularly about new readings, and event-based. The latter can generate data inconsistently in size and time, making scheduling and prediction hard.

<img src="resources/uc2-graph.png" width="700">

For this use case, we will use combined generator/distributor instances that either regularly poll the worker tasks (deadline-driven) or use event-driven workers that generate random traffic.

![Task configuration 2](resources/uc2-tasks.png)*Task dependency graph*

## Use case 3 - Codesys Control

In this scenario we consider the run-time software for IEC61131-3 compliant Soft-PLCs in bare or virtualized settings

*** WIP ***

## Use case 4 - Simatic vPLC

The fourth application considers the real-time environment around Siemens's vPLC package.

*** WIP ***


