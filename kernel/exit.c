#include <errno.h>
#include <signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <serial_debug.h>


//// 向指定任务 p 发送信号 sig, 权限 priv。
// 参数：
// sig - 信号值；
// p - 指定任务的指针；
// priv - 强制发送信号的标志。即不需要考虑进程用户属性或级别而能发送信号的权利。
// 该函数首先判断参数的正确性，然后判断条件是否满足。
// 如果满足就向指定进程发送信号sig并退出，否则返回为许可错误号。
static inline int send_sig(long sig, struct task_struct* p, int priv) {
    s_printk("send_sig entered sig:%d\n", sig);
    // First check params
    if (!p || sig < 1 || sig > 32)
        return -EINVAL;

    // 如果强制发送标志置位，或者当前进程的有效用户标识符(euid)就是指定进程的euid（也即是自己）,
    // 或者当前进程是超级用户，则向进程 p 发送信号 sig，即在进程 p 位图中添加该信号，否则出错退出。
    // 其中suser()定义为(current->euid==0)，用于判断是否是超级用户。
    if(priv || current->euid == p->euid /*|| suser() */) // 目前我们没有对用户的权限检查
        p->signal |= (1<<(sig - 1));    // sig = (1 ~ 32) 所以减1
    else
        return -EPERM;
    return 0;
}

//// 系统调用kill()可用于向任何进程或进程组发送任何信号，而并非只是杀死进程。:-)
// 参数 pid 是进程号；sig 是需要发送的信号。
// 如果pid > 0, 则信号被发送给进程号是pid的进程。
// 如果pid = 0, 那么信号就会被发送给当前进程的进程组中的所有进程。
// 如果pid = -1,则信号sig就会发送给除第一个进程(初始进程init)外的所有进程
// 如果pid < -1,则信号sig将发送给进程组-pid的所有进程。
// 如果信号 sig=0, 则不发送信号，但仍会进行错误检查。如果成功则返回0.
// 该函数扫描任务数组表，并根据pid的值对满足条件的进程发送指定信号sig。若pid=0,
// 表明当前进程是进程组组长，因此需要向所有组内进程强制发送信号sig.
// sys_kill 为系统调用 kill 的入口点
int sys_kill(int pid, int sig) {
    s_printk("sys_kill entered pid:%d sig:%d\n", pid, sig);
    struct task_struct **p = task + NR_TASKS;
    int err, retval = 0;
    if (pid > 0) {
        while (--p > &FIRST_TASK) {
            if (*p && (*p)->pid == pid) {
                // 因为我们没有实现 sys.c 相关的逻辑，这里暂时给予执行 kill 的用户最高权限
                if ((err = send_sig(sig, *p, 1))) {
                    s_printk("send_sig error err = %d\n", err);
                    retval = err;
                }
            }
        }
    }
    return retval;
}
