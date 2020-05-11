all: Image

.PHONY=clean run-qemu

run-qemu: Image
	qemu-system-i386 -boot a -fda Image

bootsect.o: bootsect.s
	as --32 bootsect.s -o bootsect.o

bootsect: bootsect.o ld-bootsect.ld
	ld -T ld-bootsect.ld bootsect.o -o bootsect
	objcopy -O binary -j .text bootsect 			# 删除头部多余信息

Image: bootsect setup
	dd if=bootsect of=Image bs=512 count=1
	dd if=setup of=Image bs=512 count=4 seek=1
	
setup.o: setup.s
	as --32 setup.s -o setup.o

setup: setup.o ld-bootsect.ld
	ld -T ld-bootsect.ld setup.o -o setup
	objcopy -O binary -j .text setup 			# 删除头部多余信息

clean:
	rm -f *.o *.out
	rm -f bootsect setup Image
