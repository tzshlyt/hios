#ifndef _SIGNAL_H
#define _SIGNAL_H

typedef unsigned int sigset_t;  // 32bit 信号集，一位表示一个信号，对于linux0.11已经够用了

struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;               // 信号屏蔽吗
    int sa_flags;                   // 指定改变信号处理过程的信号集
    void (*sa_restorer)(void);      // 恢复函数指针，用于清理用户态堆栈
};

#endif

