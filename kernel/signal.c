#include <linux/kernel.h>
#include <signal.h>
#include <serial_debug.h>

// 系统调用的中断处理程序中真正的信号预处理程序。
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
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
    s_printk("stubbed signal hanlder signal:%d\n", signr);
}