VERSION = 0.68
VERSUFF = "-beta"
GIT_VERSION := "$(shell git describe --abbrev=7 --always --tags)"
CC?=$(CROSS_COMPILE)gcc
AR?=$(CROSS_COMPILE)ar
#uncomment the line below to create debug versions by default
#DEBUG=1
#COVERAGE=1

OBJDIR = build

sources = schedstat.c

TARGETS = $(sources:.c=) check
LIBS	= -lrt -lcap -lrttest -ljson-c
# for tests
TLIBS	= -lcheck -lm -lsubunit $(LIBS)

# for installation and rpm build
DESTDIR	?=
prefix  ?= /usr/local
bindir  ?= $(prefix)/bin
mandir	?= $(prefix)/share/man
srcdir	?= $(prefix)/src

CFLAGS ?= -Wall -Wno-nonnull 
CPPFLAGS += -D _GNU_SOURCE -I src/include
LDFLAGS ?= -lrttest -L $(OBJDIR) -pthread 

# If debug is defined, disable optimization level
ifndef DEBUG
	CFLAGS	+= -O2
	CPPFLAGS += -D VERSION=\"$(VERSION)\"
else
	CFLAGS	+= -O0 -g
	CPPFLAGS += -D DEBUG -D VERSION=\"$(VERSION)$(VERSUFF)\ $(GIT_VERSION)\"

	ifdef COVERAGE
		CFLAGS += -coverage
	endif
endif

# We make some gueses on how to compile rt-tests based on the machine type
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

#include src/arch/android/Makefile

VPATH	= src/schedstat:
VPATH	+= src/lib:

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) -c $< $(CFLAGS) $(CPPFLAGS) $(EXTRA_LIBS) -o $@

# Pattern rule to generate dependency files from .c files
$(OBJDIR)/%.d: %.c | $(OBJDIR)
	@$(CC) -MM $(CFLAGS) $(CPPFLAGS) $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@ || rm -f $@

.PHONY: all
all: $(TARGETS) 
#hwlatdetect | $(OBJDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Include dependency files, automatically generate them if needed.
-include $(addprefix $(OBJDIR)/,$(sources:.c=.d))

schedstat: $(addprefix $(OBJDIR)/,schedstat.o manage.o update.o librttest.a)
	$(CC) $(CFLAGS) $(LDFLAGS) $(addprefix $(OBJDIR)/, manage.o update.o) -o $@ $< $(LIBS) $(NUMA_LIBS)

# Old test make
#testposix: $(OBJDIR)/thread.o $(OBJDIR)/librttest.a
#	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS) $(NUMA_LIBS)

check: test/test.c $(addprefix $(OBJDIR)/,schedstat.o manage.o update.o librttest.a)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(addprefix $(OBJDIR)/, manage.o update.o) -o $@_test $< $(TLIBS) $(NUMA_LIBS)
	./check_test

# lib containing include lib in one binary file
LIBOBJS =$(addprefix $(OBJDIR)/,error.o rt-get_cpu.o rt-sched.o rt-utils.o kernutil.o orchdata.o parse_config.o dockerlink.o)
$(OBJDIR)/librttest.a: $(LIBOBJS)
	$(AR) rcs $@ $^

CLEANUP  = $(TARGETS) check_test *.o .depend *.*~ *.orig *.rej *.d *.a *.gcno *.gcda *.gcov
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
#	gzip -c src/schedstat/schedstat.8 >"$(DESTDIR)$(mandir)/man8/schedstat.8.gz"

.PHONY: tarball
tarball:
	git archive --worktree-attributes --prefix=orch-${VERSION}/ -o orch-${VERSION}.tar v${VERSION}

.PHONY: help
help:
	@echo " DC orchestrator useful Makefile targets:"
	@echo ""
	@echo "    all       :  build all tests (default)"
	@echo "    install   :  install tests to local filesystem"
	@echo "    check     :  build and run unit tests
	@echo "    clean     :  remove object files"
	@echo "    tarball   :  make a tarball suitable for release"
	@echo "    help      :  print this message"

.PHONY: tags
tags:
	ctags -R --extra=+f --c-kinds=+p --exclude=tmp --exclude=BUILD *

