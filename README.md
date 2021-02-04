![C/C++ CI](https://github.com/flhofer/real-time-containers/workflows/C/C++%20CI/badge.svg)

# real-time-containers #

This repository contains all code and documentation regarding the Dynamic orchestration project started in collaboration with Siemens Corporate Technologies, USA, and the University of California at Berkeley.

# File structure #

real-time containers research repository, general tree structure:

	.
	├── build		# build output for project make
	├── docs		# generic documentation of investigation and architecture details
	|   ├── bib 		# bibliography texts, papers, books, sub-folders used in case of not connected ref's
	|   ├── (folder)	# folder with publications concerning a specific paper
	|   ├── *.bib	 	# other bib-files    	
	|   ├── related_works   # found work, presentations, master theses and alike that might be used throughout the project
	|   ├── reportShortVisit# report of the 2018 short stay, experiments and results
	|   └── slides 		# progress documenting slides; for weekly meetings and calls -> incremental
	├── src 		# source files of the different project components
	|   ├── check_dockerRT	# source code for the Icinga2 plugin
	|   ├── include 	# internal library header files
	|   ├── lib 		# internal library source files
	|   ├── orchestrator	# orchestrator source code
	|   └── testPosix 	# test program to test OS features, see docs
	├── tools		# development tools such as containers for build or run
	|   ├── test 		# `check` based test scripts for project sources, reflects src structure
	|   ├── test-monitor 	# latency and orchestration test results and scripts
	|   ├── polena 		# files for Xenomai3 kernel build
	|   ├── polenaRT	# files for PREEMPT-RT kernel build
	|   └── test_container	# scripts and configurations for the docker test container based on `rt-app`
	├── Makefile		# development tools such as containers for build or run
	└── README.md	 	# this file
	

# General instructions #

The `makefile` contains all necessary build instructions. With `make all` we build the orchestrator and the use case binaries found in `test-monitor`. `make test` instead creates and runs the test cases.

Please notify me for any found issue. Thank you.
