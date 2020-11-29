/**
 * 在 asm.s 中保存了一些状态后，本程序用来处理异常和故障
 */

#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/system.h>


// 以下定义了一些中断处理程序原型，用于在函数trap_init()中设置相应中断门描述符
// 在kernel/asm.s中用到的函数原型
void divide_error(void);
void debug(void);
void nmi(void);
void int3(void);
void overflow(void);
void bounds(void);
void invalid_op(void);
void device_not_available(void);
void double_fault(void);
void coprocessor_segment_overrun(void);
void invalid_TSS(void);
void segment_not_present(void);
void stack_segment(void);
void general_protection(void);
void page_fault(void);                          // mm/page.s
void coprocessor_error(void);
void irq13(void);
void reserved(void);
void parallel_interrupt(void);


// 该子程序用来打印出错中断的名称、出错号、调用程序的EIP、EFLAGS、ESP、fs段寄存器值、
// 段的基址、段的长度、进程号PID、任务号、10字节指令码。
// 如果堆栈在用户数据段，则还打印16字节的堆栈内容

// 用来引发一个异常，因为目前我们没有实现信号机制，
// 且为单进程，所以die的处理就是打印完必要的信息
// 停机进入死循环
static void die(char *str, long esp_ptr, long nr) {
    long *esp = (long *)esp_ptr;
    printk("%s: %x\n", str, nr & 0xffff);

    // esp[1] -> 段选择符cs，esp[0] -> eip
    // esp[2] -> eflags
    // esp[4] -> 原ss，esp[3] -> 原esp
    printk("EIP: 0x%x:0x%x\n EFLAGS: 0x%x\n ESP 0x%x:0x%x\n",
            esp[1], esp[0], esp[2], esp[4], esp[3]);

    // Some Process Related code, now stub
    printk("base 0x%x, limit 0x%x\n", get_base(current->ldt[1]), get_limit(0x17));
    printk("No Process now, System HALT!   :(\n");
    for(;;);
    return ;
}

void do_double_fault(long esp, long error_code) {
    die("double fault", esp, error_code);
}

void do_general_protection(long esp, long error_code) {
    die("general protection", esp, error_code);
}

void do_divide_error(long esp, long error_code) {
    die("divide error", esp, error_code);
}

// 参数是进入中断后被顺序压入栈的寄存器值
void do_int3(long *esp, long error_code,
        long fs, long es, long ds,
        long ebp, long esi, long edi,
        long edx, long ecx, long ebx, long eax) {

    // Now we do not support Task Register
    int tr = 0;
    __asm__ volatile("str %%ax":"=a" (tr):"0" (0));       // 取出任务寄存器值 -> tr

    printk("eax\tebx\tecx\tedx\t\n%x\t%x\t%x\t%x\n",eax, ebx, ecx, edx);
    printk("esi\tedi\tebp\tesp\t\n%x\t%x\t%x\t%x\n",esi, edi, ebp, (long)esp);
    printk("ds\tes\tfs\ttr\n%x\t%x\t%x\t%x\n",ds, es, fs, tr);
    printk("EIP: %x    CS:%x     EFLAGS: %x", esp[0], esp[1], esp[2]);
    printk("errno = %d", error_code);
    return ;
}

void do_nmi(long esp, long error_code) {
    die("nmi", esp, error_code);
}

void do_debug(long esp, long error_code) {
    die("debug", esp, error_code);
}

void do_overflow(long esp, long error_code) {
    die("overflow", esp, error_code);
}

void do_bounds(long esp, long error_code) {
    die("bounds", esp, error_code);
}

void do_invalid_op(long esp, long error_code) {
    die("invalid operand", esp, error_code);
}

void do_device_not_available(long esp, long error_code) {
    die("device not available", esp, error_code);
}

void do_coprocessor_segment_overrun(long esp, long error_code) {
    die("coprocessor segment overrun", esp, error_code);
}

void do_invalid_TSS(long esp, long error_code) {
    die("invalid TSS", esp, error_code);
}

void do_segment_not_present(long esp, long error_code) {
    die("segment not present", esp, error_code);
}

void do_stack_segment(long esp, long error_code) {
    die("stack segment", esp, error_code);
}

void do_coprocessor_error(long esp, long error_code) {
    die("coprocessor error", esp, error_code);
}

void do_reserved(long esp, long error_code) {
    die("reserved(15, 17-47)error", esp, error_code);
}

void do_stub(long esp, long error_code) {
    printk("stub interrupt! %x, %x\n", esp, error_code);
}

void trap_init(void) {
    int i;

    set_trap_gate(0, &divide_error);
    set_trap_gate(1, &debug);
    set_trap_gate(2, &nmi);
    set_system_gate(3, &int3);
    set_system_gate(4, &overflow);
    set_system_gate(5, &bounds);
    set_trap_gate(6, &double_fault);
    set_trap_gate(7, &device_not_available);
    set_trap_gate(8, &double_fault);
    set_trap_gate(9, &coprocessor_segment_overrun);
    set_trap_gate(10, &invalid_TSS);
    set_trap_gate(11, &segment_not_present);
    set_trap_gate(12, &stack_segment);
    set_trap_gate(13, &general_protection);
    set_trap_gate(14, &page_fault);
    set_trap_gate(15, &reserved);
    set_trap_gate(16, &coprocessor_error);
    // 下面把int17-47的陷阱门先均设置为 reserved ,以后各硬件初始化时会重新设置自己的陷阱门。
    for(i = 17; i < 48; i++) {
        set_trap_gate(i, &reserved);
    }
    // 设置协处理器中断0x2d(45)陷阱门描述符，并允许其产生中断请求。设置并行口中断描述符
    set_trap_gate(45, &irq13);
    outb_p(0x21, inb_p(0x21)&0xfb);  // 允许8259A主芯片的IRQ2中断请求。
	outb(0xA1, inb_p(0xA1)&0xdf);    // 允许8259A从芯片的IRQ3中断请求。
    set_trap_gate(39, &parallel_interrupt); // 设置并行口1的中断0x27陷阱门的描述符
}

