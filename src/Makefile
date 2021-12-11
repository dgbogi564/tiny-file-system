CC=gcc
CFLAGS= -g -Wall -D_FILE_OFFSET_BITS=64
LDFLAGS=-lfuse

OBJ=tfs.o block.o

tfs: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o tfs

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f *.o tfs
