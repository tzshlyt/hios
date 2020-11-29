/**
 * 调度程序头文件
 */


#ifndef _SCHED_H
#define _SCHED_H

// 最多有 64 个进程（任务）同时处于系统中
#define NR_TASKS 64
// 时钟频率 100 hz
#define HZ 100

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

#include<linux/mm.h>
#include<linux/head.h>
#include<signal.h>

// 定义任务状态
// 可中断等待状态: 如果产生某种中断，或其他进程给这个进程发送特定信号等，仍然有可能将这个进程的状态改设为就绪状态，
// 使之仍然具备运行的能力。这种意义，适用于Linux0.11中的全部进程。
// 不可中断等待状态: 只有内核代码中明确表示将该进程设置为就绪状态，它才能被唤醒。除此之外，没有任何办法将其唤醒。
#define TASK_RUNNING 0              // 正在运行或已准备就绪
#define TASK_INTERRUPTIBLE 1        // 可中断等待状态
#define TASK_UNINTERRUPTIBLE 2      // 不可中断等待状态，主要用于I/O操作
#define TASK_ZOMBIE 3               // 僵死状态，已经停止运行，但父进程还没发信号
#define TASK_STOPPED 4              // 已停止

#ifndef NULL
#define NULL ((void *)(0))
#endif

extern int copy_page_tables(unsigned long from, unsigned long to, unsigned long size);
extern int free_page_tables(unsigned long from, unsigned long size);
extern void schedule(void);

typedef int (*fn_ptr)();

// 数学协处理器使用的结构，主要用于保存进程切换时i387的执行状态信息
struct i387_struct {
    long cwd;       // 控制字(Control)
    long swd;       // 状态字(State)
    long twd;       // 标记字(Tag)
    long fip;       // 协处理器代码指针IP
    long fcs;       // 协处理器代码段寄存器CS
    long foo;       // 内存offset
    long fos;       // 内存段
    long st_space[20];      // 8个10字节的协处理器累加器
};

// 任务状态段数据结构，注意变量顺序，参考Intel手册
struct tss_struct {
    long back_link;                     /* 16 high bits zero */
    long esp0;
    long ss0;                           /* 16 high bits zero */
    long esp1;
    long ss1;                           /* 16 high bits zero */
    long esp2;
    long ss2;                           /* 16 high bits zero */
    long cr3;
    long eip;
    long eflags;
    long eax, ecx, edx, ebx;
    long esp;
    long ebp;
    long esi;
    long edi;
    long es;                            /* 16 high bits zero */
    long cs;                            /* 16 high bits zero */
    long ss;                            /* 16 high bits zero */
    long ds;                            /* 16 high bits zero */
    long fs;                            /* 16 high bits zero */
    long gs;                            /* 16 high bits zero */
    unsigned long ldt;                  /* 16 high bits zero */
    unsigned long trace_bitmap;         /* 16 high bits zero */
    struct i387_struct i387;            /* 16 high bits zero */
};

// 进程描述符
struct task_struct {
	// --- 硬编码部分，下面的不应该修改 ---
    long state;                         // 运行状态 -1 不可运行，0 可运行（就绪）, >0 已停止
    long counter;                       // 运行时间计数（递减），运行时间片
    long priority;                      // 优先级，开始运行时 counter = priority，越大运行越长

    long signal;                        // 信号，是位图，每个比特代表一种信号，信号值=位偏移值+1
    struct sigaction sigaction[32];     // 信号执行属性结构，对应信号将要执行的操作和标志信息
    unsigned long blocked;              // 进程信号屏蔽码（对应信号位图）
	// --- 硬编码部分结束 ---
    int exit_code;                      // 退出码，其父进程会取
    unsigned long start_code;           // 代码段地址
    unsigned long end_code;             // 代码长度（字节数）
    unsigned long end_data;             // 代码长度 + 数据长度（字节数）
    unsigned long brk;                  // 总长度
    unsigned long start_stack;          // 堆栈段地址

    long pid;                           // 进程号
    long father;                        // 父进程号
    long pgrp;                          // 进程组号
    long session;                       // 会话(session)ID
    long leader;                        // 会话(session)的首领

    unsigned short uid;                 // 用户id
    unsigned short euid;                // 有效用户id
    unsigned short suid;                // 保存的用户id
    unsigned short gid;                 // 组id
    unsigned short egid;                // 有效组id
    unsigned short sgid;                // 保存组id

    long alarm;                         // 报警定时值（滴答数）单位：jiffies
    long utime;                         // 用户态运行时间（滴答数）
    long stime;                         // 内核态运行时间（滴答数）
    long cutime;                        // 子进程用户态运行时间
    long cstime;                        // 子进程内核态运行时间
    long start_time;                    // 进程开始运行时刻

    unsigned short used_math;           // 标志：是否使用了协处理器

    int tty;                            // 进程使用 tty 的子设备号。-1 表示没有使用
    // 下面是和文件系统相关的变量，暂时不使用，先注释
    //unsigned short umask;               // 文件创建属性屏蔽位
    //struct m_inode * pwd;               // 当前工作目录i节点结构
    //struct m_inode * root;              // 根目录i节点结构
    //struct m_inode * executable;        // 执行文件i节点结构
    //unsigned long close_on_exec;        // 执行时关闭文件句柄位图标志。参见 include/fcntl.h
    //struct file * filp[NR_OPEN];        // 进程使用的文件表结构
    struct desc_struct ldt[3];          // 本任务的局部表描述符。0 空，1 代码段cs，2 数据段和堆栈段 ds&ss
    struct tss_struct tss;               // 本进程的任务状态段信息结构
};

// 设置第1个任务表
// 基址Base = 0, 段长limit = 0x9ffff（640KB）
// 因为 G 设置 1, limit = 0x9ffff / 4KB = 0x9ffff >> 12 = 0x9f
#define INIT_TASK \
/* state info */ {0, 15, 15, \
/* signals */    0, {{}, }, 0, \
/* exit_code, brk */    0, 0, 0, 0, 0, 0, \
/* pid */    0, -1, 0, 0, 0, \
/* uid */    0, 0, 0, 0, 0, 0, \
/* alarm, etc... */   0, 0, 0, 0, 0, 0, \
/* math */    0, \
/* tty */   -1, \
/* LDT */    { \
        {0, 0},\
        {0x9f, 0xc0fa00},  /* 代码长640k, 基地址 0x0, G=1, D=1, DPL=3, P=1 TYPE=0x0a */ \
        {0x9f, 0xc0f200},  /* 数据长640k, 基地址 0x0, G=1, D=1, DPL=3, P=1 TYPE=0x0a */ \
    }, \
/* TSS */ {0, PAGE_SIZE+(long)&init_task, 0x10, 0, 0, 0, 0, (long)&pg_dir, \
        0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, \
        _LDT(0), 0x80000000, \
        {} \
    } \
}

extern struct task_struct *task[NR_TASKS];          // 任务指针数组
extern struct task_struct *last_task_used_math;     // 上一个使用过协处理器的进程
extern struct task_struct *current;                 // 当前进程
extern long volatile jiffies;                       // 开机开始算起的滴答数（10ms/滴答）
extern long start_time;                             // 开机时间。从1970 开始计时的秒数

#define CURRENT_TIME (start_time + jiffies / HZ)     // 当前时间（秒数）

extern void add_timer(long *jiffies, void(*fn)(void));          // 添加定时器，kernel/sched.c
extern void sleep_on(struct task_struct **p);                   // 不可中断的等待睡眠，kernel/sched.c
extern void interruptible_sleep_on(struct task_struct **p);     // 可中断的等待睡眠
extern void wake_up(struct task_struct **p);                    // 明确唤醒睡眠的进程
extern void show_task_info(struct task_struct *task);

/*
 * 在GDT表中寻找第1个TSS的入口。0 没有用nul，1 代码段cs，2 数据段ds，3 系统调用syscall
 * 4 任务状态段TSS0，5 局部表LTD0，6 任务状态段TSS1，等
 */
// 全局表中第1个任务状态段（TSS）描述符的选择符索引号
#define FIRST_TSS_ENTRY 4
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)
// 计算出TSS，LDT在 GDT 中的选择符值 (偏移量，即第几个字节)
// _TSS(n)表示第n个TSS，计算方式为，第一个 TSS 的入口为 FIRST_TSS_ENTRY << 3(因为每一个Entry占8字节, 所以第4个的偏移量为4 << 3 = (4 * 8))
// 因为每个任务使用 1 个 TSS 和 1 个 LDT 描述符，共占用 16 字节，因此需要 n << 4 来表示对应 TSS 起始位置
#define _TSS(n) ((((unsigned long) n) << 4) + (FIRST_TSS_ENTRY << 3))
#define _LDT(n) ((((unsigned long) n) << 4) + (FIRST_LDT_ENTRY << 3))
#define ltr(n) __asm__ volatile("ltr %%ax"::"a" (_TSS(n)))
#define lldt(n) __asm__ volatile("lldt %%ax"::"a" (_LDT(n)))

// 取出当前的任务号，是任务数组中的索引值，和进程号 pid 不同
#define str(n) \
__asm__ volatile("str %%ax\n\t" /* 将任务寄存器中TSS段的选择符复制到ax中 */ \
        "subl %2, %%eax\n\t" /* eax - FIRST_TSS_ENTRY*8 -> eax */ \
        "shrl $4, %%eax\n\t" /* eax/16 -> eax = 当前任务号 */ \
        : "=a" (n) \
        : "a" (0), "i" (FIRST_TSS_ENTRY << 3))

// 将切换当前任务到任务nr，即n。首先检测任务n不是当前任务，如果是则什么都不做退出。
// 如果切换到的任务上次运行使用过数学协处理器， 则需要复位控制寄存器 cr0 中的TS标志
// 跳转到一个任务的 TSS 段选择符组成的地址会造成 cpu 进行任务切换操作
// __tmp 临时数据结构，用于组建远跳转指令操作数，
// 内存间跳转指令使用6字节操作数作为跳转目的地的长指针
// 其格式为：jmp 16位段选择符: 32位偏移值。但在内存中操作数的表示顺序与这里正好相反。
// __tmp.a - 32位偏移地址
// __tmp.b - 低16位新TSS段的选择符
// 输入: %0 - 指向 __tmp
//       %1 - 指向 __tmp.b 处，用于存放新 TSS 的选择符
//       dx - 新任务n的TSS段选择符
//       ecx - 新任务n的任务结构指针task[n]

#define switch_to(n) {\
struct {long a, b;} __tmp; \
__asm__ volatile("cmpl %%ecx, current\n\t" /* 任务n是当前任务吗？（current == task[n]?）*/ \
        "je 1f\n\t" /* 是，则什么都不做，退出*/ \
        "movw %%dx, %1\n\t" /* 将新任务TSS的16位选择符存入__tmp.b中 */ \
        "xchgl %%ecx, current\n\t" /* current = task[n]; ecx = 被切换出的任务 */ \
        "ljmp *%0\n\t" /* 长跳跃到新的TSS选择符 *&__temp，造成任务切换 */ \
        "cmpl %%ecx, last_task_used_math\n\t" /* 原任务上次使用过协处理器吗 */ \
        "jne 1f\n\t" /* 没有则跳转，退出 */ \
        "clts\n\t" /* 使用过，则清cr0中的任务切换标志TS */ \
        "1:" \
        ::"m" (*&__tmp.a), "m" (*&__tmp.b), \
        "d" (_TSS(n)), "c" ((long) task[n])); \
}

// 设置位于addr处描述符中各基地址字段（基地址是base）
// %0 - 地址addr偏移2
// %1 - 地址addr偏移4
// %2 - 地址addr偏移7
// edx - 基地址base
#define _set_base(addr,base)  \
__asm__ volatile(/* "push %%edx\n\t" */ \
	"movw %%dx,%0\n\t" /* 基地址低16位（15-0） -> [addr+2] */ \
	"rorl $16,%%edx\n\t" /* edx高16位（31-16） -> dx */ \
    "movb %%dl,%1\n\t" /* 基地址高16位中的低8位（23-16）-> [addr+4] */ \
	"movb %%dh,%2\n\t" /* 基地址高16位中的高8位（31-24）-> [addr+7] */ \
	/* "pop %%edx" */ \
	::"m" (*((addr)+2)), \
    "m" (*((addr)+4)), \
    "m" (*((addr)+7)), \
    "d" (base) \
    /*:"dx" 告诉gcc编译其edx寄存器中的值被改变 */ \
	)

// 设置位于地址addr处描述符中的段限长字段（段长是limit）
// %0 - 地址addr
// %1 - 地址addr偏移6处
// edx - 段长值limit
#define _set_limit(addr,limit) \
__asm__ volatile(/* "push %%edx\n\t" */ \
	"movw %%dx,%0\n\t" /* 段长limit低16位（15-0）-> [addr] */ \
	"rorl $16,%%edx\n\t" /* edx 中的段长高4位（19-16）-> dl */ \
	"movb %1,%%dh\n\t" /* 取原[addr+6]字节 -> dh, 其中高4位是些标志 */ \
	"andb $0xf0,%%dh\n\t" /* 清dh的低4位（将存放段长位19-16） */ \
	"orb %%dh,%%dl\n\t" /* 将原高4位标志和段长的高4位（19-16）合成1字节并放回[addr+6]处 */ \
	"movb %%dl,%1" \
	/* "pop %%edx" */ \
	::"m" (*(addr)), \
    "m" (*((addr)+6)), \
    "d" (limit) \
	)

// 设置局部描述符表中ldt描述符的基地址字段
#define set_base(ldt, base) _set_base(((char *)&(ldt)), (base))
// 设置局部描述符表中ldt描述符的段长字段，limit >> 12 是因为当Descriptor中G位置位的时候，Limit单位是4KB
#define set_limit(ldt, limit) _set_limit(((char *)&(ldt)), (limit - 1) >> 12)


// 从地址addr处描述符中取段基地址。功能与_set_base() 正好相反
// edx - 存放基地址(_base)
// %1 - 地址addr偏移2
// %2 - 地址addr偏移4
// %3 - 地址addr偏移7
/**
#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \
	"movb %2,%%dl\n\t" \
	"shll $16,%%edx\n\t" \
	"movw %1,%%dx" \
	:"=d" (__base) \
	:"m" (*((addr)+2)), \
	"m" (*((addr)+4)), \
	"m" (*((addr)+7)) \
        :"memory"); \
__base;})
**/

static inline unsigned long _get_base(char *addr) {
    unsigned long __base;
    __asm__ volatile("movb %3, %%dh\n\t"
            "movb %2, %%dl\n\t"
            "shll $16, %%edx\n\t"
            "movw %1, %%dx\n\t"
            :"=&d" (__base)
            :"m" (*((addr) + 2)),
            "m" (*((addr) + 4)),
            "m" (*((addr) + 7)));
    return __base;
}

// 取局部描述符表中 ldt 所指段描述符中的基地址
#define get_base(ldt) _get_base( ((char *)&(ldt)) )
// 取段选择符segment指定的描述符中的限长值
// 指令 lsl 是 Load Segment Limit 缩写。它从指定段描述符中取出分散的限长比特位拼成完整的段限长值放入指定寄存器中
// 所得限长是实际字节数减1，因此还需要加1后才返回
#define get_limit(segment) ({\
    unsigned long __limit; \
    __asm__ volatile("lsll %1, %0\n\tincl %0":"=r"(__limit):"r"(segment)); \
    __limit; \
    })


#endif
