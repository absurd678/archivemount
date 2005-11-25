all: archivemount

CFLAGS += -D_FILE_OFFSET_BITS=64
#CFLAGS += -DNDEBUG

archivemount: archivemount.c
	$(CC) -ggdb -o archivemount $(CFLAGS) $(LDFLAGS) -larchive -lfuse $^

clean:
	rm archivemount
