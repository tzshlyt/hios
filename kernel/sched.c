/*
 * 内核进程调度管理
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sys.h>
#include <asm/system.h>
#include <asm/io.h>
#include <serial_debug.h>

// #define DEBUG

extern int timer_interrupt(void);
extern int system_call(void);

// 该宏取信号nr在信号位图中对应位的二进制数值。信号编号1-32.比如信号5的位图
// 数值等于 1 <<(5-1) = 16 = 00010000b
#define _S(nr) (1<<((nr)-1))
// 除了SIGKILL 和 SIGSTOP 信号以外其他信号都是可阻塞的(...1011,1111,1110,1111,111b)
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

// 定义任务联合体
// 每个任务(进程)在内核态运行时都有自己的内核态堆栈。这里定义了任务的内核态堆栈结构
// 因为一个任务的数据结构与其内核态堆栈在同一内存页中，所以从堆栈段寄存器ss可以获得其数据段选择符
union task_union {
    struct task_struct task;
    char stack[PAGE_SIZE];
};
static union task_union init_task = {INIT_TASK,};   // 定义初始任务的数据 sched.h

long user_stack[PAGE_SIZE >> 2];
long startup_time = 0;                                      // 开机时间，从1970开始计时的秒数
struct task_struct *current = &(init_task.task);            // 当前任务指针（初始化指针任务0）
struct task_struct *last_task_used_math = NULL;             // 处理过协处理任务的指针
struct task_struct *task[NR_TASKS] = {&(init_task.task),};  // 定义任务指针数组

// PC机8253定时芯片的输入时钟频率约为1.193180MHz. Linux内核希望定时器发出中断的频率是
// 100Hz，也即没10ms发出一次时钟中断。因此这里的LATCH是设置8253芯片的初值。
#define LATCH (1193180/HZ)

// 定义用户堆栈，共1K项，容量4K字节
// 在内核初始化操作中被用作内核栈
// 初始化完成后被用作任务0的用户态堆栈
// 在运行任务0之前是内核栈，以后用作任务0和1的用户栈
// 下面结构用于设置堆栈ss:esp, 见head.s
// ss -> 内核数据段选择符0x10
// esp -> user_stack数组最后一项后面，因为栈是递减的
// 我们测算出其起始位置为 0x1e25c (120k)
struct {
    long *a;
    short b;
} stack_start = {&user_stack[PAGE_SIZE >> 2], 0x10};

// 首先把当前任务置为不可中断的等待状态(只能由wake_up函数来唤醒)，并让睡眠队列头指针指向当前任务，执行调度函数，直到明确的唤醒时才会返回，该任务重新开始执行。
// 因为如果没有被唤醒(即state置0)是不可能被调度的，调度算法只会选出”状态为0”的进程进行调度运行
// 该函数提供了进程与中断处理程序之间的同步机制
// p: 等待任务队列头指针
void sleep_on(struct task_struct **p) {
    struct task_struct *tmp;
    if (!p)                                     // 若指针无效，则退出。（指针所指对象可以是NULL， 但是指针本身不应该是0)
        return;
    if (current == &(init_task.task))           // 当前任务是0，则死机
        panic("task[0] trying to sleep");
    tmp = *p;                                   // 让tmp指向已经在等待队列的任务(如果有的话)
    *p = current;                               // 把当前任务插入到 *p 的等待队列中
    current->state = TASK_UNINTERRUPTIBLE;      // 将当前任务置为不可中断的等待状态
    schedule();                                 // 执行重新调度
    *p = tmp;                                   // 只有当这个等待任务被唤醒时，调度程序才返回到这里，表示本进程已被明确唤醒（就绪态）
    if (tmp)                                    // 肯能存在多个任务此时被唤醒，那么如果还存在等待任务，则将状态设置为”就绪
        tmp->state = TASK_RUNNING;
}

void schedule(void) {
    int i, next, c;
    struct task_struct **p;         // 任务结构指针的指针

    // 进入schedule函数后，先对所有进程进行第-次遍历信号检测，
    // 如果发现哪个进程接收到了指定的信号，而且该进程还是可中断等待状态，那么就将该进程设置为就绪状态。
    // 如果进程处于不可中断等待状态，即使它收到了信号，状态也不会改变。
    // 从任务数组中最后一个任务开始循环检测 alarm。在循环时跳过空指针项。
    for(p = &LAST_TASK; p > &FIRST_TASK; --p) {
        if (*p) {
            // 如果设置过任务的定时值alarm，并且已经过期(alarm<jiffies)，则在
            // 信号位图中置SIGALRM信号，即向任务发送SIGALARM信号。然后清alarm。
            // 该信号的默认操作是终止进程。jiffies是系统从开机开始算起的滴答数(10ms/滴答)。
            if ((*p)->alarm && (*p)->alarm < jiffies) {
                (*p)->signal |= (1 << (SIGALRM-1));
#ifdef DEBUG
                s_printk("process get SIGALRM pid: %d signal: 0x%x mask: 0x%x blockable= 0x%x\n", \
                       (*p)->pid, (*p)->signal, (*p)->blocked, _BLOCKABLE);
#endif
                (*p)->alarm = 0;
            }
            // 如果信号位图中除被阻塞的信号外还有其他信号，并且任务处于可中断状态，
            // 则置任务为就绪状态。其中'~(_BLOCKABLE & (*p)->blocked)'用于忽略被阻塞的信号，
            // 但 SIGKILL 和 SIGSTOP 不能被阻塞。
            if(((unsigned long)((*p)->signal) & (unsigned long)(((unsigned long)(_BLOCKABLE) & (~(*p)->blocked))))  \
                        && (unsigned long)((*p)->state) \
                        == TASK_INTERRUPTIBLE) {
                (*p)->state = TASK_RUNNING;
            }
        }
    }

    // 从后往前遍历任务，找到就绪任务剩余执行时间counter最大的任务，切换并运行
    while (1) {
        c = -1;
        next = 0;
        i = NR_TASKS;
        p = &task[NR_TASKS];
        // 这段代码也是从任务数组的最后一个任务开始循环处理，并跳过不含任务的数组槽。
        // 比较每个就绪状态任务的counter (任务运行时间的递减滴答计数)值，哪一个值大，说明运行时间还不长，
        // next就指向哪个的任务号。
        while (--i) {               // 跳过任务 0
            if (!*(--p))            // 跳过不含任务的数组槽
                continue;
            if ((*p)->state == TASK_RUNNING && (*p)->counter > c) {  // 找出任务运行时间的递减滴答计数最大的，运行时间不长
                c = (*p)->counter;
                next = i;
            }
        }
        // 如果有任务 counter > 0 或者没有可运行的任务，此时 c 仍然等于-1，next = 0，则退出
        // 否则根据每个任务的优先值，更新每个任务的 counter 值，然后重新比较,
        // counter 值的计算方式为 counter = counter/2 + priority，注意这里计算不考虑进程的状态
        if (c) break;               // 注意: if(-1) 返回 true
        for(p = &LAST_TASK; p > &FIRST_TASK; p--) {    // 跳过任务 0
            if (*p) {
                (*p)->counter = ((*p)->counter >> 1) + (*p)->priority;
            }
        }
    }
    // s_printk("Scheduler select task %d\n", next);
    // 若没有任务可运行时，next为0，会去执行任务0。此时任务0仅执行pause()系统调用，并又会调用本函数
    // s_printk("[%d] Scheduler select task %d\n", jiffies, next);
    switch_to(next);
}

void show_task_info(struct task_struct *task) {
    s_printk("Current task Info\n================\n");
    s_printk("pid = %d\n", task->state);
    s_printk("counter = %d\n", task->counter);
    s_printk("start_code = %x\n", task->start_code);
    s_printk("end_code = %x\n", task->end_code);
    s_printk("brk = %x\n", current->ldt[0]);
    s_printk("gid = 0x%x\n", current->gid);
    s_printk("tss.ldt = 0x%x\n", current->tss.ldt);
    // s_printk("tss.eip = 0x%x\n", current->eip);
}

// 唤醒 *p 指向的任务
// *p 是任务等待队列的头指针
// 由于新等待任务是插在头部的，所以唤醒的是最后进入的等待队列的任务
void wake_up(struct task_struct **p) {
    if (p && *p) {
        (**p).state = TASK_RUNNING;                // 置为就绪(可运行)状态TASK_RUNNING.
        *p = NULL;
    }
}

// 将当前任务置为可中断的等待状态，并放入*p指定的等待队列中。
void interruptible_sleep_on(struct task_struct **p) {
    struct task_struct *tmp;
    // 若指针无效，则退出。(指针所指向的对象可以是NULL，但指针本身不会为0)。
    // 如果当前任务是任务0，则死机。
    if (!p)
        return;
    if (current == &(init_task.task))
        panic("task[0] trying to sleep");
    // 让 tmp 指向已经在等待队列上的任务(如果有的话)，例如 inode->i_wait。
    // 并且将睡眠队列头的等待指针指向当前任务。这样就把当前任务插入到了 *p 的等待队列中。
    // 然后将当前任务置为可中断的等待状态，并执行重新调度。
    tmp = *p;
    *p = current;
repeat: current->state = TASK_INTERRUPTIBLE;
    schedule();
    // 只有当这个等待任务被唤醒时，程序才又会回到这里，标志进程已被明确的唤醒执行。
    // 如果等待队列中还有等待任务，并且队列头指针所指向的任务不是当前任务时，
    // 则将该等待任务置为可运行的就绪状态，并重新执行调度程序。
    // 当指针 *p 所指向的不是当前任务时，表示在当前任务被被放入队列后，又有新的任务被插入等待队列前部。因此我们先唤醒他们，而让自己仍然等等。
    // 等待这些后续进入队列的任务被唤醒执行时来唤醒本任务。于是去执行重新调度。
    if (*p && *p != current) {
        (**p).state = TASK_RUNNING;
        goto repeat;
    }
    // 下一句代码有误：应该是 *p = tmp, 让队列头指针指向其余等待任务，否则在当前任务之前插入
    // 等待队列的任务均被抹掉了。当然同时也需要删除下面行数中同样的语句
    *p = tmp;
    if (tmp) {
        tmp->state = TASK_RUNNING;
    }
}

// pause() 系统调用，转换当前任务状态为可中断的等待状态，并重新调度
// 将导致进程进入睡眠状态，直到收到一个信号。该信号用于终止进程或使进程调用一个信号捕获函数。
// 只有当捕获一个信号，并且信号捕获处理函数返回时，pause() 才返回。此时pause()返回值应该是-1，并且errno被置为EINTR
// 这里还没有完全实现直到(直到0.95版)
// 可中断等待状态与不可中断等待状态它是有所区别的，将进程设置成可中断等待状态意味着，
// 如果产生某种中断，或其他进程给这个进程发送特定信号等，仍然有可能将这个进程的状态改设为就绪状态，
// 使之仍然具备运行的能力。这种意义，适用于Linux0.11中的全部进程。
// 不可中断等待状态: 只有内核代码中明确表示将该进程设置为就绪状态，它才能被唤醒。
// 除此之外，没有任何办法将其唤醒。
int sys_pause(void) {
    current->state = TASK_INTERRUPTIBLE;
    schedule();
    return 0;
}

// 系统调用功能 - 设置报警定时时间值(秒)
// 如果参数seconds大于0，则设置新定时值，并返回原定时时刻还剩余的间隔时间。否则
// 返回0.进程数据结构中报警定时值 alarm 的单位是系统滴答(1滴答为10ms),
// 它是系统开机起到设置定时操作时系统滴答值jiffies和转换成滴答单位的定时值之和，即'jiffies + HZ*定时秒值'。
// 而参数给出的是以秒为单位的定时值，因此本函数的主要操作是进行两种单位的转换。
// 其中常数 HZ = 100，是内核系统运行频率。seconds是新的定时时间值，单位：秒。
int sys_alarm(long seconds) {
    int old = current->alarm;
    if (old) {
        old = (old - jiffies) / HZ;
    }
    current->alarm = (seconds > 0) ? (jiffies + HZ * seconds) : 0;
    return old;
}

// 时钟中断处理函数
// 在 system_call.s 中被调用
// cpl 是当前特权级别 0 或 3, 是时钟中断发生时正被执行的代码选择符中的特权级
// 对于一个进程由于执行时间片用完时，则进行任务切换，并执行一个计时更新工作
int counter = 0;
long volatile jiffies = 0;
void do_timer(long cpl) {
    // counter++;
    // if(counter == 10){
    //     printk("CPL = %d Jiffies = %d\n", cpl, jiffies);
    //     counter = 0;
    // }
    if (!cpl) {
        current->stime++;   // 系统运行时间
    } else {
        current->utime++;   // 用户运行时间
    }
    if ((--current->counter) > 0) return;   // 如果进程运行时间还没完，则退出。
    current->counter = 0;
    if(!cpl) return;                        // 内核程序，不依赖 counter 进行调度
    schedule();                             // 执行调度
}

// 内核调度程序的初始化子程序
void sched_init() {
    int divisor = 1193180/HZ;

    int i;
    struct desc_struct *p;  // 描述符表结构指针

    // 把任务状态描述符表和局部数据描述符表挂接到全局描述符表GDT中
    set_tss_desc(gdt+FIRST_TSS_ENTRY, &(init_task.task.tss));
    set_ldt_desc(gdt+FIRST_LDT_ENTRY, &(init_task.task.ldt));

    // 清任务数组和描述符表项(注意 i=1 开始，所以初始任务的描述符还在)
    p = gdt + 2 + FIRST_TSS_ENTRY; // 跳过 init_task
    for(i = 1; i < NR_TASKS; i++) {
        task[i] = NULL;
        p->a = p->b = 0;
        p++;
        p->a = p->b = 0;
        p++;
    }

    // 清除标志寄存器中的位 NT，NT 标志用于控制程序的递归调用(Nested Task)
    // 当NT置位时，那么当前中断任务执行 iret 指令时就会引起任务切换
    // NT 指出 TSS 中的 back_link 字段是否有效
    __asm__("pushfl; andl $0xffffbfff, (%esp); popfl");
    ltr(0);
    lldt(0);

    // 初始化8253定时器。通道0，选择工作方式3，二进制计数方式。
    // 通道0的输出引脚接在中断控制主芯片的IRQ0上，它每10毫秒发出一个IRQ0请求。
    // LATCH是初始定时计数值。
    outb_p(0x36, 0x43);                     /* binary, mode 3, LSB/MSB, ch 0 */
    outb_p(divisor & 0xFF, 0x40);           /* LSB */
    outb_p(divisor >> 8, 0x40);             /* MSB */

    // 设置时钟中断处理程序句柄(设置时钟中断门)。修改中断控制器屏蔽码，允许时钟中断。
    // 然后设置系统调用中断门。这两个设置中断描述符表 IDT 中描述符在宏定义在文件 include/asm/system.h中
    // timer interrupt gate setup: INT 0x20
    set_intr_gate(0x20, &timer_interrupt);
    // Make 8259 accept timer interrupt
    outb(inb_p(0x21)&~0x01, 0x21);
    // system_call
    set_system_gate(0x80, &system_call);
}
