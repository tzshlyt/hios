#include <linux/kernel.h>
#include <signal.h>
#include <serial_debug.h>
#include <asm/segment.h>
#include <linux/sched.h>

// #define DEBUG

void do_exit(int error_code);

void dump_sigaction(struct sigaction *action) {
    s_printk("Sigaction dump\n");
    s_printk("addr = 0x%x, sa_mask = 0x%x, sa_handler = 0x%x, sa_restorer = 0x%x\n",
            action, action->sa_mask, action->sa_handler, action->sa_restorer);
}

// 系统调用的(int 0x80)中断处理程序中真正的信号预处理程序。
// 该段代码的主要作用是将信号处理句柄插入到用户程序堆栈中，
// 并在本系统调用结束返回后立即执行信号句柄程序，然后继续执行用户的程序。
// 这个函数处理比较粗略，尚不能处理进程暂停 SIGSTOP 等信号。
// 函数的参数是进入系统调用处理程序 system_call.s 开始，直到调用本函数前逐步
// 压入堆栈的值。这些值包括：
// 1. CPU执行中断指令压入的用户栈地址ss和esp、标志寄存器eflags和返回地址cs和eip；
// 2. 刚进入 system_call 时压入栈的寄存器ds,es,fs和edx，ecx,ebx；
// 3. 调用 sys_call_table 后压入栈中的相应系统调用处理函数的返回值(eax)。
// 4. 压入栈中的当前处理的信号值(signr)
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	unsigned long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
#ifdef DEBUG
    s_printk("Context signr = %d, eax = 0x%x, ebx = 0x%x, ecx = 0x%x\n \
            edx = 0x%x, fs = 0x%x, es = 0x%x, ds = 0x%x\n \
            eip = 0x%x, cs = 0x%x, eflags = 0x%x, esp = 0x%x, ss= 0x%x\n",
            signr, eax, ebx, ecx, edx, fs, es, ds, eip, cs, eflags ,esp, ss);
    s_printk("current pid = %d\n", current->pid);
    dump_sigaction(&current->sigaction[signr - 1]);
 #endif
    unsigned long sa_handler;
    unsigned long old_eip = eip;
    struct sigaction * sa = current->sigaction + signr - 1;
    unsigned int longs;
    unsigned long * tmp_esp;

    // 如果信号句柄为SIG_IGN(1,默认忽略句柄)则不对信号进行处理而直接返回；
    // 如果句柄为SIG_DFL(0,默认处理)，则如果信号是SIGCHLD也直接返回，否则终止进程的执行。
    // 句柄SIG_IGN被定义为1，SIG_DFL被定义为0。
    // do_exit()的参数是返回码和程序提供的退出状态信息。可作为wait()或waitpid()函数的状态信息。
    // wait() 或 waitpid()利用这些宏就可以取得子进程的退出状态码或子进程终止的原因(信号)
    sa_handler = (unsigned long) sa->sa_handler;
    if (sa_handler == 1)        // 信号处理忽略
        return;
    if(!sa_handler) {           // sa_handler = 0, 默认处理
        if (signr == SIGCHLD)   // 子进程停止或终止
            return;
        else
            // do_exit(1<<(signr-1));      // 不再返回到这里 TODO:
            return;

    }
    // OK,以下准备对信号句柄的调用设置。如果该信号句柄只需使用一次，则将该
    // 句柄置空。注意，该信号句柄已经保存在 sa_handler 指针中。
    // 在系统调用进入内核时，用户程序返回地址(eip,cs)被保存在内核态中。下面
    // 这段代码修改内核态堆栈上用户调用系统调用时的代码指针eip为指向信号处理句柄，
    // 同时也将sa_restorer、signr、进程屏蔽码(如果SA_NOMASK没置空)、eax
    // ecs,edx作为参数以及原调用系统调用的程序返回指针及标志寄存器值压入用户堆栈。
    // 因此在本次系统调用中断返回用户程序时会首先执行用户的信号句柄程序，然后再继续执行用户程序
    if((sa->sa_flags & (int)SA_ONESHOT)) {
        sa->sa_handler = NULL;
    }
    // 将内核态栈上用户调用系统调用下一条代码指令eip指向该信号处理句柄。
    // 由于C函数是传值函数，因此给eip赋值时需要使用'*(&eip)'的形式。另外，
    // 如果允许信号自己的处理句柄收到信号自己，则也需要将进程的阻塞码压入堆栈。
    // 这里请注意，使用如下方式对普通C函数参数进行修改是不起作用的。
    // 因为当函数返回时堆栈上的参数将会被调用者丢弃。这里之所以可以使用这种方式，
    // 是因为该函数是从汇编程序中被调用的，并且在函数返回后汇编程序并没有把调用 do_signal()
    // 时的所有参数都丢弃。eip等仍然在堆栈中。
    // sigaction 结构的 sa_mask 字段给出了在当前信号句柄(信号描述符)程序执行期间
    // 应该被屏蔽的信号集。同时，引起本信号句柄执行的信号也会被屏蔽。不过若
    // sa_flags中使用了SA_NOMASK标志，那么引起本信号句柄执行的信号将不会被屏蔽掉。
    // 如果允许信号自己的处理句柄程序收到信号自己，则也需要将进程的信号阻塞码压入堆栈。
    *(&eip) = sa_handler;
    longs = (sa->sa_flags & SA_NOMASK) ? 7:8;
    // 将原调用程序的用户堆栈指针向下扩展7(8)个字长(用来存放调用信号句柄的参数等)，
    // 并检查内存使用情况(例如如果内存超界则分配新页等)
    *(&esp) -= longs;
    verify_area((void *)esp, longs*4);
    // 在用户堆栈中从下到上存放sa_restorer、信号signr、屏蔽码blocked(如果SA_NOMASK
    // 置位)、eax,ecx,edx,eflags和用户程序原代码指针。
    tmp_esp = esp;
    put_fs_long((unsigned long)sa->sa_restorer, tmp_esp++);
    put_fs_long((unsigned long)signr, tmp_esp++);
    if (!(sa->sa_flags & SA_NOMASK))
        put_fs_long((unsigned long)current->blocked, tmp_esp++);
    put_fs_long((unsigned long)eax, tmp_esp++);
    put_fs_long((unsigned long)ecx, tmp_esp++);
    put_fs_long((unsigned long)edx, tmp_esp++);
    put_fs_long((unsigned long)eflags, tmp_esp++);
    put_fs_long((unsigned long)old_eip, tmp_esp++);
    current->blocked |= sa->sa_mask;        // 进程阻塞码(屏蔽码)添加上 sa_mask 中的码位
    // 这些代码全部都是向用户栈空间备份数据
    /*

            |----------------|
            |   用户栈空间     |
            |----------------|
            |   old_eip      |
            |----------------|
            |   edx          |
            |----------------|
            |   ecx          |
            |----------------|
            |   eax          |
            |----------------|
            |   blocked      |
            |----------------|
            |  signr         |
            |----------------|
            |   ss_restorer  |
            |----------------|

        信号处理函数执行完毕后，-定会执行“ret"这一行代码，于是此时处于栈顶的 sa->sa_restorer
        所代表的函数地址值就发挥作用了，ret 的本质就是用栈顶指针的地址值来设置EIP,然后跳转到EIP指向的地址位置去执行。
        此时就应该跳转到sa->sa_restorer 所代表的函数地址值位置去执行了。这个库函数将来会在信号处理工作结束后
        恢复用户进程执行的“指令和数据”，并最终跳转到用户程序的“中断位置”处执行。
        注意看sa_restorer最后一行汇编“ret",由于ret的本质就是用当前栈顶的值设置EIP,并使程序跳转到EIP指向的位置去执行，
        很显然，经过一系列清栈操作后，当前栈顶的数值就是“put fs_long(old_ eip,tmp esp++)”这行代码设置的，
        这个old eip 是产生软中断int0x80的下一行代码（中断返回前处理信号）。
        所以，ret 执行后，信号就处理完毕了，并最终回到用户程序中继续执行。
    */
}

// 获取当前任务信号屏蔽位图(屏蔽码或阻塞码)。sgetmask可分解为signal-get-mask。以下类似。
int sys_sgetmask() {
    return (int)current->blocked;
}

// 设置新的信号屏蔽位图。SIGKILL不能屏蔽。返回值是原信号屏蔽位图
int sys_ssetmask(int newmask) {
    int old;
    old = (int)current->blocked;
    // We cannot mask SIGKILL
    current->blocked = (unsigned long)(newmask & ~(1<<(SIGKILL-1)));
    return old;
}

// 把 sigaction 数据从 fs 数据段 from 位置复制到 to 处。
// 即从用户数据空间复制到内核数据段中。
static inline void get_new(char * from, char * to) {
	unsigned int i;
	for (i=0 ; i< sizeof(struct sigaction); i++) {
		*to = get_fs_byte(from);
        from++;
        to++;
    }
}

// 复制sigaction数据到fs数据段to处。即从内核空间复制到用户(任务)数据段中。
static inline void save_old(char *from, char *to) {
    unsigned int i;
    // 首先验证to处的内存空间是否足够大。然后把一个sigaction结构信息复制到fs段(用户)空间中。
    // 宏函数 put_fs_byte() 在 iniclude/asm/segment.h 中实现。
    verify_area(to, sizeof(struct sigaction));
	for (i=0; i< sizeof(struct sigaction); i++) {
	    put_fs_byte(*from, to);
        from++;
        to++;
    }
}

// sigaction()系统调用，改变进程在收到一个信号时的操作。signum是除了SIGKILL以外的任何信号。
// 如果新操作(action)不为空则新操作被安装。
// 如果oldaction指针不为空，则原来被保留到oldaction。
// 成功则返回0，否则为-1.
int sys_sigaction(int signum, const struct sigaction * action, struct sigaction *old_action) {
#ifdef DEBUG
    s_printk("sys_sigaction entered signum = %d\n", signum);
#endif
    struct sigaction tmp;

    if (signum < 1 || signum > 32 || signum == SIGKILL)
        return -1;
    // 在信号的signaction结构中设置新的操作(动作)。如果oldaction指针不为空的话，
    // 则将原操作指针保存到oldaction所指的位置。
    tmp = current->sigaction[signum - 1];
    get_new((char *) action, (char *) (signum-1+current->sigaction));
    if(old_action)
        save_old((char *) &tmp, (char *) old_action);
    // 如果允许信号在自己的信号句柄中收到，则令屏蔽码为0，否则设置屏蔽本信号。
    if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (sigset_t)(1<<(signum-1));

#ifdef DEBUG
    s_printk("current pid = %d\n", current->pid);
    dump_sigaction(&current->sigaction[signum-1]);
#endif

    return 0;
}