include ../Makefile.header

LDFLAGS += -r
CFLAGS += -I../include

OBJS = printk.o panic.o traps.o asm.o

all: kernel.o
	
kernel.o: $(OBJS) 
	$(LD) $(LDFLAGS) -o kernel.o $(OBJS) 

.c.o:
	$(CC) $(CFLAGS) \
		-c -o $*.o $<

clean:
	rm -f *.o
