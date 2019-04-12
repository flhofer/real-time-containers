# real-time-containers #



# File structure #

real-time containers research repository, general tree structure:

* bib bibliography texts, papers, books, subfolders used in case of not connected ref's
  * paper* folder with publications concerning a specific paper
  * _paper*.bib_ paper or conference bibliography file
  * _bibliography.bib_ general bibliography file
  * _*.bib_ other bib-files

* prj folder containing the main research project - the container orchestrator
  * docs project docutmentation and instructions
  * lib internal library source files
  * include internal library header files
  * build build output for project make
  * src source files of the different project components
  * test `check` based test scripts for project sources
  * test_container scripts and configurations for the docker test container based on `rt-app`

* polena files for Xenomai3 kernel build
* polenaRT files for PREEMPT-RT kernel build
* test-monitor latency and orchestration test results and scripts


* _README.md_ this file

(TO BE CONTINUED)
