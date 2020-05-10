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

.equ BOOTSEG, 0x07c0
.equ INITSEG, 0x9000
.equ SETUPSEG, 0x9020

ljmp $BOOTSEG, $_start   # 长跳转, cs = 0x7c0, ip = _start

_start:
    # 获取光标位置
    mov $0x03, %ah 
    int $0x10

    mov $BOOTSEG, %ax
    mov %ax, %es        
    mov $_msg, %bp      # es:bp -> 字符串地址
    mov $0x1301, %ax    # ah -> 13h, al -> 01h 写入模式，光标移动
    mov $0x0007, %bx    # bh -> 00h video page number, bl -> 07 白色属性 
    mov $21, %cx 
    int $0x10 
         
loop: jmp loop

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
