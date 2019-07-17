CC?=$(CROSS_COMPILE)gcc
AR?=$(CROSS_COMPILE)ar

OBJDIR = ../build

sources = test.c 

TARGETS = $(sources:.c=)

LIBS	= -lcap -lrttest -ljson-c -lcheck_pic -lm -lsubunit -lrt
RTTESTLIB = -lrttest -L$(OBJDIR)
EXTRA_LIBS ?= -ldl -lrt	# for get_cpu
DESTDIR	?=
prefix  ?= /usr/local
bindir  ?= $(prefix)/bin
mandir	?= $(prefix)/share/man
srcdir	?= $(prefix)/src

CFLAGS ?= -g -O0 -Wall -Wno-nonnull -coverage
CPPFLAGS += -D_GNU_SOURCE -I ../src/include
LDFLAGS ?= -pthread 

# We make some gueses on how to compile rt-tests based on the machine type
# and the ostype. These can often be overridden.
dumpmachine := $(shell $(CC) -dumpmachine)

# The ostype is typically something like linux or android
ostype := $(lastword $(subst -, ,$(dumpmachine)))

machinetype := $(shell echo $(dumpmachine)| \
    sed -e 's/-.*//' -e 's/i.86/i386/' -e 's/mips.*/mips/' -e 's/ppc.*/powerpc/')

# make NUMA=0
ifneq ($(filter x86_64 i386 ia64 mips powerpc,$(machinetype)),)
NUMA := 1
endif

# make HAVE_PARSE_CPUSTRING_ALL=0
HAVE_PARSE_CPUSTRING_ALL?=1
ifeq ($(NUMA),1)
	CFLAGS += -DNUMA
	NUMA_LIBS = -lnuma
	ifeq ($(HAVE_PARSE_CPUSTRING_ALL),1)
		CFLAGS += -DHAVE_PARSE_CPUSTRING_ALL
	endif
endif

#gcc -g -Wall -coverage test.c -o test -I ../src/include/ -lrttest -L ../build/ -pthread -lnuma -lcheck_pic -pthread -lrt -lm -lsubunit
test: test.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS) $(RTTESTLIB) $(NUMA_LIBS)

.PHONY: all
all: $(TARGETS) 

.PHONY: clean
clean:
	rm -f test
	rm -f *.gc*

