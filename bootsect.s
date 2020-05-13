# AT&T 汇编
.code16

# SYS_SIZE is the number of clicks (16 bytes) to be loaded.
# 0x3000 is 0x30000 bytes = 196kB (若以1024字节为1KB，则应是192KB), 对于当前版本空间足够用
# 要加载的系统模块的长度，单位是节，16字节为1节, 目前够用
.equ SYSSIZE, 0x3000   

.global _start  # 程序入口
.global begtext, begdata, begbss, endtext, enddata, endbss      # 定义6个全局标识符
.text       # 文本段，.text等是伪操作符，告诉编译器产生文本段，.text用于标识
begtext:    # 文本段的开始位置。此处的.text .data .bss 表明这3个段重叠，不分段！
.data       # 数据段
begdata:
.bss        # 未初始化数据段
begbss:
.text

# 将操作系统装载到内存中，内存规划:
# 0x9000:0000   bootsect
# 0x9000:0200   setup
# 0x1000:0000   system

.equ SETUPLEN,  0x04        # setup 程序占用扇区数，一个扇区 512 字节
.equ BOOTSEG,   0x07c0      # BIOS 将 bootloader 加载到内存位置 (31KB处)
.equ INITSEG,   0x9000      # 我们的 bootloadr 代码会被移动到这里 (576KB处)
.equ SETUPSEG,  0x9020      # setup.s 代码会被移动到这里，bootloader 后的一个扇区 (576.5KB) 
.equ SYSSEG,    0x1000      # system 程序的装载地址, 因为当时system 模块不会超过 0x8000 字节大小（512kb）
                            # 后面 setup 程序会把 system 模块移动到物理内存起始位置，
                            # 这样 system 代码的地址也即等于物理地址,便于内核代码和数据进行操作
                            # 此时为实模式，内存寻址方式为 (段地址 << 4 + 偏移量) 可寻址的线性空间为 20 位

# ROOT_DEV: 0x000 - 根文件系统设备使用与引导同样的软驱设备
#           0x301 - 根文件系统设备在第一硬盘的第一分区上，等等。
# ROOT_DEV = 0x306 指定根文件系统设备是第二个硬盘的第一个分区
# 设备号 = 主设备号 << 8 + 次设备号
# 主设备号： 1-内存，2-磁盘，3-硬盘，4-ttyx，5-tty，6-并行口，7-非命名管道
# 0x300 - /dev/hd0 - 代表整个第1个磁盘
# 0x301 - /dev/hd1 - 第1个盘的第1个分区
# 0x305 - /dev/hd5 - 代表整个第2个硬盘, 由于硬盘可以有1-4个分区
# 0x306 - /dev/hd6 - 第2个盘的第1个分区
.equ ROOT_DEV,  0x301           # 指定 /dev/fda 为系统镜像所在的设备

.equ ENDSEG, SYSSEG + SYSSIZE   # system 停止加载的段地址 

ljmp $BOOTSEG, $_start   # 长跳转,修改 cs = 0x7c0, ip = _start

# 第一部分：复制 bootsect
_start:
    # 将启动扇区从 0x07c0:0000 移动到 0x9000:0000
    # rep mov 的用法如下
    # 源地址   ds:si = 0x07c0:0000 (si -> source index 源变址寄存器)
    # 目的地址 es:di = 0x9000:0000 (di -> destination index 目的变址寄存器)
    # 移动次数 %cx  = 256          (cx -> count 计数器寄存器)
    # 因为 movsw 每次移动一个 word(2Byte) 256 * 2 = 512 Byte, 即为启动扇区的大小
    mov $BOOTSEG, %ax   # 不能直接修改 ds 
    mov %ax, %ds        
    mov $INITSEG, %ax
    mov %ax, %es
    mov $256, %cx
    xor %si, %si
    xor %di, %di   
    rep movsw          # 重复执行并递减 cx 值 
    
    # 移动后跳转到新扇区           
    ljmp $INITSEG, $go  # 长跳转同时切换 cs:ip
 
go:
    mov %cs, %ax        # cs -> 0x9000
    mov %ax, %ds        # 代码整体位置发生变化，初始化 ds,es,ss 寄存器
    mov %ax, %es
    mov %ax, %ss
    mov $0xff00, %sp    # 设置好栈, ss:sp 即 0x9000:0xff00

# 第二部分：将软盘中 setup.s 加载到内存中
load_setup:
    # 将软盘中的 setup 加载到内存中，并跳转到相应的地址执行代码
    mov $0x0000, %dx    # dh -> head number 0磁头, dl -> drive number 0号盘
    mov $0x0002, %cx    # ch -> track/cylinder number 0轨道 ,cl -> sectors number, 因为1扇区是bootloader扇区
    mov $INITSEG, %ax   # es:bx -> pointer to buffer, 装载目的位置
    mov %ax, %es
    mov $0x0200, %bx    # 保存在0x9000:0x0200 
    mov $0x02, %ah      # ah -> 02 read disk sectors
    mov $SETUPLEN, %al      # al -> 读取多少个扇区 (1-128 dec.), 暂时读取 4 个扇区，因为我们代码很小
    int $0x13           # 调用 BIOS 磁盘中断   
    jnc setup_load_ok   # 没有异常，加载成功
    mov $0x0000, %dx
    mov $0x0000, %ax    # int 13, service 0: 重置磁盘 
    int $0x13
    jmp load_setup      # 一直重试, 直到加载成功

setup_load_ok:
    # 跳转到 setup 程序
    #mov $SETUPSEG, %ax   # 更新数据段寄存器
    #mov %ax, %cs         # 不能只修改 cs，
   
    #mov %ax, %ds 
    #ljmp $SETUSEG, $0x00    #Ox9020 

    # Get disk drive parameters, specifically nr of sectors/track
    # 获取磁盘驱动参数，特别是每道的扇区数量
    mov $0x00, %dl        # dl -> 0, drive number (0=A:, 1=2nd floppy, 80h=drive 0, 81h=drive 1) 驱动器号 
    mov $0x0800, %ax      # ah = 8 is get drive parameters
    int $0x13
    # 返回信息
    # 如果出错 cf 置位，ah -> 状态码
    # bl -> 驱动器类型
    #  01 - 5¼  360K	     03 - 3½  720K
	#  02 - 5¼  1.2Mb	     04 - 3½ 1.44Mb
    # ch -> 磁道数的低8位   cl -> (7-6) 磁道数高2位 (5-0) 每个磁道扇区数
    # dh -> 磁头数 0 开始，dl -> 驱动器数
    # es:di -> 软驱磁盘参数表 pointer to 11 byte Disk Base Table (DBT)
    mov $0x00, %ch          # 对于软盘来说（dl = 0）, 其最大磁道号不会超过256，ch足够表示，因此cl(6-7)为0
    mov %cx, %cs:sectors+0  # 上一句置ch = 0, 此时 cx 即每个磁道扇区数
    mov $INITSEG, %ax       # 因为上面取磁盘中断改掉了es值，这里重新改回
    mov %ax, %es

print_msg:
    # 显示一行信息
    mov $0x03, %ah      # 获取光标位置保存在 dx 里，dh -> 行， dl -> 列
    xor %bh, %bh        # bh -> video page
    int $0x10
    mov $21, %cx        # 输出长度 
    mov $msg1, %bp      # es:bp -> 字符串地址
    mov $0x1301, %ax    # ah -> 13h, al -> 01h 写入模式，光标移动
    mov $0x0007, %bx    # bh -> 00h video page number, bl -> attibute 07 白色属性 
    int $0x10           # video 中断

# 第三部分：将整系统镜像装载到 0x1000:0000 内存处    
    mov $SYSSEG, %ax
    mov %ax, %es        # es 做为参数
    call read_it        # 加载 
    call kill_motor     # 关闭驱动器马达，这样就可以知道驱动器的状态了

    mov %cs:root_dev, %ax   # 取设备号并判断是否已被定义
    cmp $0, %ax
    jne root_defined
    mov %cs:sectors+0, %bx
    mov $0x0208, %ax        # /dev/ps0 - 1.2MB
    cmp $15, %bx            # 判断每磁道扇区数是否 = 15
    je root_defined
    mov $0x021c, %ax        # /dev/PS0 - 1.44MB
    cmp $18, %bx        
    je root_defined 

undef_root:                 # 如果都不一样，死循环（死机）
    jmp undef_root

root_defined:
    mov %ax, %cs:root_dev+0     # 将检查过的设备号保存到 root_dev 中
    ljmp $SETUPSEG, $0          # 跳转到 setup.s 去执行，本程序结束


# 下面两个子程序，read_it 用于读取磁盘上的system模块，kill_moter 用于关闭软驱的马达
# 系统加载到内存 0x1000 << 4 处, 并确定没有跨越64KB内存边界 （0x1000 << 4 == 64KB）
# 我们试图尽快加载，只要可能，就每次加载整条磁道的数据

# 定义一些变量，用于读取软盘信息
sread:  .word 1 + SETUPLEN      # 当前轨道已读扇区数, 表示已经读入一个引导扇区和setup程序扇区数
head:   .word 0                 # 当前读头号
track:  .word 0                 # 当前轨道号

read_it:
    # 首先测试输入段值，从盘上读入的数据必须存放在内存地址 64KB 的边界开始处，否则进入死循环
    # 清 bx 寄存器，用于表示当前段内存存放数据的开始位置
    mov %es, %ax        # 将参数赋给 ex
    test $0x0fff, %ax   # test 指令按位进行逻辑与运算，与 and 指令的区别是两个操作数不会被改变
                        # 0x1000 & 0x0fff = 0x0000, 于是zf（零标志）置位，下一条 jne 条件不成立
                        # 0x2000 & 0x0fff = 0x0000, 即 es 必须为 0x1000，0x2000...
    
die:
    jne die             # if es is not at 64KB(0x1000) boundary, then stop here, es 必须位于64KB地址边界
    xor %bx, %bx        # 清 bx 寄存器，用于表示当前内存放数据的开始位置，bx（基地址寄存器）为段内偏移 

rp_read:
    # 判断是否已读入全部数据，比较当前所读段是否就是系统数据末端所处段（ENDSEG）
    # 如果不是跳转到 ok1_read 继续读，否则返回
    mov %es, %ax
    cmp $ENDSEG, %ax
    jb ok1_read         # if $ENDSEG > %es, then continue reading, else just return
    ret                 # 已经加载全部数据

ok1_read:
    # 计算和验证当前磁道需要读取的扇区数，放在 ax 寄存器中
    # 根据当前磁道未读取的扇区数以及段内数据偏移，计算出未读扇区数
    # 所读总字节数是否超过 64KB 段长度的限制，若超过，则计算此次最多能读入字节数（64KB - 段内偏移）
    # 反算出此次需要读取扇区数
    mov %cs:sectors+0, %ax  # ax -> 磁道扇区数，最大 6 位
    sub sread, %ax          # 减去当前磁道已读扇区数
    mov %ax, %cx            # cx = ax -> 当前磁道未读扇区数
    shl $9, %cx             # 左移动 << 9 , cx = cx * 512 未读字节数 
    add %bx, %cx            # cx = cx + 段内当前偏移值 bx, 此次读操作后，段内共读入的字节数
    jnc ok2_read            # cf = 0 跳转，如果没有超过64K，跳转到 ok2_read
    je ok2_read
    # 若加上此次将要读磁道上所有未读扇区时超过64KB，则计算此时最多能读入的字节数
    # （64KB - 段内读偏移），再转换成所需读取扇区数，其中0减某数就是取该数64KB的补值 
    xor %ax, %ax            # ax = 0
    sub %bx, %ax            # ax = ax - bx
    shr $9, %ax             # ax = ax/512

ok2_read:
    # 读取当前磁道上指定开始扇区 cl 和需要读取的扇区数 al 的数据到 es: bx 开始处
    # 然后统计当前磁道上已经读取的扇区数与磁道最大扇区数 sectors 作比较
    # 如果小于 sectors 说明当前磁道未读完，跳到 ok3_read 处继续读
    call read_track         # 读当前磁道上指定开始扇区和需要读扇区数的数据
    mov %ax, %cx            # cx -> 该次操作读取扇区数
    add sread, %ax          # 加上当前磁道上已经读取的扇区数 
    
    cmp %cs:sectors+0, %ax  # 如果当前磁道上还有扇区未读，则跳转到 ok3_read  
    jne ok3_read
    # 若该磁道的当前磁头面所有扇区已经读取，则读该磁道的下一磁面（1号磁头）上的数据，
    # 如果已经完成，则去读下一磁道        
    mov $1, %ax             # 
    sub head, %ax           # 判断当前磁头
    jne ok4_read            # 如果是0磁头，则再去读1磁头面上的扇区数据
    incw track              # 否则去读下一磁道

ok4_read:
    mov %ax, head           # 保存当前磁头号
    xor %ax, %ax            # 清当前磁道已读扇区数
    
ok3_read:
    # 如果当前磁道上还有未读扇区，则首先保存当前磁道已读扇区数，
    # 然后调整存放数据处的开始位置，若小于64KB边界值，则跳转到 rp_read 处继续读数据
    mov %ax, sread          # 保存当前磁道已读扇区数
    shl $9, %cx             # 上次已读扇区数 * 512 字节
    add %cx, %bx            # 调整当前段内数据开始位置
    jnc rp_read
    # 否则说明已经读取 64KB 数据，此时调整当前段，为下一段数据作准备             
    mov %es, %ax        
    add $0x1000, %ax        # 将段基址调整为指向下一个 64KB 内存开始处
    mov %ax, %es
    xor %bx, %bx            # 清段内数据偏移值            
    jmp rp_read             # 继续读数据

read_track:
    # 读当前磁道上指定开始扇区和需要读扇区数的数据到 es:bx 处
    # al -> 需要读扇区数，es:bx 缓冲区开始位置
    # track 当前磁道
    # head  当前磁头
    # sread + 1 扇区开始读
    push %ax
    push %bx
    push %cx
    push %dx
    mov track, %dx          # 取当前磁道号
    mov sread, %cx          # 取当前磁道上已读扇区数      
    inc %cx                 # cl -> sector number, 开始读扇区 = 已读 + 1
    mov %dl, %ch            # ch -> track/cylinder number, 当前磁道号
    mov head, %dx           # dx 获取当前磁头
    mov %dl, %dh            # dh -> head number, 磁头号 
    mov $0, %dl             # dl -> drive number,驱动号（0 表示当前A驱动器）
    and $0x0100, %dx        # 磁头号不大于1
    mov $2, %ah             # ah -> 0x02, 读磁盘服务
    int $0x13               
    jc bad_rt               # 出错，跳转到 bad_rt
    pop %dx
    pop %cx
    pop %bx
    pop %ax
    ret
   
bad_rt:
    # 读磁盘出错，执行驱动复位操作（磁盘中断功能号0），再跳转到read_track 处重试
    mov $0, %ax             # ah -> 0
    mov $0, %dx             # dl -> drive number
    int $0x13
    pop %dx
    pop %cx
    pop %bx
    pop %ax
    jmp read_track

kill_motor:
    # 关闭马达，这样进入内核后就能知道它所处状态，以后也无须担心它
    # 0x3f2 是软盘控制器的一个端口，被称为数字输出寄存器(DOR)端口,
    push %dx
    mov $0x3f2, %dx     
    mov $0, %al
    outsb
    pop %dx
    ret

sectors:            # 保存一个磁道多少个扇区
    .word 0

msg1: 
    .ascii "Hello Bootloader!"
    .byte 13, 10, 13, 10  #13 回车，10 换行  


.= 508                  # 等价于 .org 表示补零到地址 508 

root_dev:
    .word ROOT_DEV

boot_flag:      
    .word 0xaa55        # bootloader 魔法数字

.text
endtext:
.data
enddata:
.bss
endbss:
