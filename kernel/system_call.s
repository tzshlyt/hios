/**
 * 实现系统调用过程
 * 使用中断调用 int 0x80 和放在 eax 中的功能号来使用内核提供各种功能服务
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */

.global timer_interrupt

# 定义栈的布局
EAX = 0x00
EBX = 0x04
ECX = 0x08
EDX = 0x0C
FS = 0x10
ES = 0x14
DS = 0x18
EIP = 0x1C
CS = 0x20
EFLAGS = 0x24
OLDESP = 0x28
OLDSS = 0x2C

ret_from_syscall:
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret

timer_interrupt:
	push %ds
	push %es
	push %fs
	push %edx
	push %ecx
	push %ebx
	push %eax
	movl $0x10, %eax		# ds，es指向内核数据段
	mov %ax, %ds
	mov %ax, %es
	#movl $0x17, %eax       # fs 置为指向局部数据段（程序的数据段）
	#mov	%ax, %fs
	incl jiffies
	movb $0x20, %al         # 由于初始化中断芯片时没有采用自动EOI，所以这里需要发指令结束该硬件中断
	outb %al, $0x20
	movl CS(%esp), %eax		# 从栈中取出执行系统调用代码的选择符（cs段寄存器值）中的当前特权级别（0或3）并压入堆栈, 作为do_timer的参数，
	andl $3, %eax			# do_timer() 函数执行任务切换、计时等工作，在kernel/sched.c
	pushl %eax
	call do_timer
	addl $4, %esp
	jmp ret_from_syscall
