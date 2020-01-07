# real-time-containers #

This repository contains all code and documentation regarding the Dynamic orchestration project.

# File structure #

real-time containers research repository, general tree structure:


- build build output for project make
- docs generic documentation of investigation and architecture details
	- bib bibliography texts, papers, books, sub-folders used in case of not connected ref's
    	* (folder) folder with publications concerning a specific paper
    	* _*.bib_ other bib-files    	
   	* related_works found work, presentations, master theses and alike that might be used throughout the project
	* reportShortVisit report of the 2018 short stay, experiments and results
	* slides progress documenting slides; for weekly meetings and calls -> incremental

* src source files of the different project components
    * check_dockerRT source code for the Icinga2 plugin
    * include internal library header files
    * lib internal library source files
	* orchestrator orchestrator source code
    * testPosix test program to test OS features, see docs

* tools development tools such as containers for build or run
* test `check` based test scripts for project sources

* test-monitor latency and orchestration test results and scripts
	* polena files for Xenomai3 kernel build
	* polenaRT files for PREEMPT-RT kernel build
	* test_container scripts and configurations for the docker test container based on `rt-app`

* _README.md_ this file
