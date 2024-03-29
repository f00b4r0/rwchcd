VARLIBDIR := /var/lib/rwchcd

REVISION := $(shell git describe --tags --always --dirty)
HOST_OS := $(shell uname)
CC := gcc
LD := ld
FLEX := flex
BISON := bison
#-Wdouble-promotion should be checked but triggers warnings with printf
WFLAGS := -Wall -Wextra -Winline -Wdeclaration-after-statement -Wno-unused-function -Wno-double-promotion -Winit-self -Wswitch-default -Wswitch-enum -Wbad-function-cast -Wcast-qual -Wwrite-strings -Wjump-misses-init -Wlogical-op -Wconversion -Wshadow
OPTIMS := -Og -g -ggdb3 -march=native -mcpu=native -mtune=native -fstrict-aliasing -Wstrict-aliasing
CFLAGS := -I$(CURDIR) -std=gnu11 $(OPTIMS) -DRWCHCD_REV='"$(REVISION)"' -DRWCHCD_STORAGE_PATH='"$(VARLIBDIR)"' $(EXTRA_CFLAGS)
# -lm is only necessary for sqrtf() used in hw_lib.c
LDLIBS := -lm

CONFIG := -DHAS_FILECFG -DDEBUG=2

ifeq ($(HOST_OS),Linux)
 CONFIG += -DHAS_BDB
 CFLAGS += -D_GNU_SOURCE -pthread -DC_HAS_BUILTIN_EXPECT
 ifeq ($(shell pkg-config --exists gio-unix-2.0 && echo 1),1)
  CONFIG += -DHAS_DBUS
 endif
 ifeq ($(shell pkg-config --exists librrd && echo 1),1)
  CONFIG += -DHAS_RRD
 endif
 ifeq ($(shell pkg-config --exists libmosquitto && echo 1),1)
  CONFIG += -DHAS_MQTT
 endif
 # quick hack to enable HWP1
 ifneq (,$(findstring Raspberry,$(shell cat /proc/device-tree/model)))
  CONFIG += -DHAS_HWP1
 endif
endif

ifeq ($(V),)
 QUIET := @
else
 ifeq ($(V),0)
  QUIET := @
 else
  QUIET :=
 endif
endif

CFLAGS += $(CONFIG)

SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

MAIN := rwchcd
MAINOBJS := $(OBJS)

SUBDIRS := plant/ plant/heatsources/ io/ io/inputs/ io/outputs/

HWBACKENDS_DIRS := dummy/

ifneq (,$(findstring HAS_FILECFG,$(CONFIG)))
 SUBDIRS += filecfg/parse/ filecfg/dump/
endif

ifneq (,$(findstring HAS_BDB,$(CONFIG)))
 LDLIBS += -ldb
endif

SUBDIRS += log/
ifneq (,$(findstring HAS_RRD,$(CONFIG)))
 LDLIBS += $(shell pkg-config --libs librrd)
endif

ifneq (,$(findstring HAS_HWP1,$(CONFIG)))
 LDLIBS += -lwiringPi
 HWBACKENDS_DIRS += hw_p1/
endif

ifneq (,$(findstring HAS_MQTT,$(CONFIG)))
 LDLIBS += $(shell pkg-config --libs libmosquitto)
 HWBACKENDS_DIRS += mqtt/
endif

ifneq (,$(findstring HAS_DBUS,$(CONFIG)))
 LDLIBS += $(shell pkg-config --libs gio-unix-2.0)
 SUBDIRS += dbus/
endif

SUBDIRS += hw_backends/ $(patsubst %,hw_backends/%,$(HWBACKENDS_DIRS))

TOPTARGETS := all clean distclean install uninstall doc

SUBDIRBIN := _payload.o
SRCROOT := $(CURDIR)

MAKEFILES := $(CURDIR)/Makefile.sub

export SUBDIRBIN CC LD CFLAGS WFLAGS CONFIG SRCROOT FLEX BISON QUIET

$(TOPTARGETS): $(SUBDIRS)

$(SUBDIRS):
	$(QUIET)$(MAKE) -C $@ $(MAKECMDGOALS)

SUBDIRS_OBJS := $(SUBDIRS:/=/$(SUBDIRBIN))
MAINOBJS += $(SUBDIRS_OBJS)

all:	MAKECMDGOALS ?= all
all:	$(SUBDIRS) $(MAIN)
	@echo	Done

$(MAIN): $(MAINOBJS)
	$(info CC $@)
	$(QUIET)$(CC) -o $@ $^ $(CFLAGS) $(WFLAGS) $(LDLIBS)

.c.o:
	$(info CC $@)
	$(QUIET)$(CC) $(CFLAGS) $(WFLAGS) -MMD -c $< -o $@

clean:
	$(RM) *.o *.d *~ $(MAIN)

distclean:	clean
	$(RM) -r doc
	$(MAKE) -C tools $@

install: $(MAIN)
	install -m 755 -o nobody -g nogroup -d $(VARLIBDIR)/
	install -D -s $(MAIN) -t /usr/sbin/
	@echo Done

uninstall:
	$(RM) /usr/sbin/$(MAIN)
	$(RM) -r $(VARLIBDIR)
	@echo Done

doc:	Doxyfile
	( cat Doxyfile; echo "PROJECT_NUMBER=$(REVISION)" ) | doxygen -
	
# rebuild rwchcd.o if anything changes to update version
rwchcd.o:       $(filter-out rwchcd.o,$(MAINOBJS))

-include $(DEPS)
.PHONY:	$(TOPTARGETS) $(SUBDIRS)
