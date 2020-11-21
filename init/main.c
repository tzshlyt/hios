
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
#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" /* 保存堆栈指针esp到eax寄存器中 */\
	"pushl $0x17\n\t" /* 将堆栈段选择符(SS)入栈 */\
	"pushl %%eax\n\t" /* 保存堆栈指针值(esp)入栈 */\
	"pushfl\n\t" /* 将标志寄存器(eflags)内容入栈 */\
	"pushl $0x0f\n\t" /* 将Task0 代码段选择符(cs)入栈 */\
	"pushl $1f\n\t" /* 将下标号1的偏移地址(eip)入栈 */\
	"iret\n" /* 中断返回，跳转到下标号1处 */ \
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

    // move_to_user_mode();
    sys_debug("User mode can print string use this syscall");

    // // fork still not work
    // if(!fork()) {
    //     while(1) sys_debug("User: Hello\n");
    // }
    // while(1) sys_debug("Kernel Hello\n");
    // pause();
}

void init() {
    while(1);
}
