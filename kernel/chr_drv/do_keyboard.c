#include <linux/kernel.h>
#include <linux/head.h>
#include <asm/system.h>
#include <serial_debug.h>

extern void keyboard_interrupt(void);

void do_keyboard_interrupt(short scan_code) {
    s_printk("Keyboard Scancode 0x%x\n", scan_code);
    return;
}