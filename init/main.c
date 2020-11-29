
#define __LIBRARY__

#include <linux/kernel.h>
#include <linux/sched.h>
#include <unistd.h>
#include <asm/system.h>
#include <asm/io.h>
// Use to debug serial
#include <serial_debug.h>

extern void trap_init(void);
extern void video_init(void);
extern void sched_init(void);
extern void mem_init(unsigned long start_mem, unsigned long end_mem);
void init(void);

static inline int fork(void) __attribute__((always_inline));
static inline int pause(void) __attribute__((always_inline));
static inline int sys_debug(char *str) __attribute__((always_inline));
static inline _syscall0(int, fork)
static inline _syscall0(int, pause)
static inline _syscall1(int, sys_debug, char *, str)

// 移动到用户模式
// 所使用的方法是模拟中断调用返回过程，即利用 iret 指令来实现特权级的变更和堆栈的切换
// 压栈顺序与通常中断时硬件的压栈动作一样
// 这里的 iret 并不会造成 cpu 去执行任务切换操作，因为在执行这个函数之前，标志位 NT 硬件在 sched_init() 中被复位了
/*
                   <-------- SP0-(TSS中的SS:ESP)
        原ss
        原esp
        原eflags
        原cs
        原eip
                    <------ SP1-iret指令执行前新的 SS:ESP
 */
//
#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" /* 保存堆栈指针esp到eax寄存器中 */\
	"pushl $0x17\n\t" /* 将堆栈段选择符(SS)入栈, (0x10111) 用户级别，从 LDT 中获得描述符, 从第3项(index=2)中获取描述符*/\
	"pushl %%eax\n\t" /* 保存堆栈指针值(esp)入栈 */\
	"pushfl\n\t" /* 将标志寄存器(eflags)内容入栈 */\
	"pushl $0x0f\n\t" /* 将Task0 代码段选择符(cs)入栈 (0x1111) 用户级别, 从 LDT 中获得描述符，从第2项中获取描述符 */\
	"pushl $1f\n\t" /* 将下标号1的偏移地址(eip)入栈 */\
	"iret\n" /* 中断返回, 引起系统移到任务0去执行，跳转到下标号1处, 把压栈的这些值恢复给后面执行的程序特权级就转为用户特权级 */ \
	"1:\t movl $0x17, %%eax\n\t" /* 此处开始执行任务0 */\
	"movw %%ax, %%ds\n\t" /* 初始化段寄存器指向本局部表数据段 */\
	"movw %%ax, %%es\n\t" \
	"movw %%ax, %%fs\n\t" \
	"movw %%ax, %%gs" \
	:::"ax");

int mmtest_main(void);
void signal_demo_main(void);
void sched_abcd_demo(void);

int main() {
    video_init();
    trap_init();
    sched_init();
    // printk("Welcome to Linux0.1 Kernel Mode(NO)\n");

    // 初始化物理页内存, 将 1MB - 16MB 地址空间的内存进行初始
    mem_init(0x100000, 0x300000);

    // 中断实验
    sti(); // 开启中断
	// asm ("int $3");
    // int k = 3/0;

    // 内存实验
    // mmtest_main();

    // 在Linux 0.11中，除进程0外，所有进程都是由一个已有进程在用户态下完成创建的。
    // 为了遵守这个规则，在进程0正式创建进程1之前，要将进程0由内核态转变为用户态，
    // 方法是调用move_to_user_mode函数，模仿中断返回动作，实现进程0的特权级从内核态转变为用户态。
    move_to_user_mode();
    sys_debug("User mode can print string use this syscall\n");

    // fork still not work
    if(!fork()) {   // fork() 返回1(子进程pid), !1为假，所以进程0继续执行 else 的代码
        // 进程 1
        if(!fork()) {
            // 进程 2
            sched_abcd_demo();
        } else {
            signal_demo_main();
        }
        while(1);
    }

    // 在Linux0.11的进程调度机制中，有两种情况可以产生进程切换。
    // 一种情况是:由于进程运行的时间到了，于是进行切换。每个进程在创建时，都被赋予有限的时间片，
    // 以此保证所有进程每次都只执行有限的时间。一旦当前进程的时间片被削减为0了，就说明这个进程此次执行的时间用完了，
    // 只要它是处于用户态，就立即切换到其他进程去执行，以此来保证多进程能够轮流执行。
    // 另一种情况是:由于逻辑执行需要被打断了，于是进程切换。当一个进程处于内核态时，它下一步确实已经没有事务要处理了，
    // 或者它下一步要处理的事务需要外设提供的数据来支持，等等，在这种情况下，该进程就不再具备进一步执行的“逻辑条件”了，
    // 如果还等着时钟中断产生后再切换到别的进程去执行，就是在无谓地浪费时间，于是它将被立即切换到其他进程去执行。
    // 进程 0 切换到进程 1 表现为第二种情况
    // pause系统调用会把任务 0 转换成可中断等待状态，再执行调度函数, 就行进程切换。但是调度函数只要发现系统中
    // 没有其他任务可以运行是就会切换到任务0，而不依赖于任务 0 的状态。
    for(;;) pause();
}

void init() {
    // pid = 1
    // 为什么不在进程0和进程1中打印，因为schedule()跳过进程0
    if(!fork()) {
        while(1)
            sys_debug("A");
    } else {
        while(1)
            sys_debug("B");
    }
}

void sched_abcd_demo() {
    // Here init process (pid = 2) will
    // print AABB randomly
    int pid, i;

    if(fork()) {
        while(1);
            // sys_debug("A\n");
    }
    if(!fork()) {
        while(1);
            // sys_debug("B\n");
    }
    if(!(pid = fork())) {
        sys_debug("exit befor\n");
        _exit(1);
        sys_debug("exit after\n");
        while(1);
            // sys_debug("C\n");
    }
    if (pid > 0) {
        sys_debug("wait pid\n");
        while (pid != wait(&i));
    }
    if(!fork()) {
        while(1);
            // sys_debug("D\n");
    }
    while(1);
}
