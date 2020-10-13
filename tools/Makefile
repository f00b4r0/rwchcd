SRCS := 
CFLAGS := -I../

hwp1_prelays:	hwp1_prelays.c ../storage.c ../timekeep.c
	$(CC) -fdata-sections -ffunction-sections -Wl,--gc-sections -o $@ $^ $(CFLAGS) $(WFLAGS) $(LDLIBS)

clean: TARGET := hwp1_prelays

include ../Makefile.sub
