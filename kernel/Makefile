include ../Makefile.header

LDFLAGS += -r

all: kernel.o
	
kernel.o: printk.o main.o
	$(LD) $(LDFLAGS) -o kernel.o printk.o main.o

.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<

clean:
	rm -f *.o
