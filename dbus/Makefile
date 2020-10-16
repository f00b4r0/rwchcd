SYSTEMDUNITDIR := $(shell pkg-config --variable=systemdsystemunitdir systemd)
DBUSSYSTEMDIR := /etc/dbus-1/system.d

CFLAGS += $(shell pkg-config --cflags gio-unix-2.0)
undefine WFLAGS

DBUSGEN_BASE := dbus-generated

DBUSGEN_SRCS := $(DBUSGEN_BASE).c

$(DBUSGEN_SRCS):	rwchcd_introspection.xml
	gdbus-codegen --generate-c-code $(DBUSGEN_BASE) --c-namespace dbus --interface-prefix org.slashdirt. $<

$(DBUSGEN_BASE).h:	$(DBUSGEN_SRCS)


SRCS := $(DBUSGEN_SRCS) dbus.c

include $(SRCROOT)/Makefile.sub

install: org.slashdirt.rwchcd.conf rwchcd.service
	install -m 644 -D org.slashdirt.rwchcd.conf -t $(DBUSSYSTEMDIR)/
	install -m 644 -D rwchcd.service -t $(SYSTEMDUNITDIR)/
	systemctl enable rwchcd.service

uninstall:
	$(RM) $(DBUSSYSTEMDIR)/org.slashdirt.rwchcd.conf
	systemctl disable rwchcd.service
	$(RM) $(SYSTEMDUNITDIR)/rwchcd.service

distclean:
	$(RM) *-generated.[ch]

# quick hack
dbus.o:	$(DBUSGEN_BASE).h
