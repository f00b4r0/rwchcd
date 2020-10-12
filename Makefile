VARLIBDIR := /var/lib/rwchcd

REVISION := $(shell git describe --tags --always --dirty)
HOST_OS := $(shell uname)
CC := gcc
LD := ld
FLEX := flex
BISON := bison
#-Wdouble-promotion should be checked but triggers warnings with printf
WFLAGS := -Wall -Wextra -Winline -Wdeclaration-after-statement -Wno-unused-function -Wno-double-promotion -Winit-self -Wswitch-default -Wswitch-enum -Wbad-function-cast -Wcast-qual -Wwrite-strings -Wjump-misses-init -Wlogical-op -Wvla -Wconversion
OPTIMS := -Og -g -ggdb3 -march=native -mcpu=native -mtune=native -fstack-protector -Wstack-protector -fstrict-aliasing -Wstrict-aliasing
CFLAGS := -I$(CURDIR) -std=gnu11 $(OPTIMS) -DRWCHCD_REV='"$(REVISION)"' -DRWCHCD_STORAGE_PATH='"$(VARLIBDIR)"'
LDLIBS := -lm

ifeq ($(HOST_OS),Linux)
CONFIG := -DHAS_DBUS -DHAS_HWP1 -DHAS_RRD -DDEBUG=2
CFLAGS += -D_GNU_SOURCE -pthread -DC_HAS_BUILTIN_EXPECT
 ifeq ($(shell pkg-config --exists libmosquitto && echo 1),1)
 CONFIG += -DHAS_MQTT
 endif
else
CONFIG :=
endif

CFLAGS += $(CONFIG)

SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

MAIN := rwchcd
MAINOBJS := $(OBJS)

HWBACKENDS_DIR := hw_backends

SUBDIRS := plant/ io/ io/inputs/ io/outputs/ filecfg/parse/ filecfg/dump/
SUBDIRS += $(HWBACKENDS_DIR)/ $(HWBACKENDS_DIR)/dummy/

SUBDIRS += log/
ifneq (,$(findstring HAS_RRD,$(CONFIG)))
LDLIBS += -lrrd
endif

ifneq (,$(findstring HAS_HWP1,$(CONFIG)))
LDLIBS += -lwiringPi
SUBDIRS += $(HWBACKENDS_DIR)/hw_p1/
endif

ifneq (,$(findstring HAS_MQTT,$(CONFIG)))
LDLIBS += $(shell pkg-config --libs libmosquitto)
SUBDIRS += $(HWBACKENDS_DIR)/mqtt/
endif

ifneq (,$(findstring HAS_DBUS,$(CONFIG)))
LDLIBS += $(shell pkg-config --libs gio-unix-2.0)
SUBDIRS += dbus/
endif

TOPTARGETS := all clean distclean install uninstall doc

SUBDIRBIN := _payload.o
SRCROOT := $(CURDIR)

export SUBDIRBIN CC LD CFLAGS WFLAGS CONFIG SRCROOT FLEX BISON

$(TOPTARGETS): $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

SUBDIRS_OBJS := $(SUBDIRS:/=/$(SUBDIRBIN))
MAINOBJS += $(SUBDIRS_OBJS)

all:	MAKECMDGOALS ?= all
all:	$(SUBDIRS) $(MAIN)
	@echo	Done

$(MAIN): $(MAINOBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(WFLAGS) $(LDLIBS)

.c.o:
	$(CC) $(CFLAGS) $(WFLAGS) -MMD -c $< -o $@

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
