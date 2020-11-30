#ifndef _TTY_H
#define _TTY_H

#define TTY_BUF_SIZE 1024               // tty 缓冲区(缓冲队列)大小
#define TTY_ECHO 0x0001                 // 回显标

void tty_init();                        // tty 初始化
int tty_read(unsigned channel, char * buf, int nr);

// tty 字符缓冲队列数据结构
struct tty_queue {
    char buf[TTY_BUF_SIZE];
    struct task_struct *wait_proc;      // 等待该缓冲区的进程列表
    unsigned long head;
    unsigned long tail;
};

// tty 等待队列中缓冲区操作宏函数
// [-------------------------------]
//      ^                       ^
//    tail, 取,右移          head, 存, 右移
//
#define INC(a) ((a) = ((a)+1) & (TTY_BUF_SIZE-1))
#define DEC(a) ((a) = ((a)-1) & (TTY_BUF_SIZE-1))
#define EMPTY(a) ((a).head == (a).tail)
#define LEFT(a) (((a).tail-(a).head-1)&(TTY_BUF_SIZE-1))        // 缓冲区还可以存放字符长度(空闲区长度)
#define LAST(a) ((a).buf[(TTY_BUF_SIZE-1)&((a).head-1)])        // 缓冲区中最后一个位置
#define FULL(a) (!LEFT(a))                                      // 缓冲区已满
#define CHARS(a) (((a).head-(a).tail)&(TTY_BUF_SIZE-1))         // 缓冲区已存放字符长度
#define GETCH(queue,c) \
(void)({c=(queue).buf[(queue).tail];INC((queue).tail);})
#define PUTCH(c,queue) \
(void)({(queue).buf[(queue).head]=(c);INC((queue).head);})

// tty 数据结构
struct tty_struct {
    void (*write)(struct tty_struct *tty);      // tty 写函数指针
    int pgrp;                                   // 所属进程组
    unsigned int flags;
    struct tty_queue read_q;                    // tty 读队列
    struct tty_queue write_q;                   // tty 写队列
    struct tty_queue buffer;                    // buffer 是用户输入，会被tty_read 读走
};

extern struct tty_struct tty_table[];           // tty 结构数组
void tty_queue_stat(const struct tty_queue *q);
void tty_write(struct tty_struct *tty);
void copy_to_buffer(struct tty_struct *tty);

#endif