# head 程序占有 25Kb + 184b 的空间
# 创建页目录表，页表，缓冲区，GDT，IDT，跳转到main函数执行

.text
.globl idt, gdt, pg_dir, tmp_floppy_area
pg_dir:             # 页目录将会保存在这里，最后会覆盖掉startup_32内容

.globl startup_32
#   段选择符格式:
#        15              3    2     0
#        ----------------------------
#        |  描述符索引   | TI | RPL |
#        ----------------------------
#      index: 用与选择指定描述符表中 8192 (2^13)个描述符中的一个
#      TI: 0 -> GDT, 1 -> LDT
#      RPL: 00 -> 内核级别，11 -> 用户级别

startup_32:
    movl $0x10, %eax            # 0x10 -> 10000b 请求特权级0（位1-0）、选择全局描述符表（位2）、选择第2项（位15-3）
                                # 指向表中的数据段描述符项
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    lss stack_start, %esp       # 表示stack_start -> ss:esp, 设置系统堆栈
                                # stack_start 定义在 kernel/sched.c 中, 在移动到任务0执行以后，该栈被用作任务0和任务1共同使用的用户栈
    call setup_idt              # 设置中断
    call setup_gdt              # 设置gdt

    movl $0x10, %eax            # 因为修改了gdt，所以需重新装载段寄存器
    mov %ax, %ds                # cs 代码段寄存器已经在 setup_gdt 中重新装载过
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    lss stack_start, %esp

    # 测试是否开启 A20 地址线
    xorl %eax, %eax
1:	incl %eax
	movl %eax, 0x000000		# Compare the address 0 with 1MB
	cmpl %eax, 0x100000
	je 1b					# 1b 表示向后跳转到标号 1 去，若 5f 则表示向前跳转到标号5去

# 检查486协处理器是否存
# 方法是修改寄存器 cr0，假设存在协处理器的情况下执行一个协处理器指令，如果出错说明不存在
	movl %cr0, %eax
	andl $0x80000011, %eax  # Save PG，PE，ET
	orl	$2, %eax			# Set MP Bit
	movl %eax, %cr0
	call check_x87
	jmp after_page_tables

check_x87:
	fninit                  # 向协处理器发出初始化命令
	fstsw %ax               # 取协处理器状态字到 ax 寄存器
	cmpb $0, %al            # 状态字应该为 0，否则说明协处理器不存在
	je 1f                   # 如果存在则跳转到标号 1 处，否则改写 cr0
	movl %cr0, %eax
	xorl $6, %eax           # reset MP，set EM
	movl %eax, %cr0
	ret

.align 2
1:	.byte 0xDB, 0xE4  # 287 协处理器码, 指令 fsetpm 的机器码
	ret

setup_idt:
    #   中断描述符结构:
    #       31                              15  14    13 12      7              0
    #       ----------------------------------------------------------------------
    #       ｜ 中断处理函数地址(offset)位 31-16 ｜ P | DPL | 0 | TYPE | 000 | 未使用  ｜
    #       ----------------------------------------------------------------------
    #       ｜ 段选择符(selector)  0x8: 代码段  |   中断处理函数地址 offset 位15-0     ｜
    #       ----------------------------------------------------------------------
    #   P: 段存在标志  1
	#   DPL: 特权级别  00
    #   TYPE: 中断描述符对应类型标志为 1110 (0xe): 中断门, 0xf: 陷阱门
	#
    #
    #   所以设置如下：
    #       63                   32
    #       | 0000  |    8e00     |
    #       | 0008  | ignore_init |
    #
    # 将中断描述符表 idt 设置成具有 256 个项，并都指向 ignore_init 中断门，然后加载中断描述符（lidt）
    # 真正实用的中断门以后再安装，当我们在其他地方认为一切正常再开启中断。该子程序会被页表覆盖。
    # eax 含有描述符低 4 字节，edx 含有高 4 字节
    lea ignore_init, %edx       # 将 ignore_int 的有效地址值（偏移值）赋给 edx 寄存器
    movl $0x00080000, %eax      # 将选择符 0x0008 置入 eax 高 16 位中
    movw %dx, %ax               # selector = 0x0008 = cs
                                # 偏移值低 16 位置入 eax 的低 16 位中，此时 eax 含有门描述符低 4 字节的值
    movw $0x8e00, %dx           # edx 含有门描述符高 4 字节值
	lea idt, %edi               # idt 是中断描述符表的地址

	mov $256, %cx
rp_sidt:					    # Fill the IDT Table will default stub entry
	movl %eax, (%edi)           # 将哑中断门描述符存入表中
	movl %edx, 4(%edi)          # edx 内容放到 edi + 4 内存处
	addl $8, %edi
	dec %cx
	jne rp_sidt
	lidt idt_descr			# Load IDT Registe

setup_gdt:
    lgdt gdt_descr
    ret

# Linus 将内核内存页表直接放在页目录之后，使用 4 个表来寻址16MB的物理内存
# 如果你有多于 16MB 内存，就需要在这里进行扩充修改
# 每个页表长 4Kb，每个页表项需要 4 字节，因此每个页表可以存放 1024 个表项，
# 如果一个页表项寻址 4KB，则一个页表可寻址 4MB 物理内存
# Make place for pg directory
.org 0x1000					# 从偏移 0x1000 处开始是第 1 个页表（偏移0开始处将存放页表目录）
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000                 # 定义下面的内存数据块从偏移 0x5000 处开始

# 当 DMA（直接存储器访问）不能访问缓冲块时，下面的 tmp_floppy_area 内存块就可供软盘驱动程序使用，
# 其地址需要对齐调整，这样就不会跨越 64KB 边界
tmp_floppy_area:
	.fill 1024, 1, 0		# 共保留 1024 项，每一项1字节，填充数值0

# 入栈操作用于跳转到 init/main.c 中 main（）作准备
after_page_tables:
	push $0                 # envp  main() 3 个参数
	push $0                 # argv
	push $0                 # argc
	pushl $L6               # 压入返回地址
	pushl $main             # 压入main() 函数代码地址
    jmp setup_paging
L6:
	jmp L6                  # main() 不应该返回


# 默认的中断“向量句柄”
int_msg:
    .asciz "Unknown interrupt\n"

.align 2                            # 按 4 字节方式对齐地址

ignore_init:
    pushl %eax
	pushl %ecx
	pushl %edx
	push %ds                        # 虽然是16位寄存器，入栈时仍然以32位形式入栈
	push %es
	push %fs
	movl $0x10, %eax                # 置段选择符（使ds，es，fs指向 gdt 表中的数据段）
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	pushl $int_msg                  # 把调用printk函数的参数指针入栈
	call printk                    # 该函数在 /kernel/printk.c  中
                                    # ‘_printk’ 是printk 编译后模块中内部表示法

	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret                            # 中断返回（把中断调用时压入栈的 cpu 标志寄存器（32bit）值也弹出


# 页目录表和页表结构:
#    ------------------------------------------------------------------
#    |    pg_dir    |     pg0     |     pg1   |    pg2    |     pg3   |
#    ------------------------------------------------------------------
#  0x0000        0x1000         0x2000      0x3000      0x4000      0x4fff
#
#
#  page table entry(PTE) 结构:
#     31                         12    11~9     8    7   6    5    4     3    2     1     0
#     ---------------------------------------------------------------------------------------
#     |  page frame address 31-12  |   AVAIL  | G | PAT | D | A | PCD | PWT | U/S | R/W | P |
#     ---------------------------------------------------------------------------------------
#
#   P: present, 存在位, 1 表示该页在内存中
#	R/W: 读写位，1 可读可写，0 只读
#	U/S: 用户/管理位，和读写位一起提供硬件保护,当特权级为3的进程要想访问页面时，需要通过页保护检查，而特权级为0的进程就可以绕过页保护。
#	PWT: (Page Write-Throught)，否采用写透方式，写透方式就是既写内存（RAM）也写高速缓存,该位为1表示采用写透方式
#	PCD: (Page Cache Disable)位，表示是否启用高速缓存,该位为1表示启用高速缓存
#	A: 访问位，为1表示该页被CPU访问过
#   D: 脏页位，当CPU对一个页面执行写操作，此为被赋1
#	PAT: 页属性表位，只适用于页目录项。如果置为1，页目录项指的是4MB的页面
#   G: 全局位，为1表示该页在高速缓存TLB中一直保存
#
#   启用分页机制需要完成下面三步
#	1. 准备好页目录表和页表
#	2. 将页表地址写入控制寄存器Cr3
#	3. 将寄存器Cr0的PG位置1
#
.align 2                        # 按 2^2 字节方式对齐内存地址边界
setup_paging:
   	movl $1024 * 5, %ecx		# 对5页内存（1页目录+4页表）清零
	xorl %eax, %eax
	xorl %edi, %edi
	# 页目录从 0x00 处开始，eax 内存存放到es:edi所指内存处，且edi增加4
	cld;rep;stosl

	# 设置页目录表中的项，因为内核共有4个页表所以设置4项
    # 页目录项的结构与与表中的结构一样，4个字节为1项
    # $pg0+7 表示 0x00001007, 是页目录表中的第一项
    # 则第1个页表所在的地址 = 0x00001007 & 0xfffff000 = 0x1000
    # 第1个页表的属性标志 = 0x00001007 & 0x00000fff = 0x07, 表示该页存在、用户可读可写
	movl $pg0+7, pg_dir			# +7 Means set attribute present bit, r/w user
	movl $pg1+7, pg_dir+4		# -- -- ---
	movl $pg2+7, pg_dir+8		# -- -- ---
	movl $pg3+7, pg_dir+12		# -- -- ---

    # 填写4个页表中所有项的内容，共有：4（页表）*1024（项/页表）=4096项（0-0xfff）
    # 能映射物理内存 4096*4Kb = 16 Mb
    # 每项内容是：当前项所映射的物理内存地址 + 该页的标志（这里均为7）
    # 使用的方法是是从最后一个页表的最后一项开始按倒序填写
    # 一个页表的最后一项在页表中的位置是 1023*4 = 4092。因此最后一项的位置就是$pg3+4092
	movl $pg3 + 4092, %edi          # edi -> 最后一页的最后一项
	movl $0xfff007, %eax			# 7 means present, r/w user attribute, 最后一项对应物理内存页面的地址是 0xfff000

	std                             # 方向位置位，edi值递减（4字节）
1:	stosl
	subl $0x1000, %eax              # 每填好一项，物理内存地址值减0x1000
	jge 1b                          # 如果小于0则说明全添写好

	# 设置页目录表基址寄存器cr3的值，指向页目录表。cr3 中保存的是页目录表的物理地址
	xorl %eax, %eax                 # 页表目录表在 0x0000 处
	movl %eax, %cr3                 # cr3 - page directory start

	# Then enable paging
	movl %cr0, %eax
	orl $0x80000000, %eax			# Set the paging bit, 31 位
	movl %eax, %cr0					# ENABLE PAGING NOW!
	ret

# 本程序到此结束


#   中断描述表寄存器
#   47             15      0
#   ------------------------
#   ｜   基地址    ｜ 限长 ｜
#   ------------------------
#
.align 2
.word 0                             # 这里先空出2字节，这样 .long idt 是4字节对齐
idt_descr:
    .word 256*8 - 1                 # idt表限长， 共256项，限长=长度-1 (0x7ff)
    .long idt                       # idt表基地址


#  全局描述符表寄存器
#   47             15      0
#   ------------------------
#   ｜   基地址     ｜ 限长  ｜
#   ------------------------

.align 2
.word 0
gdt_descr:
    .word 256*8-1                   # gdt表限长，这里全局表长度为2KB（0x7ff即可），每个8字节，所以256项
    .long gdt                       # gdt表基地址
	.align 8

idt:
    .fill 256, 8, 0         # idt is uninitialized

# 废弃setup中gdt，重新创建gdt，其段限长均设置为 16MB
# 原来 gdt 所在 0x90200...位置处，会在设计缓冲区时被覆盖

# 全局描述符表结构
#     31             23             20    19            15   14   12  11      7           0
#     -------------------------------------------------------------------------------------
#     |  base 31-24   | G | B/D | 0 | AVL | limit 19-16 | P | DPL | S | type | base 23-16 |
#     -------------------------------------------------------------------------------------
#     |   base 15-0                                     |          limit 15-0          	  |
#     -------------------------------------------------------------------------------------
#     G: 颗粒度，0: 字节为单位，1: 颗粒度为 4KB
#     AVL: 软件可用位，操作系统使用，但被Linux忽略
#     type: 类型, 1010 -> 代码段,可读可执行(0xa)，0010 -> 数据段,可读可写(0x2)
#     DPL: 特权级，00 -> 内核， 11 -> 用户
#	  S: 系统标志, 置0，系统段，存储诸如LDT这种关键的数据结构，否则它是一个普通的代码段或数据段
#     P: Segment-Present标志, 0: 表示段当前不在主存中, Linux 总是把这个标志（第47位）设为1，因为它从来不把整个段交换到磁盘上去
#	  B/D: 当描述符指向的是可执行代码段时，这一位叫做D位，D=1使用32位地址和32/8位操作数，
#		   D=0使用16位地址和16/8位操作数。如果指向的是向下扩展的数据段，这一位叫做B位，
#          B=1时段的上界为4GB，B=0时段的上界为64KB。如果指向的是堆栈段，这一位叫做B位，
#		   B=1使用32位操作数，堆栈指针用ESP，B=0时使用16位操作数，堆栈指针用SP
#
# 所以:
#     G = 1, AVL = 1, P = 1, DPL = 00, S = 1, limit = 16M
#
#      64                         32
#      |  00c0   |   9a00/9200    |
#      |  0000   |    0fff        |
#
gdt:
    # Empty Entry (FIRST ENTRY)
	.quad 0x0000000000000000            # 空项
	# BaseAddress = 0x00000000
	# Limit = 0xfff
	# Granularity = 1 means 4KB Segment limit are 4KB unit
	# TYPE = 0xA Executable Read
	# DPL = 0x00 S = 1 P = 1
	# Code Segment
	.quad 0x00c09a0000000fff            # 代码段描述符, 16MB
	# BaseAddress = 0x00000000
	# Limit = 0xfff
	# Granularity = 1 means 4KB Segment limit are 4KB unit
	# TYPE = 0x2 Read/Write
	# DPL = 0x00 S = 1 P = 1
	# Data Segment
	.quad 0x00c0920000000fff            # 数据段描述符, 16MB
	# Temporaray
	.quad 0x0000000000000000            # 系统调用段描述符, 没有用
	.fill 252, 8, 0                     # 预留 252 项空间，用于放置创建任务的局部描述符（LDT）和
                                        # 对应的任务状态段 TSS 的描述符

####################################################################################################
#
#   最终内存分布图:
#
#
#         --------------------------
#         |         main..         |
#         |------------------------|    0x064b8 (25k)
#         |    全局描述符表(2KB)     |
#         |------------------------|	0x05cb8
#         |    中断描述符表(2KB)     |
#         |------------------------|    0x054b8
#         |                        |
#         |      head.s 部分代码    |
#         |       剩余 184B         |
#         |------------------------|    0x05400
#         |    软盘缓冲区(1KB)       |
#         |------------------------|    0x05000 (20k)
#         |                        |
#         |     页表 1-3            |
#         |                        |
#         | -  -  -  -  -  -  -  - |    0x02000
#         |     页表0 (4KB)        |
#         |------------------------|    0x01000
#         |     页目                |
#         |------------------------|    0x00000
#
#

