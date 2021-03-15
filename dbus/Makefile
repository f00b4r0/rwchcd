SYSTEMDUNITDIR := $(shell pkg-config --variable=systemdsystemunitdir systemd)
DBUSSYSTEMDIR := /etc/dbus-1/system.d

CFLAGS += $(shell pkg-config --cflags gio-unix-2.0)
undefine WFLAGS

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
