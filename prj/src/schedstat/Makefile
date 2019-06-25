VERSION = 0.62
CC?=$(CROSS_COMPILE)gcc

OBJDIR = ../../build/

sources = schedstat.c 

TARGETS = $(sources:.c=)
# rem -lpthread
LIBS	= -lrt -lrttest 
RTTESTLIB = -lrttest -L$(OBJDIR) -lnuma -lcap 
EXTRA_LIBS ?= -ldl -lrt	# for get_cpu

#added prthread
CFLAGS ?= -Wall -Wno-nonnull -pthread -O2
CPPFLAGS += -D_GNU_SOURCE -D VERSION=$(VERSION) -I ../include/

schedstat.o: schedstat.c pidlist.c update.c manage.c 
	$(CC) $(CFLAGS) schedstat.c pidlist.c update.c manage.c -o schedstat $(CPPFLAGS) $(RTTESTLIB)

