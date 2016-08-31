CC = gcc
CFLAGS = -Wall -std=gnu99 -O2 -g -Wno-unused-function
LDLIBS = -lwiringPi
#SYSTEMDUNITDIR = $(shell pkg-config --variable=systemdsystemunitdir systemd)
DBUSSYSTEMDIR = /etc/dbus-1/system.d

SRCS = $(wildcard *.c)

OBJS = $(SRCS:.c=.o)

MAIN = rwchcd

.PHONY:	all clean install uninstall

all:	$(MAIN)
	@echo	Done

$(MAIN): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o *~ $(MAIN)

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
