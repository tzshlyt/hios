include Makefile.header

# $@ = target file
# $< = first dependency
# $^ = all dependencies

LDFLAGS += -Ttext 0 -e startup_32

.PHONY=clean run all
.PRECIOUS: %.o 		# 保留.o文件

OBJS = boot/head.o init/main.o kernel/kernel.o mm/mm.o

all: Image

system: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o system.sym
	strip system.sym -o system.o
	objcopy -O binary -R .note -R .comment system.o	system 		# 删除头部多余信息

kernel/kernel.o:
	make -C kernel

boot/head.o:
	make -C boot

boot/bootsect:
	make -C boot

boot/setup:
	make setup -C boot

init/main.o:
	make main.o -C init

mm/mm.o:
	make -C mm

Image: boot/bootsect boot/setup system 
	dd if=boot/bootsect of=Image bs=512 count=1
	dd if=boot/setup of=Image bs=512 count=4 seek=1
	dd if=system of=Image bs=512 seek=5
	@echo "Build bootimg done"

run: Image
	qemu-system-i386 -m 16M -boot a -fda Image

run-bochs: Image
	bochs -q

debug:
	qemu-system-i386 -m 16M -boot a -fda Image -S -s &
	gdb -ex "target remote :1234" -ex "symbol-file system.sym" 
	

clean:
	rm -f *.o *.sym 
	rm -f Image
	make clean -C boot
	make clean -C kernel
	make clean -C mm
	make clean -C init

