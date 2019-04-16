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


