CC = gcc
#add -Wconversion when ready
WARNINGS = -Wall -Wextra -Winline -Wdeclaration-after-statement -Wno-unused-function -Wdouble-promotion -Winit-self -Wswitch-default -Wswitch-enum -Wbad-function-cast -Wcast-qual -Wwrite-strings -Wjump-misses-init -Wlogical-op -Wvla
CFLAGS = $(WARNINGS) -std=gnu99 -O0 -g -fstack-protector -Wstack-protector -fstrict-aliasing
LDLIBS = -lwiringPi -lm
#SYSTEMDUNITDIR = $(shell pkg-config --variable=systemdsystemunitdir systemd)
DBUSSYSTEMDIR = /etc/dbus-1/system.d

SRCS = $(wildcard *.c)

OBJS = $(SRCS:.c=.o)

DEPS = $(SRCS:.c=.d)

MAIN = rwchcd

.PHONY:	all clean install uninstall

all:	$(MAIN)
	@echo	Done

$(MAIN): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

.c.o:
	$(CC) $(CFLAGS) -MMD -c $< -o $@

clean:
	$(RM) *.o *.d *~ $(MAIN)

install: $(MAIN) org.slashdirt.rpiloted.conf rpiloted.service
	install -D -s $(MAIN) -t /usr/sbin/
	install -m 644 -D org.slashdirt.rpiloted.conf -t $(DBUSSYSTEMDIR)/
	install -m 644 -D rpiloted.service -t $(SYSTEMDUNITDIR)/
	systemctl enable rpiloted.service
	@echo Done

uninstall:
	$(RM) /usr/sbin/$(MAIN)
	$(RM) $(DBUSSYSTEMDIR)/org.slashdirt.rpiloted.conf
	$(RM) $(SYSTEMDUNITDIR)/rpiloted.service
	@echo Done

-include $(DEPS)
