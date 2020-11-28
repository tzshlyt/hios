#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

typedef int sig_atomic_t;           // 定义信号原子操作类型
typedef unsigned int sigset_t;      // 32bit 信号集类型，一位表示一个信号，对于linux0.11已经够用了

#define _NSIG 32		            // 32种信号，所以下面的信号集也是32bit
#define NSIG _NSIG

// 定义信号宏

#define SIGHUP 		1               // Hang Up      挂断控制终端或进程
#define SIGINT 		2               // Interrupt    来自键盘的中断
#define SIGQUIT 	3               // Quit         来自键盘的退出
#define SIGILL 		4               // Illeagle     非法指令
#define SIGTRAP 	5               // Trap         跟踪断点
#define SIGABRT 	6               // Abort        异常结束
#define SIGIOT 		6               // IO Trap      同上
#define SIGUNUSED 	7               // Unused       没有使用
#define SIGFPE 		8               // FPE          协处理器出错
#define SIGKILL 	9               // Kill         强迫进程终止
#define SIGUSR1 	10              // User1        用户信号1，进程可使用
#define SIGSEGV		11              // Segment Violation    无效内存引用
#define SIGUSR2		12              // User2        用户信号2，进程可使用
#define SIGPIPE		13              // Pipe         管道写出错，无读者
#define SIGALRM		14              // Alarm        实时定时器报警
#define SIGTERM     15              // Terminate    进程终止
#define SIGSTKFLT	16              // Stack Fault  栈出错（协处理器）
#define SIGCHLD		17              // Child        子进程停止或终止
#define SIGCONT		18              // Continue     恢复进程继续执行
#define SIGSTOP		19              // Stop         停止进程的执行
#define SIGTSTP		20              // TTY Stop     tty发出停止进程，可忽略
#define SIGTTIN		21              // TTY IN       后台进程请求输入
#define SIGTTOU		22              // TTY Out      后台进程请求输出

// 下面定义 sigaction.sa_mask 需要的结构
#define SA_NOCLDSTOP    1           // 当子进程处于停止状态，就不对 SIGCHLD 处理
#define SA_NOMASK   0x40000000		// 不阻止在指定的信号处理程序中再次收到该信号
#define SA_ONESHOT  0x80000000		// 信号处理句柄一旦被调用就恢复到默认处理句柄

// 下面定义用于 sigprocmask(how, )，用来改变阻塞信号集(屏蔽码)。用于改变该函数行为。
#define SIG_BLOCK       0           /* for blocking signals */ // 在阻塞信号集中加上给定信号
#define SIG_UNBLOCK     1           /* for unblocking signals */ // 在阻塞信号集中删除给定信号
#define SIG_SETMASK     2           /* for setting the signal mask */ // 设置阻塞信号集

#define SIG_DFL     ((void(*) (int))0)		// 默认信号处理句柄
#define SIG_IGN     ((void(*) (int))1)		// 信号处理忽略句柄

// sigaction 的数据结构
// sa_handler: 是对应某信号指定要采取的行动。可以用上面的SIG_DFL，或SIG_IGN来忽略该信号，
//           也可以是指向处理该信号函数的一个指针。
// sa_mask: 给出了对信号的屏蔽码，在信号程序执行时将阻塞对这些信号的处理。
// sa_flags: 指定改变信号处理过程的信号集。它是上面 SA_NOCLDSTOP, SA_NOMASK, SA_ONESHOT 位标志定义的。
// sa_restorer: 是恢复函数指针，由函数库Libc提供，用于清理用户态堆栈。参见signal.c。
// 另外，引起触发信号处理的信号也将被阻塞，除非使用了SA NOMASK标志。
struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;               // 信号屏蔽码
    int sa_flags;                   // 指定改变信号处理过程的信号集
    void (*sa_restorer)(void);      // 恢复函数指针，用于清理用户态堆栈
};

// 下面 signal 函数用于是为信号 _sig 安装一新的信号处理程序(信号句柄)，与sigaction()类似。
// 该函数含有两个参数:指定需要捕获的信号 _sig; 具有一个参数且无返回值的函数指针 _func。
// 该函数返回值也是具有一个int参数(最后一个(int))且无返回值的函数指针，它是处理该信号的原处理句柄。
void (*signal(int _sig, void(*func)(int)))(int);
// 下面两函数用于发送信号。
// kill()用于向任何进程或进程组发送信号。
// raise()用于向当前进//程自身发送信号。其作用等价于kill (getpid ), sig)。
int raise(int sig);						            // 向自身发信号
int kill(pid_t pid, int sig);			            // 向某个进程发信号

// 对阻塞信号集的操作
// Not implemented in sigaction
int sigaddset(sigset_t *mask, int signo);           // 增加指定信号 signo
int sigdelset(sigset_t *mask, int signo);           // 删除信号
int sigemptyset(sigset_t *mask);                    // 清空屏蔽所有信号，即响应所有信号
int sigfillset(sigset_t *mask);                     // 置入所有所有信号，即屏蔽所有信号
int sigismember(sigset_t *mask, int signo);         // 用于测试一个指定信号是否存在 1:存在 0:不存在 -1:出错

// 对 set 中的信号进行检测，看是否有挂起信号。在set中返回进程中当前被阻塞的信号集
int sigpending(sigset_t *set);
// 下面函数用于改变进程目前被阻塞的信号集(信号屏蔽码)。
// 若 oldset 不是NULL，则通过其返回进程当前屏蔽信号集。
// 若 set 指针不是NULL，则根据 how 指示修改进程屏蔽信号集。
int sigprocmask(int how, sigset_t *set, sigset_t *oldset);
// 下面函数用 sigmask 临时替换进程的信号屏蔽码, 然后暂停该进程直到收到一个信号。
// 若捕捉到某一信号并从该信号处理程序中返回，则该函数也返回，并且信号屏蔽码会恢复到调用前的值。
int sigsuspend(sigset_t *sigmask);
// 改变对于某个信号 sig 的处理句柄
// int sigaction(int sig, struct sigaction *act, struct sigaction *oldact);

#endif
