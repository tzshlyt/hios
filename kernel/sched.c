/*
 * 内核进程调度管理
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>
#include <asm/io.h>

extern int timer_interrupt(void);

// 每个进程在内核状态运行时都有自己的内核态堆栈
union task_union {
    struct task_struct task;    // 因为一个任务的数据结构与其内核态堆栈在同一内存页中，所以从堆栈段寄存器ss可以获得其数据段选择符
    char stack[PAGE_SIZE];
};
static union task_union init_task = {INIT_TASK,};   // 定义初始任务的数据 sched.h

long volatile jiffies = 0;
long startup_time = 0;                                      // 开机时间，从1970开始计时的秒数
struct task_struct *current = &(init_task.task);            // 当前任务指针（初始化指针任务0）
struct task_struct *last_task_used_math = NULL;             // 处理过协处理任务的指针
struct task_struct *task[NR_TASKS] = {&(init_task.task),};  // 定义任务指针数组

// 定义用户堆栈，共1K项，容量4K字节
// 在内核初始化操作中被用作内核栈
// 初始化完成后被用作任务0的用户态堆栈
// 在运行任务0之前是内核栈，以后用作任务0和1的用户栈
// 下面结构用于设置堆栈ss:esp, 见head.s
// ss -> 内核数据段选择符0x10
// esp -> user_stack数组最后一项后面，因为栈是递减的
long user_stack[PAGE_SIZE >> 2];
struct {
    long *a;
    short b;
} stack_start = {&user_stack[PAGE_SIZE >> 2], 0x10};

void sleep_on(struct task_struct **p) {

}

void schedule(void) {

}

void wake_up(struct task_struct **p) {

}

void interruptible_sleep_on(struct task_struct **p) {

}

int sys_pause(void) {

}

// 时钟中断处理函数
// 在 system_call.s 中被调用
// cpl 是当前特权级别 0 或 3, 是时钟中断发生时正被执行的代码选择符中的特权级
// 对于一个进程由于执行时间片用完时，则进行任务切换，并执行一个计时更新工作
int counter = 0;
void do_timer(long cpl) {
    counter++;
    if(counter == 10){
        printk("CPL = %d Jiffies = %d\n", cpl, jiffies);
        counter = 0;
    }
}

void timer_interrupt(void);

// 这是一个临时函数，用于初始化8253计时器
// 并开启时钟中断
void sched_init() {
    int divisor = 1193180/HZ;
    outb_p(0x36, 0x43);
    outb_p(divisor & 0xFF, 0x40);
    outb_p(divisor >> 8, 0x40);

    // timer interrupt gate setup: INT 0x20
    set_intr_gate(0x20, &timer_interrupt);
    // Make 8259 accept timer interrupt
    outb(inb_p(0x21) & ~0x01, 0x21);
}
