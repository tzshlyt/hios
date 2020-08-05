/*
 * 定义了用来在C语言中直接调用的汇编宏函数
 *
 */

#define sti()  __asm__ volatile ("sti"::)
#define cli()  __asm__ volatile ("cli"::)
#define nop()  __asm__ volatile ("nop"::)
#define iret() __asm__ volatile ("iret"::)

// 下面这个宏是通用的设置门描述符的宏，参数分别为
// gate_addr: IDT 描述符的地址
// type: 门类型
// dpl: 特权级别
// funaddr: 中断处理程序的32位地址
// 因为中断处理属于内核段代码，所以它们段选择符值均为 0x0008（在eax寄存器高字节中指定）
#define _set_gate(gate_addr, type, dpl, funaddr) \
    __asm__ volatile (\
            "movw %%dx, %%ax\n\t"/* 将类型标志字与偏移高字组合成描述符低4字节(eax) */ \
            "movw %0, %%dx\n\t"/* 将类型标志字与偏移高字组合成描述符高4字节(edx) */\
            "movl %%eax, %1\n\t"/* 分别设置门描述符的低4字节和高4字节 */\
            "movl %%edx, %2"\
            :\
            :"i" ((short)(0x8000 + ((dpl) << 13) + ((type) << 8))), \
            "o" (*((char *) (gate_addr))),\
            "o" (*(4 + (char *)(gate_addr))), \
            "a" (0x00080000), \
            "d" ((char*)(funaddr)))

// 陷阱门, Type = 0xF
#define set_trap_gate(n, funaddr) \
    _set_gate(&idt[n], 0xF, 0, funaddr)

// 中断门, Type = 0xE
#define set_intr_gate(n, funaddr) \
    _set_gate(&idt[n], 0xE, 0, funaddr)

#define set_system_gate(n, funaddr) \
    _set_gate(&idt[n], 0xF, 3, funaddr)
