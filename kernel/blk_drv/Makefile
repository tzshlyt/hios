include ../../Makefile.header

OBJS = hd.o	ll_rw_blk.o ramdisk.o

LDFLAGS	+= -r
CFLAGS += -I../../include

.PHONY=clean run all

all: blk_drv.a

blk_drv.a: $(OBJS)
	@$(AR) rcs blk_drv.a $(OBJS)
	@sync

.c.o:
	@$(CC) $(CFLAGS) \
		-c -o $*.o $<

clean:
	rm -f *.a
	rm -f *.o