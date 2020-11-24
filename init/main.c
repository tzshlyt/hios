
#define __LIBRARY__

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <asm/system.h>
#include <asm/io.h>
// Use to debug serial
#include <serial_debug.h>

extern void trap_init(void);
extern void video_init(void);
extern void sched_init(void);
extern void mem_init(unsigned long start_mem, unsigned long end_mem);

static inline int fork(void) __attribute__((always_inline));
static inline int pause(void) __attribute__((always_inline));
static inline int sys_debug(char *str) __attribute__((always_inline));

static inline int fork(void) {
    long __res;
    __asm__ volatile("int $0x80\n\t"
            :"=a" (__res)
            :"0" (__NR_fork));
    if( __res >= 0)
        return (int) __res;
    return -1;
}

static inline int sys_debug(char *str) {
    long __res;
    __asm__ volatile("int $0x80\n\t"
            :"=a" (__res)
            :"0" (__NR_sys_debug), "b" ((long)(str)));
    if (__res >= 0)
        return (int) __res;
    return -1;
}

static inline int pause(void) {
    long __res;
    __asm__ volatile("int $0x80\n\t"
            :"=a" (__res)
            :"0" (__NR_pause));
    if( __res >= 0)
        return (int) __res;
    return -1;
}

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

typedef unsigned long size_t;
int snprintf(char *str, size_t size, const char *fmt, ...);

int main() {
    int ret;
    char str[1000] = "";
    video_init();
    trap_init();
    sched_init();
    printk("Welcome to Linux0.1 Kernel Mode(NO)\n");

    // 初始化物理页内存, 将 1MB - 16MB 地址空间的内存进行初始
    mem_init(0x100000, 0x1000000);

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
        while(1)
            sys_debug("User: Hello\n");
    }
    while(1)
        sys_debug("Kernel Hello\n");
}

void init() {
    while(1);
}