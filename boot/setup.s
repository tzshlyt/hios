.code16

.text

.equ SETUPSEG,  0x9020
.equ INITSEG,   0x9000
.equ SYSSEG,    0x1000
.equ LEN, 50 

.globl _start
.globl begtext, begdata, begbass, endtext, enddta, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

show_text:
    mov $SETUPSEG, %ax
    mov %ax, %es
    mov $0x03, %ah
    xor %bh, %bh
    int $0x10

    mov $0x000a, %bx
    mov $0x1301, %ax
    mov $LEN, %cx
    mov $msg, %bp
    int $0x10

    ljmp $SETUPSEG, $_start

_start:

# 保存光标位置
# Comment for routine 10 service 3
# AH = 03
# BH = video page
# on return:
# CH = cursor starting scan line (low order 5 bits)
# CL = cursor ending scan line (low order 5 bits)
# DH = row
# DL = column
	mov $INITSEG, %ax
	mov %ax, %ds
	mov $0x03, %ah
	xor %bh, %bh
	int $0x10
	mov %dx, %ds:0

# 取扩展内存大小的值
# 1M 外的内存叫扩展内存
# Comment for routine 0x15 service 0x88
# AH = 88h
# on return:
# CF = 80h for PC, PCjr
# = 86h for XT and Model 30
# = other machines, set for error, clear for success
# AX = number of contiguous 1k blocks of memory starting
# at address 1024k (100000h)
	mov $0x88, %ah
	int $0x15
	mov %ax, %ds:2

# 取显卡显示模式
# Comment for routine 10 service 0xf
# AH = 0F
# on return:
# AH = number of screen columns
# AL = mode currently set (see VIDEO MODES)
# BH = current display page
	mov $0x0f, %ah
	int $0x10
	mov %bx, %ds:4
	mov %ax, %ds:6

# 检查显示方式(EGA/VGA)并取参数
# Comment for routine 10 service 0x12
# We use bl 0x10
# BL = 10  return video configuration information
# on return:
# BH = 0 if color mode in effect，彩色模式
#    = 1 if mono mode in effect，黑白模式
# BL = 0 if 64k EGA memory，显存大小
#    = 1 if 128k EGA memory
#    = 2 if 192k EGA memory
#    = 3 if 256k EGA memory
# CH = feature bits
# CL = switch settings
	mov $0x12, %ah
	mov $0x10, %bl
	int $0x10
	mov %ax, %ds:8
	mov %bx, %ds:10
	mov %cx, %ds:12

# 复制硬盘参数表信息
# 比较奇怪的是硬盘参数表存在中断向量里
# 第一个硬盘参数表的首地址在0x41中断向量处，
# 第二个参数的首地址表在0x46中断向量处，紧跟着第一个参数表, 
# 每个参数表长度为0x10 Byte

# 第一块硬盘参数表
	mov $0x0000, %ax
	mov %ax, %ds
	lds %ds:4*0x41, %si     # 取中断向量0现1的值，也即hd0参数的地址 ds:si
	mov $INITSEG, %ax
	mov %ax, %es
	mov $0x0080, %di        # 目标地址 es:di = 0x90080
	mov $0x10, %cx          # 16字节
	rep movsb
# 第二块硬盘参数表
	mov $0x0000, %ax
	mov %ax, %ds
	lds %ds:4*0x46, %si
	mov $INITSEG, %ax
	mov %ax, %es
	mov $0x0090, %di
	mov $0x10, %cx
	rep movsb

# 检查第二块硬盘是否存在，如果不存在的话就清空相应的参数表
# Comment for routine 0x13 service 0x15
# AH = 15h
# DL = drive number (0=A:, 1=2nd floppy, 80h=drive 0, 81h=drive 1)
# on return:
# AH = 00 drive not present
#    = 01 diskette, no change detection present
#    = 02 diskette, change detection present
#    = 03 fixed disk present
# CX:DX = number of fixed disk sectors; if 3 is returned in AH
# CF = 0 if successful
#    = 1 if error
	mov $0x1500, %ax
	mov $0x81, %dl      # 0x8X表示硬盘，0x80指第1个硬盘
	int $0x13
	jc no_disk1
	cmp $3, %ah
	je is_disk1
no_disk1:				# 没有第二块硬盘，那么就对第二个硬盘表清零，使用stosb
	mov $INITSEG, %ax
	mov %ax, %es
	mov $0x0090, %di
	mov $0x10, %cx
	mov $0x00, %ax
	rep stosb

is_disk1:

# 下面该切换到保护模式了
# 进行切换保护模式的准备操作
	cli				# 关中断

# 我们先将 system 从 0x1000:0000 移动到 0x0000:0000 处
# 由于当时假设 system 模块最大为 0x80000（512KB），其末端不会超过内存地址 0x90000，
# 所以 bootsect 会把自己移动到 0x90000 开始的地方，并把 setup 加载在它后面
	mov $0x0000, %ax
	cld					# Direction = 0 move forward
do_move:
	mov %ax, %es        # es:di 是目的地址，初始为 0x0:0x0
	add $0x1000, %ax
	cmp $0x9000, %ax	# Does we finish the move
	jz end_move
	mov %ax, %ds        # ds:si 是源地址，初始为 0x1000:0x0
	sub %di, %di
	sub %si, %si
	mov $0x8000, %cx	# Move 0x8000 word = 0x10000 Byte (64KB)
	rep movsw
	jmp do_move

# 下面我们加载 GDT, IDT 等

end_move:
	mov $SETUPSEG, %ax
	mov %ax, %ds        # ds 指向本程序 setup.s
	lgdt gdt_48
    lidt idt_48


# 开启A20地址线，
# 使得可以访问 1MB 以上的内存
# 至于是否真正开启了A20地址线，需要进入保护模式后测试一下
# 这个工作放在 head.s 中
	inb $0x92, %al
	orb $0b00000010, %al
	outb %al, $0x92

# 这里我们会对8259A进行编程
	mov $0x11, %al			# Init ICW1, 0x11 is init command

	out %al, $0x20			# 0x20 is 8259A-1 Port
	.word 0x00eb, 0x00eb	# Time Delay jmp $+2, jmp $+2
	out %al, $0xA0			# And init 8259A-2
	.word 0x00eb, 0x00eb
	mov $0x20, %al			# Send Hardware start intterupt number(0x20)
	out %al, $0x21			# From 0x20 - 0x27
	.word 0x00eb, 0x00eb
	mov $0x28, %al
	out %al, $0xA1			# From 0x28 - 0x2F
	.word 0x00eb, 0x00eb
	mov $0x04, %al			# 8259A-1 Set to Master
	out %al, $0x21
	.word 0x00eb, 0x00eb
	mov $0x02, %al			# 8259A-2 Set to Slave
	out %al, $0xA1
	.word 0x00eb, 0x00eb
	mov $0x01, %al			# 8086 Mode
	out %al, $0x21
	.word 0x00eb, 0x00eb
	out %al, $0xA1
	.word 0x00eb, 0x00eb
	mov $0xFF, %al
	out %al, $0x21			# Mask all the interrupts now
	.word 0x00eb, 0x00eb
	out %al, $0xA1

# 开启保护模式！
	mov %cr0, %eax
	bts $0, %eax		# Turn on Protect Enable (PE) bit, cr0 置1
	mov %eax, %cr0

# Jump to protected mode
	.equ sel_cs0, 0x0008
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	ljmp $sel_cs0, $0

# GDTR 信息
gdt_48:
    # 这里全局表长度社着为 2KB （0x7ff）即可，因为每个描述符 8 字节
    # 所以表中共可有 256 项					
	.word 0x800	            # gdt 表限长，2kB		 
	.word 512+gdt, 0x9	    # gdt 表基地址，0x0009 << 16 + 0x0200 + gdt

gdt:
    # offset 0x0
	.word	0,0,0,0
    # offset 0x08 代码段
    # 代码段的限长是 8MB = (0x7ff + 1) * 4KB = 2KB * 4KB
    # 这里加 1 是因为限长是从 0 开始算起的 
	.word	0x07FF          # 段限长 15-0
	.word	0x0000          # 基地址 15-0
	.word	0x9A00          # 1 00 1 1010 0x00 , 代码段为只读、可执行
	.word	0x00C0          # 0x00 1100 0000
    # offset 0x10 数据段 
	.word	0x07FF
	.word	0x0000
	.word	0x9200          # 1 00 1 0010 0x00，数据段为可读可写
	.word	0x00C0          

idt_48:
    # 先设置一个空表
    .word 0                 # limit = 0
    .word 0, 0              # base = 0L

msg:
    .byte 13, 10
    .ascii "You've successfully load the floppy into RAM"
    .byte 13, 10, 13, 10
    
.text
endtext:
.data
enddata:
.bss
endbss:

