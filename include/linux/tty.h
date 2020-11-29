#ifndef _TTY_H
#define _TTY_H

#define TTY_BUF_SIZE 1024               // tty 缓冲区(缓冲队列)大小
#define TTY_ECHO 0x0001                 // 回显标

void tty_init();                        // tty 初始化

// tty 字符缓冲队列数据结构
struct tty_queue {
    char buf[TTY_BUF_SIZE];
    struct task_struct *wait_proc;      // 等待该缓冲区的进程列表
    unsigned long head;
    unsigned long tail;
};

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
int tty_isempty_q(const struct tty_queue *q);
int tty_isfull_q(const struct tty_queue *q);
char tty_pop_q(struct tty_queue *q);
char tty_queue_head(const struct tty_queue *q);
char tty_queue_tail(const struct tty_queue *q);
int tty_push_q(struct tty_queue *q, char ch);
void tty_queue_stat(const struct tty_queue *q);
void tty_write(struct tty_struct *tty);
void copy_to_buffer(struct tty_struct *tty);

#endif