# $@ = target file
# $< = first dependency
# $^ = all dependencies


LDFLAGS += -Ttext 0

.PRECIOUS: %.o 		# 保留.o文件

Image: boot/bootsect.bin boot/setup.bin boot/binary.bin
	dd if=boot/bootsect.bin of=Image bs=512 count=1
	dd if=boot/setup.bin of=Image bs=512 count=4 seek=1
	dd if=boot/binary.bin of=Image bs=512 seek=5
	
.s.o:
	$(AS) $< -o $@ 

%.bin: %.o
	$(LD) $(LDFLAGS) $< -o $@
	objcopy -O binary -j .text $@		# 删除头部多余信息

run: Image
	qemu-system-i386 -boot a -fda Image

run-bochs: Image
	bochs -q

clean:
	rm -f boot/*.o boot/*.bin
	rm -f Image
