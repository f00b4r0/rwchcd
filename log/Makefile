SRCS := log.c log_file.c log_statsd.c

ifneq (,$(findstring HAS_RRD,$(CONFIG)))
SRCS += log_rrd.c
endif

include $(SRCROOT)/Makefile.sub
