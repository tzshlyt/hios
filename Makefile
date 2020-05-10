all: Image

.PHONY=clean run-qemu

run-qemu: bootsect
	qemu-system-i386 -boot a -fda bootsect

bootsect.o:
	as --32 bootsect.s -o bootsect.o

bootsect: bootsect.o ld-bootsect.ld
	ld -T ld-bootsect.ld bootsect.o -o bootsect
	objcopy -O binary -j .text bootsect 			# 删除头部多余信息

clean:
	rm -f *.o
	rm -f bootsect
