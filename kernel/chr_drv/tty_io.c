#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>
#include <serial_debug.h>

extern void keyboard_interrupt(char scancode);

// 控制台初始化程序。在init/main.c中被调用
// 该函数首先根据 setup.s 程序取得的系统硬件参数初始化设置几个本函数专用的静态
// 全局变量。然后根据显示卡模式(单色还是彩色显示)和显卡类型(EGA/VGA还是CGA)
// 分别设置显示内存起始位置以及显示索引寄存器和显示数值寄存器端口号。最后设置
// 键盘中断陷阱描述符并复位对键盘中断的屏蔽位，以允许键盘开始工作。
void con_init() {
    register unsigned char a;
    set_trap_gate(0x21, &keyboard_interrupt);
    outb_p(0x21, inb_p(0x21)&0xfd);        // 取消对键盘中断的屏蔽，允许IRQ1。
    a = inb_p(0x61);                        // 读取键盘端口 0x61 (8255A端口PB)
	outb_p(0x61, a|0x80);                   // 设置禁止键盘工作（位7置位）
	outb(0x61, a);                          // 再允许键盘工作，用以复位键盘
}

void tty_init() {
    con_init();                 // 初始化控制台终端(console.c文件中)
}