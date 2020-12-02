#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <serial_debug.h>

/*
    每个tty设备有3个缓冲队列，分别是读缓冲队列(read_ q)、写缓冲队列(write_ q)和辅助缓冲队列(secondary)，
    定义在 tty_ struct 结构中(include/linux/tty.h).
    对于每个缓冲队列，读操作是从缓冲队列的左端取字符，并且把缓冲队列尾(tail) 指针向右移动。
    而写操作则是往缓冲队列的右端添加字符，并且也把头(head)指针向右移动。
    这两个指针中，任何一个若移动到超出了缓冲队列的末端，则折回到左端重新开始。

    |-----------------------------------|
        ^                           ^
      tail, 读，右移                head, 写，右移

*/

#define DEBUG

void con_init();
extern void con_write(struct tty_struct *tty);

extern void keyboard_interrupt(char scancode);

// tty 数据结构的 tty_table 数组，包含3个初始化项数据
// 0 控制台
// 1 串口终端1
// 2 串口终端2
struct tty_struct tty_table[] = {
    {
        .pgrp = 0,
        .write = NULL,
        .flags = 0 | TTY_ECHO,
        .read_q = {
            .head = 0,
            .tail = 0
        },
        .write_q = {
            .head = 0,
            .tail = 0
        },
            .buffer = {
            .head = 0,
            .tail = 0
        }
    }
};

void tty_init() {
    tty_table[0].write = con_write;
    con_init();                 // 初始化控制台终端(console.c文件中)
}

// copy_to_buffer() 函数由键盘中断过程调用(通过do_keyboard_interrupt()),
// 用于根据终端termios 结构中设置的字符输入/输出标志(例如INLCR、OUCLC)对 read_q 队列中的字符进行处理，
// 把字符转换成以字符行为单位的规范模式字符序列，并保存在辅助字符缓冲队列( 规范模式缓冲队列) (secondary) 中，供上述tty_read()读取。
// 在转换处理期间，若终端的回显标志 L_ECHO 置位，则还会把字符放入写队列 write_q 中，并调用终端写函数把该字符显示在屏幕上。
// 如果是串行终端，那么写函数将是 rs_write() (在serial.c)。
// rs_ write() 会把串行终端写队列中的字符通过串行线路发送给串行终端，并显示在串行终端的屏幕上。
// copy_to_buffer() 函数最后还将唤醒等待着辅助缓冲队列的进程。
void copy_to_buffer(struct tty_struct *tty) {
    char ch;

    while(!EMPTY(tty->read_q)) {
        GETCH(tty->read_q, ch);
        switch(ch) {
            case '\b':
                // This is backspace char
                if (!EMPTY(tty->buffer)) {
                    if(tty->buffer.buf[tty->buffer.head - 1] == '\n')  // \n 不能被清除掉
                       continue ;
                    DEC(tty->buffer.head);
                } else {
                    continue;
                }
                break;
            case -1:
            case '\n':
                s_printk("Enter wake the tty read queue up\n");
                PUTCH(ch, tty->buffer);
                wake_up(&tty_table[0].buffer.wait_proc);
                break;
                // EOF
            default:
                if (!FULL(tty->buffer)) {
                    PUTCH(ch, tty->buffer);
                } else {
                    // here we need to sleep until the queue
                    // is not full
                }
                break;
        }

        if (tty->flags | TTY_ECHO) {
            PUTCH(ch, tty->write_q);
            tty->write(tty);
        }
    }
    return ;
}

static void sleep_if_empty(struct tty_queue *queue) {
	cli();
	while (!current->signal && EMPTY(*queue))
		interruptible_sleep_on(&queue->wait_proc);
	sti();
}

static void sleep_if_full(struct tty_queue *queue) {
	if (!FULL(*queue))
		return;
	cli();
	while (!current->signal && LEFT(*queue)<128)
		interruptible_sleep_on(&queue->wait_proc);
	sti();
}

void wait_for_keypress(void) {
	sleep_if_empty(&tty_table[0].buffer);
}

// tty 读函数
// 从终端辅助缓冲队列中读取指定数量的字符，放到用户指定的缓冲区中。
// channel: 子设备号
// buf: 用户缓冲区指针
// nr: 欲读字节数
// 返回已读字节数
int tty_read(unsigned channel, char *buf, int nr) {
#ifdef DEBUG
    s_printk("tty_read channel = %d, buf = 0x%x, nr = %d, pid = %d\n", channel, buf, nr, current->pid);
#endif
    struct tty_struct *tty;
    int len = 0;
    char ch;
    char *p = buf;
    if (channel > 2 || nr < 0) {
        return -1;
    }
    tty = &tty_table[channel];
    // interruptible_sleep_on(&tty->buffer.wait_proc);
    while (nr > 0){
        if (EMPTY(tty->buffer)) {
#ifdef DEBUG
            s_printk("tty->buffer empty to sleep\n");
#endif
            sleep_if_empty(&tty->buffer);
#ifdef DEBUG
            s_printk("tty->buffer empty from sleep\n");
#endif
        }

        GETCH(tty->buffer, ch);
        put_fs_byte(ch, p++);  // TODO: 为什么不可以
        // *p++ = ch;
        len++;
        nr--;
        // TODO: Change -1 to EOF
        if (ch == '\n' || ch == -1) {
            break;
        }
    }
    // s_printk("Buf = %s\n", buf);
    // s_printk("tty_read return len = %d\n", len);
    return len;
}

void tty_write(struct tty_struct* tty) {
    tty->write(tty);
    return;
}

int _user_tty_write(unsigned channel, char *buf, int nr) {
#ifdef DEBUG
    s_printk("user_tty_write channel = %d, buf = 0x%x, nr = %d, pid = %d\n", channel, buf, nr, current->pid);
#endif
    int i = 0;
    struct tty_struct *tty = &tty_table[channel];
    for (i = 0; i < nr; i++) {
        char c = get_fs_byte(buf + i);
        PUTCH(c, tty->write_q);
    }
    tty_queue_stat(&tty->write_q);
    tty_write(tty);
    return nr;
}