CC = gcc
#add -Wconversion when ready - -Wdouble-promotion should be checked but triggers warnings with printf
WARNINGS = -Wall -Wextra -Winline -Wdeclaration-after-statement -Wno-unused-function -Wno-double-promotion -Winit-self -Wswitch-default -Wswitch-enum -Wbad-function-cast -Wcast-qual -Wwrite-strings -Wjump-misses-init -Wlogical-op -Wvla
CFLAGS = $(WARNINGS) $(shell pkg-config --cflags gio-unix-2.0) -std=gnu99 -O0 -g -fstack-protector -Wstack-protector -fstrict-aliasing
LDLIBS = $(shell pkg-config --libs gio-unix-2.0) -lwiringPi -lm
SYSTEMDUNITDIR = $(shell pkg-config --variable=systemdsystemunitdir systemd)
DBUSSYSTEMDIR = /etc/dbus-1/system.d

SRCS = $(wildcard *.c)

OBJS = $(SRCS:.c=.o)

DEPS = $(SRCS:.c=.d)

MAIN = rwchcd

.PHONY:	all clean install uninstall

all:	rwchcd_dbus-generated.h $(MAIN)
	@echo	Done

$(MAIN): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

.c.o:
	$(CC) $(CFLAGS) -MMD -c $< -o $@

clean:
	$(RM) *.o *.d *~ $(MAIN)

rwchcd_dbus-generated.h:	rwchcd_introspection.xml
	gdbus-codegen --generate-c-code rwchcd_dbus-generated --c-namespace dbus --interface-prefix org.slashdirt. rwchcd_introspection.xml

install: $(MAIN) org.slashdirt.rwchcd.conf rwchcd.service
	install -d /var/lib/rwchcd/
	install -D -s $(MAIN) -t /usr/sbin/
	install -m 644 -D org.slashdirt.rwchcd.conf -t $(DBUSSYSTEMDIR)/
	install -m 644 -D rwchcd.service -t $(SYSTEMDUNITDIR)/
	systemctl enable rwchcd.service
	@echo Done

uninstall:
	$(RM) /usr/sbin/$(MAIN)
	$(RM) $(DBUSSYSTEMDIR)/org.slashdirt.rwchcd.conf
	$(RM) $(SYSTEMDUNITDIR)/rwchcd.service
	@echo Done

-include $(DEPS)
