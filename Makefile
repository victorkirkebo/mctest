# Makefile for mctest
# Version 1.0
# Victor Kirkebo
# 23 Feb 2009

memcached_debug_SOURCES = memcached.c slabs.c items.c assoc.c thread.c stats.c daemon.c

test_SOURCES = 00-startup.c 64bit.c binary-get.c bogus-commands.c\
    cas.c daemonize.c expirations.c flags.c flush-all.c getset.c\
    incrdecr.c lru.c maxconns.c multiversioning.c noreply.c\
    stats-detail.c stats.c udp.c unixsocket.c

TESTS = $(test_SOURCES:.c=)
LIBS_SRC = libmemctest.c libmemc.c
LIBS = $(LIBS_SRC:.c=.o)

#VERBOSE = -v

# gcc
#CC = /usr/bin/gcc
#CFLAGS = -std=gnu99 -DHAVE_PROTOCOL_BINARY
#PROFILER = gcov

# Sun Studio
CC = /opt/studio12/SUNWspro/bin/cc
CFLAGS = -DHAVE_PROTOCOL_BINARY
LDFLAGS = -lsocket -lumem
PROFILER = tcov
UMEMFLAGS = UMEM_DEBUG=default UMEM_LOGGING=transaction;
UMEMEXPORT = export UMEM_DEBUG UMEM_LOGGING;

ENV = $(UMEMFLAGS)
EXPORT = $(UMEMEXPORT)

.c.o:
	$(CC) $(CFLAGS) -c $<

$(TESTS): $(LIBS)
	$(CC) $(CFLAGS) $@.c -o $@ $(LIBS) $(LDFLAGS)

all: $(TESTS)

# -t : run test with textual protocol
# -b : run test with binary protocol
# -v : run test verbose
test: all
	@rm -f error.log
	@rm -f ../*.gcda
	@rm -f ../*.gcov	
	@rm -f *.tcov
	@rm -rf memcached-debug.profile
	@($(ENV) $(EXPORT) \
	for test in $(TESTS); do \
	  (echo --- $$test - textual protocol --- >> ./error.log;) && \
	  (echo $$test - textual protocol; ./$$test -t $(VERBOSE) 2>>./error.log;) && \
	  (echo --- $$test - binary protocol --- >> ./error.log;) && \
	  (echo $$test - binary protocol; ./$$test -b $(VERBOSE) 2>>./error.log;) \
	done)
	@if test `basename $(PROFILER)` = "gcov"; then \
	  cd ..; \
	  sleep 2; \
	  for file in memcached_debug-*.gc??; do \
	    mv -f $$file `echo $$file | sed 's/memcached_debug-//'`; \
	  done && \
	  for file in *.gcda; do \
	    srcfile=`echo $$file | sed 's/.gcda/.c/'`; \
	    if test -n "`echo $(memcached_debug_SOURCES) | grep $$srcfile`"; then \
	      echo `$(PROFILER) $$srcfile` | sed 's/'$$srcfile':.*//'; \
	    fi \
	  done \
	elif test `basename $(PROFILER)` = "tcov"; then \
	  sleep 2; \
	  files=`grep SRCFILE memcached-debug.profile/tcovd | sed 's/SRCFILE://' | sort | uniq`; \
	  $(PROFILER) -x memcached-debug.profile $$files 2>&1; \
	  rm -rf *.h.tcov; \
	  for file in *.tcov; do \
	    srcfile=`echo $$file | sed 's/.tcov//'`; \
	    if test -n "`echo $(memcached_debug_SOURCES) | grep $$srcfile`"; then \
	      echo $$srcfile : `grep 'Percent of the file executed' $$file`; \
	    fi \
	  done \
	else :; fi

clean:
	rm -rf *.o
	rm -rf $(TESTS)
