#define __LIBRARY__
#include <signal.h>
#include <unistd.h>
#include <errno.h>

// First we define kill system call

_syscall2(int, kill, int, pid, int, sig)
static inline _syscall1(int, sys_debug, char *, str)

// 信号机制是Linux 0.11为进程提供的一套“局部的类中断机制”，
// 即在进程执行的过程中，如果系统发现某个进程接收到了信号，就暂时打断进程的执行，
// 转而去执行该进程的信号处理程序，处理完毕后，再从进程“被打断”之处继续执行。
void signal_demo_main(void) {
    kill(2, SIGSEGV);
    return;
}
