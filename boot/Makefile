include ../Makefile.header

LDFLAGS += -Ttext 0

.PHONY=clean all

all: bootsect setup head.o

bootsect: bootsect.s
	@$(AS) -n -g -o bootsect.o bootsect.s
	@$(LD) $(LDFLAGS) -o bootsect bootsect.o
	@cp -f bootsect bootsect.sym
	@$(OBJCOPY) -R .pdr -R .comment -R.note -S -O binary bootsect

setup: setup.s
	@$(AS) -n -g -o setup.o setup.s
	@$(LD) $(LDFLAGS) -o setup setup.o
	@cp -f setup setup.sym
	@$(OBJCOPY) -R .pdr -R .comment -R.note -S -O binary setup

head.o: head.s
	@$(AS) -n -g -o head.o head.s

binary: binary.s
	@$(AS) -n -g -o binary.o binary.s
	@$(LD) $(LDFLAGS) -o binary binary.o
	@cp -f binary binary.sym
	@$(OBJCOPY) -R .pdr -R .comment -R.note -S -O binary binary

clean:
	rm -f bootsect *.o setup *.sym binary head