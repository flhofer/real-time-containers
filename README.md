#TODO: MERGE

# real-time-containers #

This repositor contains all code and documentation regarding rhe Dynamic orchestration projcet.
The main project files are in the prj folder and should all be contained.
All other folders contain project description or detail information. Reder to docs for architectural details.

# File structure #

real-time containers research repository, general tree structure:

* bib bibliography texts, papers, books, subfolders used in case of not connected ref's
  * (folder) folder with publications concerning a specific paper
  * _paper*.bib_ paper or conference bibliography file
  * _bibliography.bib_ general bibliography file
  * _*.bib_ other bib-files

* prj folder containing the main research project - the container orchestrator
  * build build output for project make
  * docs project docutmentation and instructions
  * src source files of the different project components
    * check_dockerRT source code for the Icinga2 plugin
    * include internal library header files
    * lib internal library source files
	* schedstat static orchestrator source code
    * testPosix test program to test OS features, see docs
    * tools eventual development tools
  * test `check` based test scripts for project sources
  * test_container scripts and configurations for the docker test container based on `rt-app`

* docs generic documentation of investigation and architecture details
* polena files for Xenomai3 kernel build
* polenaRT files for PREEMPT-RT kernel build
* related_works found work, presentations, master theses and alike that might be used throughout the project
* reportShortVisit report of the 2018 short stay, experiments and results
* slides progress documenting slides; for weekly meetings and calls -> incremental
* test-monitor latency and orchestration test results and scripts

* _README.md_ this file

# Real-time container dynamic orchestration #

This is the main development repository containing code, tests and short documentation.
Refer to the parent repository for more detailed descriptions and performance tests.

NOTE: schedstat does not compile with the global makefile yet. Use the makefile in the folder.
Further notes can be found in the README in docs.

# File structure #

* build build output for project make
* docs project docutmentation and instructions
* src source files of the different project components
  * check_dockerRT source code for the Icinga2 plugin
  * include internal library header files
  * lib internal library source files
  * schedstat static orchestrator source code
  * testPosix test program to test OS features, see docs
* tools eventual development tools
* test `check` based test scripts for project sources
* test_container scripts and configurations for the docker test container based on `rt-app`


