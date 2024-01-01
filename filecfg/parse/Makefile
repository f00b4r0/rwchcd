EXTRAOBJS := filecfg_parser.tab.o filecfg_parser.lex.o

%.lex.c: %.l %.tab.h
	$(FLEX) -s -P$*_ -o$@ $<

%.tab.h %.tab.c: %.y
	$(BISON) -b $* -p $*_ -d $<

$(TARGET): $(EXTRAOBJS)

distclean:
	$(RM) *.output *.tab.h

# disable implicit rules we don't want
%.c: %.y
%.c: %.l
