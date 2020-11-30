#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/tty.h>
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

void copy_to_buffer(struct tty_struct *tty) {
    char ch;
    struct tty_queue *read_q= &tty->read_q;
    struct tty_queue *buffer= &tty->buffer;

    while(!tty_isempty_q(&tty->read_q)) {
        ch = tty_pop_q(read_q);
        switch(ch) {
            case '\b':
                // This is backspace char
                if (!tty_isempty_q(buffer)) {
                    if(tty_queue_tail(buffer) == '\n')  // \n 不能被清除掉
                       continue ;
                    buffer->tail = (buffer->tail - 1) % TTY_BUF_SIZE;
                } else {
                    continue;
                }
                break;
            case '\n':
            default:
                if (!tty_isfull_q(buffer)) {
                    tty_push_q(buffer, ch);
                } else {
                    // here we need to sleep until the queue
                    // is not full
                }
                break;
        }

        if (tty->flags | TTY_ECHO) {
            tty_push_q(&tty->write_q, ch);
            tty->write(tty);
        }
    }
    return ;
}

void tty_write(struct tty_struct *tty) {
    tty->write(tty);
    return;
}