VARLIBDIR := /var/lib/rwchcd

REVISION := $(shell git describe --tags --always --dirty)
HOST_OS := $(shell uname)
CC := gcc
FLEX := flex
BISON := bison
#add -Wconversion when ready - -Wdouble-promotion should be checked but triggers warnings with printf
WFLAGS := -Wall -Wextra -Winline -Wdeclaration-after-statement -Wno-unused-function -Wno-double-promotion -Winit-self -Wswitch-default -Wswitch-enum -Wbad-function-cast -Wcast-qual -Wwrite-strings -Wjump-misses-init -Wlogical-op -Wvla
OPTIMS := -O0 -g -ggdb3 -march=native -mcpu=native -mtune=native -fstack-protector -Wstack-protector -fstrict-aliasing -Wstrict-aliasing
CFLAGS := -I$(CURDIR) -std=gnu99 $(OPTIMS) -DRWCHCD_REV='"$(REVISION)"' -DRWCHCD_STORAGE_PATH='"$(VARLIBDIR)"'
LDLIBS := -lm

ifeq ($(HOST_OS),Linux)
CONFIG := -DHAS_DBUS -DHAS_HWP1 -DHAS_RRD -DDEBUG
CFLAGS += -D_GNU_SOURCE -pthread
SYSTEMDUNITDIR := $(shell pkg-config --variable=systemdsystemunitdir systemd)
DBUSSYSTEMDIR := /etc/dbus-1/system.d
else
CONFIG :=
endif

CFLAGS += $(CONFIG)

DBUSGEN_BASE := dbus-generated
HWBACKENDS_DIR := hw_backends

SRCS := $(wildcard *.c)

DBUSGEN_SRCS := $(DBUSGEN_BASE).c
SRCS := $(filter-out $(DBUSGEN_SRCS),$(SRCS))
ifeq (,$(findstring HAS_DBUS,$(CONFIG)))
SRCS := $(filter-out dbus.c,$(SRCS))
endif

# filter out log subsystems, re-add log_file which is libc-only, then add based on config
SRCS := $(filter-out $(wildcard log_*.c),$(SRCS))
SRCS += log_file.c
ifneq (,$(findstring HAS_RRD,$(CONFIG)))
SRCS += log_rrd.c
LDLIBS += -lrrd
endif

ifneq (,$(findstring HAS_HWP1,$(CONFIG)))
LDLIBS += -lwiringPi
SRCS += $(wildcard $(HWBACKENDS_DIR)/hw_p1/*.c)
endif

OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

DBUSGEN_OBJS := $(DBUSGEN_SRCS:.c=.o)
DBUSGEN_DEPS := $(DBUSGEN_SRCS:.c=.d)

MAIN := rwchcd
MAINOBJS := $(OBJS) filecfg_parser.tab.o filecfg_parser.lex.o
ifneq (,$(findstring HAS_DBUS,$(CONFIG)))
MAINOBJS += $(DBUSGEN_OBJS)
CFLAGS += $(shell pkg-config --cflags gio-unix-2.0)
LDLIBS += $(shell pkg-config --libs gio-unix-2.0)
endif

.PHONY:	all clean distclean install uninstall dbus-gen doc

all:	$(MAIN)
	@echo	Done

$(MAIN): $(MAINOBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(WFLAGS) $(LDLIBS)

%.lex.c: %.l %.tab.h
	$(FLEX) -s -P$*_ -o$@ $<

%.tab.h %.tab.c: %.y
	$(BISON) -b $* -p $*_ -d $<

.c.o:
	$(CC) $(CFLAGS) $(WFLAGS) -MMD -c $< -o $@

clean:
	$(RM) $(HWBACKENDS_DIR)/*/*.[od~]
	$(RM) *.o *.d *~ *.output $(MAIN)

distclean:	clean
	$(RM) *-generated.[ch]
	$(RM) -r doc

$(DBUSGEN_SRCS):	rwchcd_introspection.xml
	gdbus-codegen --generate-c-code $(DBUSGEN_BASE) --c-namespace dbus --interface-prefix org.slashdirt. rwchcd_introspection.xml

$(DBUSGEN_OBJS):	$(DBUSGEN_SRCS)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(DBUSGEN_BASE).h:	$(DBUSGEN_SRCS)
dbus-gen:	$(DBUSGEN_SRCS)

install: $(MAIN) org.slashdirt.rwchcd.conf rwchcd.service
	install -m 755 -o nobody -g nogroup -d $(VARLIBDIR)/
	install -D -s $(MAIN) -t /usr/sbin/
ifneq (,$(findstring HAS_DBUS,$(CONFIG)))
	install -m 644 -D org.slashdirt.rwchcd.conf -t $(DBUSSYSTEMDIR)/
	install -m 644 -D rwchcd.service -t $(SYSTEMDUNITDIR)/
	systemctl enable rwchcd.service
endif
	@echo Done

uninstall:
	$(RM) /usr/sbin/$(MAIN)
	$(RM) -r $(VARLIBDIR)
ifneq (,$(findstring HAS_DBUS,$(CONFIG)))
	$(RM) $(DBUSSYSTEMDIR)/org.slashdirt.rwchcd.conf
	systemctl disable rwchcd.service
	$(RM) $(SYSTEMDUNITDIR)/rwchcd.service
endif
	@echo Done

doc:	Doxyfile
	( cat Doxyfile; echo "PROJECT_NUMBER=$(REVISION)" ) | doxygen -
	
# quick hack
dbus.o:	$(DBUSGEN_BASE).h
# rebuild rwchcd.o if anything changes to update version
rwchcd.o:       $(filter-out rwchcd.o,$(OBJS)) filecfg_parser.tab.h

tools:	tools/hwp1_prelays

tools/hwp1_prelays:	tools/hwp1_prelays.o $(filter-out rwchcd.o hw_backends/hw_p1/hw_p1.o,$(MAINOBJS))
	$(CC) -o $@ $^ $(CFLAGS) $(WFLAGS) $(LDLIBS)

# disable implicit rules we don't want
%.c: %.y
%.c: %.l

-include $(DEPS) $(DBUSGEN_DEPS)
