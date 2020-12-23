include ../Makefile.header

LDFLAGS	+= -r
CFLAGS += -I../include

OBJS = buffer.o super.o inode.o file_table.o

.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<
.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<
.s.o:
	$(AS) -o $*.o $<

fs.o: $(OBJS)
	$(LD) -r -o fs.o $(OBJS)

clean:
	rm -rf *.o