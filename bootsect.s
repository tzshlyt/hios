# AT&T 汇编
.code16

.global begtext, begdata, begbss, endtext, enddata, endbss
.text       # 文本段，.text等是伪操作符，告诉编译器产生文本段，.text用于标识
begtext:    # 文本段的开始位置。此处的.text .data .bss 表明这3个段重叠，不分段！
.data       # 数据段
begdata:
.bss        # 未初始化数据段
begbss:
.text

.equ BOOTSEG, 0x07c0    # bootloader 加载到内存位置
.equ INITSEG, 0x9000
.equ DEMOSEG, 0x1000    # 测试使用

ljmp $BOOTSEG, $_start   # 长跳转, cs = 0x7c0, ip = _start

_start:
    # 获取光标位置
    mov $0x03, %ah 
    int $0x10

    mov $BOOTSEG, %ax   # 不能直接修改 es 
    mov %ax, %es        
    mov $_msg, %bp      # es:bp -> 字符串地址
    mov $0x1301, %ax    # ah -> 13h, al -> 01h 写入模式，光标移动
    mov $0x0007, %bx    # bh -> 00h video page number, bl -> 07 白色属性 
    mov $21, %cx 
    int $0x10           # video 中断

_load_demo:
    mov $0x0000, %dx    # dh -> head number 0磁头, dl -> drive number 0号盘
    mov $0x0002, %cx    # ch -> track/cylinder number 0柱面 ,cl -> sectors number 1 扇区是bootloader扇区
    
    mov $DEMOSEG, %ax   # es:bx -> pointer to buffer, 保存的位置
    mov %ax, %es
    mov $0x0200, %bx    # 暂时保存在0x1000: 0x0200 , 之后保存到 0x0000  

    mov $0x02, %ah      # ah -> 02 read disk sectors
    mov $0x4, %al       # al -> 读取多少个扇区 (1-128 dec.), 暂时读取 4 个扇区，因为我们代码很小

    int $0x13            # 磁盘中断   
    jnc _demo_load_ok
    jmp _load_demo

_demo_load_ok:
    # 跳转到 demo 程序
    mov $DEMOSEG, %ax   # 更新数据段寄存器
    mov %ax, %dx    
    
    ljmp $0x1020, $0    # 相当于 Ox1000:0x0200



_msg: 
    .ascii "Hello Bootloader!"
    .byte 13, 10, 13, 10  #13 回车，10 换行  


.= 510
boot_flag:      
    .word 0xaa55 

.text
endtext:
.data
enddata:
.bss
endbss:
