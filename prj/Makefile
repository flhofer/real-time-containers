VERSION = 1.0
CC?=$(CROSS_COMPILE)gcc
AR?=$(CROSS_COMPILE)ar

OBJDIR = build

sources = schedstat.c 

TARGETS = $(sources:.c=)
# rem -lpthread
LIBS	= -lrt -lcap -lrttest
RTTESTLIB = -lrttest -L$(OBJDIR)
EXTRA_LIBS ?= -ldl	# for get_cpu
DESTDIR	?=
prefix  ?= /usr/local
bindir  ?= $(prefix)/bin
mandir	?= $(prefix)/share/man
srcdir	?= $(prefix)/src

#added prthread
CFLAGS ?= -Wall -Wno-nonnull -pthread
CPPFLAGS += -D_GNU_SOURCE -Isrc/include
LDFLAGS ?= -pthread 

# If debug is defined, disable optimization level
ifndef DEBUG
	CFLAGS	+= -O2
else
	CFLAGS	+= -O0 -g
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
	$(CC) -D VERSION=$(VERSION) -c $< $(CFLAGS) $(CPPFLAGS) -o $@

# Pattern rule to generate dependency files from .c files
$(OBJDIR)/%.d: %.c | $(OBJDIR)
	@$(CC) -MM $(CFLAGS) $(CPPFLAGS) $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@ || rm -f $@

.PHONY: all
all: $(TARGETS) 
#hwlatdetect | $(OBJDIR)

$(OBJDIR):
	mkdir $(OBJDIR)

# Include dependency files, automatically generate them if needed.
-include $(addprefix $(OBJDIR)/,$(sources:.c=.d))

schedstat: $(addprefix $(OBJDIR)/,schedstat.o pidlist.o manage.o update.o jsmn.o librttest.a)
	$(CC) $(CFLAGS) $(LDFLAGS) $(addprefix $(OBJDIR)/, pidlist.o manage.o update.o jsmn.o) -o $@ $< $(LIBS) $(RTTESTLIB) $(NUMA_LIBS)

# Old test make
#testposix: $(OBJDIR)/thread.o $(OBJDIR)/librttest.a
#	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS) $(RTTESTLIB) $(NUMA_LIBS)

# lib containing include lib in one binary file
LIBOBJS =$(addprefix $(OBJDIR)/,error.o rt-get_cpu.o rt-sched.o rt-utils.o kernutil.o)
$(OBJDIR)/librttest.a: $(LIBOBJS)
	$(AR) rcs $@ $^

CLEANUP  = $(TARGETS) *.o .depend *.*~ *.orig *.rej *.d *.a
CLEANUP += $(if $(wildcard .git), ChangeLog)

.PHONY: clean
clean:
	for F in $(CLEANUP); do find -type f -name $$F | xargs rm -f; done
	rm -f rt-tests-*.tar
	rm -f hwlatdetect
	rm -f tags

RPMDIRS = BUILD BUILDROOT RPMS SRPMS SPECS
.PHONY: distclean
distclean: clean
	rm -rf $(RPMDIRS) releases *.tar.gz *.tar.asc tmp

.PHONY: rebuild
rebuild:
	$(MAKE) clean
	$(MAKE) all

.PHONY: install
install: all 
	mkdir -p "$(DESTDIR)$(bindir)" "$(DESTDIR)$(mandir)/man4"
	mkdir -p "$(DESTDIR)$(srcdir)" "$(DESTDIR)$(mandir)/man8"
	cp $(TARGETS) "$(DESTDIR)$(bindir)"
	gzip -c src/schedstat/schedstat.8 >"$(DESTDIR)$(mandir)/man8/schedstat.8.gz"


.PHONY: tarball
tarball:
	git archive --worktree-attributes --prefix=rt-tests-${VERSION}/ -o rt-tests-${VERSION}.tar v${VERSION}

.PHONY: help
help:
	@echo ""
	@echo " rt-tests useful Makefile targets:"
	@echo ""
	@echo "    all       :  build all tests (default)"
	@echo "    install   :  install tests to local filesystem"
	@echo "    clean     :  remove object files"
	@echo "    distclean :  remove all generated files"
	@echo "    tarball   :  make a rt-tests tarball suitable for release"
	@echo "    help      :  print this message"

.PHONY: tags
tags:
	ctags -R --extra=+f --c-kinds=+p --exclude=tmp --exclude=BUILD *
