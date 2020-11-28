#define __LIBRARY__
#include <linux/kernel.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <serial_debug.h>

// First we define kill system call

_syscall2(int, kill, int, pid, int, sig)
static inline _syscall1(int, sys_debug, char *, str)
static inline _syscall3(int, sigaction, int, signum, struct sigaction *, action,
        struct sigaction *, old_action)

extern void __sig_restore();
extern void __masksig_restore();

static void demo_handle(int sig) {
    sys_debug("Demo handler activate!\n");
    sig++; // 仅消除编译警告
}

// 信号机制是Linux 0.11为进程提供的一套“局部的类中断机制”，
// 即在进程执行的过程中，如果系统发现某个进程接收到了信号，就暂时打断进程的执行，
// 转而去执行该进程的信号处理程序，处理完毕后，再从进程“被打断”之处继续执行。
void signal_demo_main(void) {
    // kill(2, SIGSEGV);
    int ret = 0;
    struct sigaction sa_action;
    sa_action.sa_handler = demo_handle;
    sa_action.sa_flags |= SA_NOMASK;
    sa_action.sa_mask = 0;
    sa_action.sa_restorer = __sig_restore;
    ret = sigaction(SIGUSR1, &sa_action, NULL);
    if (ret) {
        panic("sigaction set failed");
    }
    kill(1, SIGUSR1);
    return;
}
