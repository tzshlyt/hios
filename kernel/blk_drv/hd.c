#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <serial_debug.h>

#define MAJOR_NR 3
#include "blk.h"

extern void hd_interrupt(void);

int sys_setup(void * BIOS) {
    s_printk("sys_setup()\n");
    return 0;
}

static void read_intr(void) {
    s_printk("read_intr()\n");
}

static void write_intr(void) {
    s_printk("write_intr()\n");
}

void do_hd_request(void) {
    s_printk("do_hd_request()\n");
}

void unexpected_hd_interrupt(void) {
    printk("Unexpected HD interrupt\n");
}

// 硬盘系统初始化
// 设置硬盘中断描述符，并允许硬盘控制器发送中断请求信号。
// 该函数设置硬盘设备的请求项处理函数指针为 do_hd_request(), 然后设置硬盘中断门
// 描述符。Hd_interrupt(kernel/system_call.s)是其中断处理过程。硬盘中断号为
// int 0x2E(46),对应8259A芯片的中断请求信号IRQ13.接着复位接联的主8250A int2
// 的屏蔽位，允许从片发出中断请求信号。再复位硬盘的中断请求屏蔽位(在从片上)，
// 允许硬盘控制器发送中断信号。中断描述符表 IDT 内中断门描述符设置宏 set_intr_gate().
void hd_init() {
    s_printk("hd_init()\n");
    blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;      // do_hd_request()
	set_intr_gate(0x2E, &hd_interrupt);
	outb_p(0x21, inb_p(0x21)&0xfb);                      // 复位接联的主8259A int2的屏蔽位
	outb(0xA1, inb_p(0xA1)&0xbf);                        // 复位硬盘中断请求屏蔽位(在从片上)
}