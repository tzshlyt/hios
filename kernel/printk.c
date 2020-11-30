#include <stdarg.h>
#include <stddef.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <serial_debug.h>

void vsprintf(char *dest, char *fmt, ...);

void printk(char *fmt, ...) {
    int i = 0;
    char buf[TTY_BUF_SIZE] = "";
    char *ptr = buf;
    va_list ap;
    for (i = 0; i < TTY_BUF_SIZE; i++) {
        buf[i] = 0;
    }

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);

    while (*ptr) {
        tty_push_q(&tty_table[0].write_q, *ptr);
        tty_queue_stat(&tty_table[0].write_q);
        ptr++;
    }
    tty_write(&tty_table[0]);
}