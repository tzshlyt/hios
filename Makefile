include Makefile.header

# $@ = target file
# $< = first dependency
# $^ = all dependencies

LDFLAGS += -Ttext 0 -e startup_32 -nostdlib

.PHONY=clean run all
#.PRECIOUS: %.o 		# 保留.o文件

all: Image

OBJS = boot/head.o init/main.o
ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o
DRIVERS = kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
LIBS = lib/lib.o

system: $(OBJS) $(ARCHIVES) $(DRIVERS) $(LIBS)
	@$(LD) $(LDFLAGS) $(OBJS) $(ARCHIVES) $(DRIVERS) $(LIBS) -o system.sym
	strip system.sym -o system.o
	$(OBJCOPY) -O binary -R .note -R .comment system.o system 		# 删除头部多余信息

kernel/chr_drv/chr_drv.a:
	@make -C kernel/chr_drv/

kernel/blk_drv/blk_drv.a:
	@make -C kernel/blk_drv/

kernel/kernel.o:
	@make -C kernel

boot/head.o:
	@make -C boot

boot/bootsect:
	@make -C boot

boot/setup:
	@make setup -C boot

init/main.o:
	@make main.o -C init

mm/mm.o:
	@make -C mm

fs/fs.o:
	@make -C fs

lib/lib.o:
	@make -C lib

Image: boot/bootsect boot/setup system
	@echo "Start building image..."
	@dd if=boot/bootsect of=Image bs=512 count=1
	@dd if=boot/setup of=Image bs=512 count=4 seek=1
	@dd if=system of=Image bs=512 seek=5
	@echo "Build bootimg done"

run: Image
	$(QEMU) -m 16M -boot a -fda Image -serial stdio

run-bochs: Image
	$(BOCHS) -q

debug:
	$(QEMU) -m 16M -boot a -fda Image -S -s &
	gdb -ex "target remote :1234" -ex "symbol-file system.sym"


clean:
	rm -f *.o *.sym
	rm -f Image system
	make clean -C boot
	make clean -C kernel
	make clean -C mm
	make clean -C init
	make clean -C lib
	make clean -C fs

###################################################################################
# 							qemu 运行参数说明
###################################################################################
# `-hda file'        `-hdb file' `-hdc file' `-hdd file'
#     使用 file  作为硬盘0、1、2、3镜像。
# `-fda file'  `-fdb file'
#     使用 file  作为软盘镜像，可以使用 /dev/fd0 作为 file 来使用主机软盘。
# `-cdrom file'
#     使用 file  作为光盘镜像，可以使用 /dev/cdrom 作为 file 来使用主机 cd-rom。
# `-boot [a|c|d]'
#     从软盘(a)、光盘(c)、硬盘启动(d)，默认硬盘启动。
# `-snapshot'
#     写入临时文件而不写回磁盘镜像，可以使用 C-a s 来强制写回。
# `-m megs'
#     设置虚拟内存为 msg M字节，默认为 128M 字节。
# `-smp n'
#     设置为有 n 个 CPU 的 SMP 系统。以 PC 为目标机，最多支持 255 个 CPU。
# `-nographic'
#     禁止使用图形输出。
# 其他：
#     可用的主机设备 dev 例如：
#         vc
#             虚拟终端。
#         null
#             空设备
#         /dev/XXX
#             使用主机的 tty。
#         file: filename
#             将输出写入到文件 filename 中。
#         stdio
#             标准输入/输出。
#         pipe：pipename
#             命令管道 pipename。
#         等。
#     使用 dev 设备的命令如：
#         `-serial dev'
#             重定向虚拟串口到主机设备 dev 中。
#         `-parallel dev'
#             重定向虚拟并口到主机设备 dev 中。
#         `-monitor dev'
#             重定向 monitor 到主机设备 dev 中。
#     其他参数：
#         `-s'
#             等待 gdb 连接到端口 1234。
#         `-p port'
#             改变 gdb 连接端口到 port。
#         `-S'
#             在启动时不启动 CPU， 需要在 monitor 中输入 'c'，才能让qemu继续模拟工作。
#         `-d'
#             输出日志到 qemu.log 文件。
