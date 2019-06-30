VERSION = 0.62
GIT_VERSION := "$(shell git describe --abbrev=7 --always --tags)"
CC?=$(CROSS_COMPILE)gcc
DEBUG=1

OBJDIR = ../../build/

sources = schedstat.c 

TARGETS = $(sources:.c=)
# rem -lpthread
LIBS	= -lrt -lrttest 
RTTESTLIB = -lrttest -L$(OBJDIR) -lnuma -lcap 
EXTRA_LIBS ?= -ldl -lrt	# for get_cpu

#added prthread
CFLAGS ?= -Wall -Wno-nonnull -pthread -O2
CPPFLAGS += -D_GNU_SOURCE -I ../include/

# If debug is defined, disable optimization level
ifndef DEBUG
	CFLAGS	+= -O2
	CPPFLAGS += -D VERSION=\"$(VERSION)\"
else
	CFLAGS	+= -O0 -g
	CPPFLAGS += -D DEBUG -D VERSION=\"$(VERSION)\ $(GIT_VERSION)\"
endif

schedstat.o: schedstat.c pidlist.c update.c manage.c 
	$(CC) $(CFLAGS) schedstat.c pidlist.c update.c manage.c -o schedstat $(CPPFLAGS) $(RTTESTLIB)

