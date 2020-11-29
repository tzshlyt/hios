#include <errno.h>
#include <signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <serial_debug.h>
#include <sys/wait.h>
#include <asm/segment.h>
/*
    该程序主要描述了进程(任务)终止和退出的有关处理事宜。
    主要包含进程释放、会话(进程组)终止和程序退出处理函数以及杀死进程、终止进程、挂起进程等系统调用函数。
    还包括进程信号发送函数 send_ sig() 和 通知父进程子进程终止的函数 tell_father()。
*/

//// 释放指定进程占用的任务槽及其任务数据结构占用的内存页面。
// 参数 p 是任务数据结构指针。该函数在后面的 sys_kill() 和 sys_waitpid() 函数中被调用。
// 扫描任务指针数组表task[]以寻找指定的任务。如果找到，则首先清空该任务槽，
// 然后释放该任务数据结构所占用的内存页面，最后执行调度函数并在返回时立即退出。
// 如果在任务数组表中没有找到指定任务对应的项，则内核panic. ;-)
void release(struct task_struct *p) {
    int i;
    if (!p) {
        return;
    }
    for (i = 1; i < NR_TASKS; i++) {                    // 扫描任务数组，寻找指定任务
        if (task[i] == p) {
            task[i] = NULL;
            free_page((unsigned long)p);                         // 置空该任务项并释放相关内存页。
            schedule();                                 // 重新调度(似乎没有必要)
        }
    }
    panic("trying to release non-existent task");       // 指定任务若不存在则死机
}

//// 向指定任务 p 发送信号 sig, 权限 priv。
// 参数：
// sig - 信号值；
// p - 指定任务的指针；
// priv - 强制发送信号的标志。即不需要考虑进程用户属性或级别而能发送信号的权利。
// 该函数首先判断参数的正确性，然后判断条件是否满足。
// 如果满足就向指定进程发送信号sig并退出，否则返回为许可错误号。
static inline int send_sig(long sig, struct task_struct* p, int priv) {
    s_printk("send_sig entered sig = %d\n", sig);
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
    s_printk("sys_kill entered pid = %d, sig = %d\n", pid, sig);
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
    s_printk("sys_kill leaved, retval = %d\n", retval);
    return retval;
}

//// 通知父进程 - 向进程 pid 发送信号 SIGCHLD；默认情况下子进程将停止或终止。
// 如果没有找到父进程，则自己释放。但根据POSIX.1要求，若父进程已先行终止，
// 则子进程应该被初始进程1收容。
static void tell_father(int pid) {
    int i;
    if (pid) {
        for (i = 0; i < NR_TASKS; i++) {        // 扫描进城数组表，寻找指定进程pid，并向其发送子进程将停止或终止信号SIGCHLD。
            if (!task[i]) {
                continue;
            }
            if (task[i]->pid != pid) {
                continue;
            }
            task[i]->signal |= (1 << (SIGCHLD - 1));
            return;
        }
    }
    printk("BAD BAD - no father found\n\r");
    release(current);                           // 如果没有找到父进程，则自己释放
}

//// 程序退出处理函数。
// 该函数将把当前进程置为TASK_ZOMBIE状态，然后去执行调度函数schedule()，不再返回。
// 参数 code 是退出状态码，或称为错误码。
int do_exit(long code) {
    s_printk("do_exit(%d), pid = %d\n", code, current->pid);
    int i;
    // 首先释放当前进程代码段和数据段所占的内存页。
    free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
    // 如果当前进程有子进程，就将子进程的 father 置为 1 (其父进程改为进程1，即init进程)。
    // 如果该子进程已经处于僵死(ZOMBIE)状态，则向进程1发送子进程中止信号 SIGCHLD。
    for (i = 0; i < NR_TASKS; i++) {
        if (task[i] && task[i]->father == current->pid) {
            task[i]->father = 1;
            if (task[i]->state == TASK_ZOMBIE) {
                /* assumption task[1] is always init */
                send_sig(SIGCHLD, task[1], 1);
            }
        }
    }
    // TODO: 关闭当前进程打开着的所有文件。

    // TODO: 对当前进程的工作目录pwd，根目录root以及执行程序文件的i节点进行同步操作，
    // 放回各个i节点并分别置空(释放)。

    // TODO: 如果当前进程上次使用过协处理器，则将last_task_used_math置空。

    // TODO: 如果当前进程是leader进程，则终止该会话的所有相关进程。

    // 把当前进程置为僵死状态，表明当前进程已经释放了资源。并保存将由父进程读取的退出码
    current->state = TASK_ZOMBIE;
    current->exit_code = code;
    // 通知父进程，也即向父进程发送信号SIGCHLD - 子进程将停止或终止。
    tell_father(current->father);
    schedule();                     // 重新调度进程运行，以让父进程处理僵死其他的善后事宜。
    // 下面的return语句仅用于去掉警告信息。因为这个函数不返回，所以若在函数名前加关键字
    // volatile，就可以告诉gcc编译器本函数不会返回的特殊情况。这样可让gcc产生更好一些的代码，
    // 并且可以不用再写return语句也不会产生假警告信息。
    return -1;
}

//// 系统调用exit()，终止进程。
// 参数error_code是用户程序提供的退出状态信息，只有低字节有效。
// 把error_code左移8bit是 wait() 或 waitpid() 函数的要求。
// 低字节中将用来保存 wait() 的状态信息。例如，如果进程处理暂停状态(TASK_STOPPED),
// 那么其低字节就等于0x7f. wait() 或 waitpid() 利用这些宏就可以取得子进程的退出状态码或子进程终止的原因。
int sys_exit(int error_code) {
	return do_exit((error_code & 0xff)<<8);
}

//// 系统调用 waipid(). 挂起当前进程，直到 pid 指定的子进程退出(终止)或收到要求终止该进程的信号，
// 或者是需要调用一个信号句柄(信号处理程序)。如果 pid 所指向的子进程早已退出(已成所谓的僵死进程)，
// 则本调用将立刻返回。子进程使用的所有资源将释放。
// 如果pid > 0，表示等待进程号等于 pid 的子进程。
// 如果pid = 0, 表示等待进程组号等于当前进程组号的任何子进程。
// 如果pid < -1, 表示等待进程组号等于 pid 绝对值的任何子进程。
// 如果pid = -1, 表示等待任何子进程。
// 如 options = WUNTRACED, 表示如果子进程是停止的，也马上返回(无须跟踪)
// 若 options = WNOHANG, 表示如果没有子进程退出或终止就马上返回。
// 如果返回状态指针 stat_addr 不为空，则就将状态信息保存到那里。
// pid 是进程号，
// *stat_addr 是保存状态信息位置的指针，
// options 是 waitpid 选项。
int sys_waitpid(pid_t pid, unsigned long *stat_addr, int options) {
    s_printk("sys_waitpid pid = %d\n", pid);
    int flag, code;         // flag标志用于后面表示所选出的子进程处于就绪或睡眠态。
    struct task_struct **p;

    verify_area(stat_addr, 4);
repeat:
    flag = 0;
    // 从任务数组末端开始扫描所有任务，跳过空项、本进程项以及非当前进程的子进程项。
    for (p = &LAST_TASK; p > &FIRST_TASK; --p) {
        if (!(*p) || (*p) == current) {
            continue;
        }
        if ((*p)->father != current->pid) {
            continue;
        }
        // 此时扫描选择到的进程p肯定是当前进程的子进程。
        // 如果等待的子进程号pid>0，但与被扫描子进程p的pid不相等，说明它是当前进程另外的
        // 子进程，于是跳过该进程，接着扫描下一个进程。
        if (pid > 0) {
            if ((*p)->pid != pid) {
                continue;
            }
        // 否则，如果指定等待进程的 pid=0 , 表示正在等待进程组号等于当前进程组号的任何子进程。
        // 如果此时被扫描进程 p 的进程组号与当前进程的组号不等，则跳过。
        } else if(!pid) {
            if ((*p)->pgrp != current->pgrp) {
                continue;
            }
        // 否则，如果指定的pid < -1,表示正在等待进程组号等于 pid 绝对值的任何子进程。如果此时
        // 被扫描进程 p 的组号与 pid的绝对值不等，则跳过。
        } else if (pid != -1) {
            if ((*p)->pgrp != -pid) {
                continue;
            }
        }

        // 如果前3个对 pid 的判断都不符合，则表示当前进程正在等待其任何子进程，也即 pid=-1 的情况，
        // 此时所选择到的进程 p 或者是其进程号等于指定pid，或者是当前进程组中的任何子进程，或者
        // 是进程号等于指定 pid 绝对值的子进程，或者是任何子进程(此时指定的pid等于-1).
        // 接下来根据这个子进程 p 所处的状态来处理。
        switch ((*p)->state) {
            // 子进程 p 处于停止状态时，如果此时 WUNTRACED 标志没有置位，表示程序无须立刻返回，
            // 于是继续扫描处理其他进程。如果 WUNTRACED 置位，则把状态信息 0x7f 放入*stat_addr，并立刻
            // 返回子进程号pid.这里0x7f表示的返回状态是wifstopped（）宏为真。
            case TASK_STOPPED:
                if (!(options & WUNTRACED)) {
                    continue;
                }
                put_fs_long(0x7f, stat_addr);
                return (*p)->pid;
            break;
            // 如果子进程 p 处于僵死状态，则首先把它在用户态和内核态运行的时间分别累计到当前进程(父进程)中,
            // 然后取出子进程的 pid 和 退出码，并释放该子进程。最后返回子进程的退出码和pid.
            case TASK_ZOMBIE:
                current->cutime += (*p)->utime;
                current->cstime += (*p)->stime;
                flag = (*p)->pid;                   // 临时保存子进程pid
                code = (*p)->exit_code;             // 取子进程的退出码
                release(*p);
                put_fs_long((unsigned long)code, stat_addr);
                return flag;
            break;
            default:
                flag = 1;
                break;
        }
    }
    // 在上面对任务数组扫描结束后，如果flag被置位，说明有符合等待要求的子进程并没有处于退出或僵死状态。
    // 如果此时已设置 WNOHANG 选项(表示若没有子进程处于退出或终止态就立刻返回)，
    // 就立刻返回0，退出。否则把当前进程置为可中断等待状态并重新执行调度。当又开始执行本进程时，
    // 如果本进程没有收到除 SIGCHLD 以外的信号，则还是重复处理。
    // 否则，返回出错码‘中断系统调用’并退出。针对这个出错号用户程序应该再继续调用本函数等待子进程。
    if (flag) {
        if (options & WNOHANG) {                    // options = WNOHANG,则立刻返回。
            return 0;
        }
        current->state = TASK_INTERRUPTIBLE;        // 置当前进程为可中断等待态
        schedule();                                 // 重新调度
        if (!(current->signal &= ~(1 << (SIGCHLD - 1)))) {
            goto repeat;
        } else {
            return -EINTR;                          // 返回出错码(中断的系统调用)
        }
    }
    // 若没有找到符合要求的子进程，则返回出错码(子进程不存在)。
    return -ECHILD;
}
