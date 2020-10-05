CFLAGS += $(shell pkg-config --cflags gio-unix-2.0)
undefine WFLAGS

DBUSGEN_BASE := dbus-generated

DBUSGEN_SRCS := $(DBUSGEN_BASE).c

$(DBUSGEN_SRCS):	rwchcd_introspection.xml
	gdbus-codegen --generate-c-code $(DBUSGEN_BASE) --c-namespace dbus --interface-prefix org.slashdirt. rwchcd_introspection.xml

$(DBUSGEN_BASE).h:	$(DBUSGEN_SRCS)


SRCS := $(DBUSGEN_SRCS) dbus.c

include $(SRCROOT)/Makefile.sub

distclean:
	$(RM) *-generated.[ch]

# quick hack
dbus.o:	$(DBUSGEN_BASE).h
