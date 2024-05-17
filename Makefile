VERSION = 0.91
VERSUFF = 
GIT_VERSION := $(shell git describe --abbrev=7 --always --tags)
CC?=$(CROSS_COMPILE)gcc
AR?=$(CROSS_COMPILE)ar
CPP?=$(CROSS_COMPILE)g++
#uncomment the line below to create debug versions by default
#DEBUG=1
#COVERAGE=1
#uncomment the line below to exclude kbuffer source from linking and use libtraceevent instead
#USELIBTRACE=1

OBJDIR = build

sources = orchestrator.c test.c
orcbins = update.o manage.o prepare.o adaptive.o resmgnt.o
testbins = orchestrator_suite.o library_suite.o resmgntTest.o \
		   adaptiveTest.o manageTest.o updateTest.o dockerlinkTest.o\
		   kernutilTest.o orchdataTest.o parse_configTest.o errorTest.o

TARGETS = $(sources:.c=)	# sources without .c ending
LIBS	= -lrt -lcap -lrttest -ljson-c -lm -lgsl -lgslcblas
ifdef USELIBTRACE
	LIBS += -ltraceevent
endif
# for tests
TLIBS	= -lcheck -lm -lsubunit $(LIBS)

# for installation and rpm build
DESTDIR	?=
prefix  ?= /usr/local
bindir  ?= $(prefix)/bin
mandir	?= $(prefix)/share/man
srcdir	?= $(prefix)/src

CFLAGS ?= -Wall -Wno-nonnull -D _GNU_SOURCE
LDFLAGS ?= -lrttest -L $(OBJDIR) -pthread

# If we are running a multibinary, prob busybox, enable the option for compiling change
BBOX=$(shell [ -h /bin/sh ] && [ -h /bin/ls ] && [ -e /bin/busybox ] && echo 1 || echo 0 )
ifeq (1, $(BBOX))
	CFLAGS	+= -D BUSYBOX
endif
# should we compile for CGROUP v2? check if controller settings exist
CG2=$(shell [ -e /sys/fs/cgroup/cgroup.controllers ] && echo 1 || echo 0 )
ifeq (1, $(CG2))
	CFLAGS	+= -D CGROUP2
endif
# is priviledged testing available? # TODO: temp test to update
PRIV=$(shell [ -e /sys/kernel/debug/tracing ] && echo 1 || echo 0 )
ifeq (1, $(PRIV))
	CFLAGS += -D PRVTEST
endif

# If debug is defined, disable optimization level
ifndef DEBUG
	CFLAGS	+= -O2 -D VERSION=\"$(VERSION)\"
else
	CFLAGS	+= -O0 -g -D DEBUG -D VERSION=\"$(VERSION)$(VERSUFF)\ $(GIT_VERSION)\"
	ifdef COVERAGE
		CFLAGS += -coverage
		DIRDEPTH=$(shell var=${PWD//[!\/]}; echo ${#var} )
	endif
endif

CPPFLAGS ?= $(CFLAGS)
CFLAGS += -I src/include

# We make some gueses on how to compile based on the machine type
# and the ostype. These can often be overridden.
dumpmachine := $(shell $(CC) -dumpmachine)

# The ostype is typically something like linux or android
ostype := $(lastword $(subst -, ,$(dumpmachine)))

machinetype := $(shell echo $(dumpmachine)| \
    sed -e 's/-.*//' -e 's/i.86/i386/' -e 's/mips.*/mips/' -e 's/ppc.*/powerpc/')

# The default is to assume you have libnuma installed, which is fine to do
# even on non-numa machines. If you don't want to install the numa libs, for
# example, they might not be available in an embedded environment, then
# compile with
# make NUMA=0
ifneq ($(filter x86_64 i386 ia64 mips powerpc,$(machinetype)),)
	NUMA := 1
endif

# The default is to assume that you have numa_parse_cpustring_all
# If you have an older version of libnuma that only has numa_parse_cpustring
# then compile with
# make HAVE_PARSE_CPUSTRING_ALL=0
HAVE_PARSE_CPUSTRING_ALL?=1
ifeq ($(NUMA),1)
	CFLAGS += -DNUMA
	NUMA_LIBS = -lnuma
	ifeq ($(HAVE_PARSE_CPUSTRING_ALL),1)
		CFLAGS += -DHAVE_PARSE_CPUSTRING_ALL
	endif
endif

# define search paths
VPATH	= src/orchestrator:
VPATH	+= src/lib:
VPATH	+= test/lib:
VPATH	+= test/orchestrator:
VPATH	+= test-monitor/usecase/uc1-2/src:

.PHONY: all
all: $(TARGETS) usecases check | $(OBJDIR)

# include use case builds
include test-monitor/usecase/uc1-2/src/Makefile

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) -c $< $(CFLAGS) -o $@

# Pattern rule to generate dependency files from .c files
$(OBJDIR)/%.d: %.c | $(OBJDIR)
	@$(CC) -MM $(CFLAGS) $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@ || rm -f $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Include dependency files, automatically generate them if needed.
-include $(addprefix $(OBJDIR)/,$(sources:.c=.d))

orchestrator: $(addprefix $(OBJDIR)/,orchestrator.o $(orcbins) librttest.a)
	$(CC) $(CFLAGS) $(LDFLAGS) $(addprefix $(OBJDIR)/, $(orcbins)) -o $@ $< $(LIBS) $(NUMA_LIBS)

# Old test make
#testposix: $(OBJDIR)/thread.o $(OBJDIR)/librttest.a
#	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS) $(NUMA_LIBS)

# TODO: create missing test cases to allow removal of librttest.a
# TODO: add checks for dependent sources change! (does not work :/)
test: test/test.c $(wildcard src/orchestrator/*.c) $(addprefix $(OBJDIR)/,$(testbins) librttest.a)
	$(CC) $(CFLAGS) $(LDFLAGS) $(addprefix $(OBJDIR)/,$(testbins)) -o check_$@ $< $(TLIBS) $(NUMA_LIBS)
	
check:
	./check_test
	@if [ -n "$(COVERAGE)" ]; then \
		./test/coverage.sh ; \
	fi 

# lib containing include lib in one binary file
LIBOBJS =$(addprefix $(OBJDIR)/,error.o rt-sched.o kernutil.o orchdata.o parse_config.o dockerlink.o kbuffer.o runstats.o)
$(OBJDIR)/librttest.a: $(LIBOBJS)
	$(AR) rcs $@ $^

CLEANUP += $(TARGETS) check_test *.o .depend *.*~ *.orig *.rej *.d *.a *.gcno *.gcda *.gcov
CLEANUP += $(if $(wildcard .git), ChangeLog)

.PHONY: clean
clean:
	for F in $(CLEANUP); do find -type f -name "$$F" | xargs rm -f; done

.PHONY: rebuild
rebuild:
	$(MAKE) clean
	$(MAKE) all

#
#.PHONY: install
#install: all 
#	mkdir -p "$(DESTDIR)$(bindir)" "$(DESTDIR)$(mandir)/man4"
#	mkdir -p "$(DESTDIR)$(srcdir)" "$(DESTDIR)$(mandir)/man8"
#	cp $(TARGETS) "$(DESTDIR)$(bindir)"
#	gzip -c src/orchestrator/orchestrator.8 >"$(DESTDIR)$(mandir)/man8/orchestrator.8.gz"

.PHONY: tarball
tarball:
	git archive --worktree-attributes --prefix=orch-${VERSION}/ -o orch-${VERSION}.tar v${VERSION}

.PHONY: help
help:
	@echo " DC orchestrator useful Makefile targets:"
	@echo ""
	@echo "    all       :  build all tests (default)"
	@echo "    test      :  build unit tests"
	@echo "    check     :  run unit tests"
	@echo "    clean     :  remove object files"
	@echo "    tarball   :  make a tarball suitable for release"
	@echo "    help      :  print this message"

.PHONY: tags
tags:
	ctags -R --extra=+f --c-kinds=+p --exclude=tmp --exclude=BUILD *

