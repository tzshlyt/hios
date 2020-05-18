
# $@ = target file
# $< = first dependency
# $^ = all dependencies

include Makefile.header

LDFLAGS += -Ttext 0 

.PHONY=clean run all
.PRECIOUS: %.o 		# 保留.o文件

all: Image

system: boot/head.o kernel/kernel.o
	$(LD) $(LDFLAGS) boot/head.o kernel/kernel.o -o system.sym
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

Image: boot/bootsect boot/setup system 
	dd if=boot/bootsect of=Image bs=512 count=1
	dd if=boot/setup of=Image bs=512 count=4 seek=1
	dd if=system of=Image bs=512 seek=5
	@echo "Build bootimg done"

run: Image
	qemu-system-i386 -boot a -fda Image

run-bochs: Image
	bochs -q

run_debug:
	qemu-system-i386 -boot a -fda Image -S -s

clean:
	rm -f *.o *.sym 
	rm -f Image
	make clean -C boot
	make clean -C kernel
