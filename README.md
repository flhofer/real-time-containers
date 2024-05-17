![C/C++ CI](https://github.com/flhofer/real-time-containers/workflows/C/C++%20CI/badge.svg)
![Coverage](test/coverage.svg)

# real-time-containers #

This repository contains all code and documentation regarding the Dynamic orchestration project started in collaboration with Siemens Corporate Technologies, USA, and the University of California at Berkeley. All changes done since Feb 2024 are related to its continuation with the Università degli studi di Bologna and include optimizations such as the `polenaRT` automatic environment configuation for Moby compatible systems.

# File structure #

real-time containers research repository, general tree structure. Folders marked as `(archived)` will no longer be updated.

	.
	├── docs		# generic documentation of investigation and architecture details
	|   ├── bib 		# bibliography texts, papers, books, sub-folders used in case of not connected ref's (archived)
	|   ├── diagrams	# folder with architecture diagrams for the `orchestrator` use case (archived)
	|   ├── (folder)	# folder with publications concerning a specific paper (archived)
	|   ├── *.bib	 	# other bib-files    	
	|   ├── related_works   # found work, presentations, master theses and alike that might be used throughout the project (archived)
	|   ├── reportShortVisit# report of the 2018 short stay, experiments and results (archived)
	|   └── slides 		# progress documenting slides; for weekly meetings and calls -> incremental (archived)
	├── src		 	# source files of the different project components
	|   ├── check_dockerRT	# source code for the Icinga2 plugin (TBD)
	|   ├── include 	# internal library header files
	|   ├── lib 		# internal library source files
	|   └── orchestrator	# orchestrator source code
	├── tools		# development tools such as containers for build or run
	├── test 		# `check` based test scripts for project sources, reflects src structure
	├── test-monitor 	# latency and orchestration test results and scripts
	|   ├── latBplot 	# latency experiments BoxPlot scripts and results printed (archived)
	|   ├── latency		# scripts and results for `cyclictest` latency tests on bare metal and cloud instances (archived)
	|   ├── polena 		# files for Xenomai3 kernel build (archived)
	|   ├── polenaRT	# files for PREEMPT-RT kernel build and `Moby` compatible RT-environment configuration
	|   ├── statsched 	# R scripts for results analysis for `orchestrator` static scheduling tests (archived)
	|   ├── test-container	# scripts and configurations for the kernel boot parameter test based on `rt-app` containers
	|   └── usecase		# folders with use cases applications and simulations 
	├── Makefile		# Makefile for orchestrator, tests via check and use cases 1-2
	└── README.md	 	# this file
	

# General instructions #

The `makefile` contains all necessary build instructions. With `make all` we build the orchestrator and the use case binaries found in `test-monitor`, as well as the test cases. `make check` instead runs the test cases only. See `make help` for more info.

Further documentation is located in the docs folder. Specifically, the _README_ file and the _orchestrator.8_ manual give further insight on configuration and usage. For help on `polenaRT` and its configuration, see _README_ and folder description in `test-monitor->polenaRT`.

Please notify me for any found issue. Thank you.
