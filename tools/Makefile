SRCS := 
CFLAGS := -I../
LDLIBS := -ldb

hwp1_prelays:	hwp1_prelays.c ../storage.c ../timekeep.c
	$(CC) -fdata-sections -ffunction-sections -Wl,--gc-sections -o $@ $^ $(CFLAGS) $(WFLAGS) $(LDLIBS)

temp_table:	temp_table.c
	$(CC) -o $@ $^ -lm

clean: TARGET := hwp1_prelays temp_table

include ../Makefile.sub
