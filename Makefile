# $@ = target file
# $< = first dependency
# $^ = all dependencies

AS=as
LD=ld

.PRECIOUS: %.o 		# 保留.o文件

Image: bootsect.bin setup.bin binary.bin
	dd if=bootsect.bin of=Image bs=512 count=1
	dd if=setup.bin of=Image bs=512 count=4 seek=1
	dd if=binary.bin of=Image bs=512 seek=5
	
.s.o:
	$(AS) --32 $< -o $@

%.bin: %.o
	$(LD) -T ld-bootsect.ld $< -o $@
	objcopy -O binary -j .text $@		# 删除头部多余信息

run: Image
	qemu-system-i386 -boot a -fda Image

run-bochs: Image
	bochs -q

clean:
	rm -f *.o *.out *.bin
	rm -f Image
