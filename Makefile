all: Image

.PHONY=clean run-qemu

run-qemu: bootsect
	qemu-system-i386 -boot a -fda Image

bootsect.o: bootsect.s
	as --32 bootsect.s -o bootsect.o

bootsect: bootsect.o ld-bootsect.ld
	ld -T ld-bootsect.ld bootsect.o -o bootsect
	objcopy -O binary -j .text bootsect 			# 删除头部多余信息

Image: bootsect demo
	dd if=bootsect of=Image bs=512 count=1
	dd if=demo of=Image bs=512 count=4 seek=1
	
demo.o: demo.s
	as --32 demo.s -o demo.o

demo: demo.o ld-bootsect.ld
	ld -T ld-bootsect.ld demo.o -o demo
	objcopy -O binary -j .text demo 			# 删除头部多余信息

clean:
	rm -f *.o *.out
	rm -f bootsect
