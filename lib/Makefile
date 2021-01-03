include ../Makefile.header

CFLAGS += -I../include
LDFLAGS += -r

all: lib.o

.c.o:
	@$(CC) $(CFLAGS) \
		-c -o $*.o $<
.s.o:
	@$(AS) -o $*.o $<

.c.s:
	@$(CC) $(CFLAGS) \
	-S -o $*.s $


OBJS = _exit.o wait.o getline.o printf.o string.o open.o error.o read.o dup.o close.o


lib.o: $(OBJS)
	$(LD) $(LDFLAGS) -o lib.o $(OBJS)

clean:
	rm -rf *.o
