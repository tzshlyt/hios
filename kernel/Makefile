include ../Makefile.header

LDFLAGS += -r
CFLAGS += -I../include

all: kernel.o
	
kernel.o: printk.o main.o
	$(LD) $(LDFLAGS) -o kernel.o printk.o main.o

.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<

clean:
	rm -f *.o
