# 大部分异常处理的底层代码，包括数学协处理器的异常

.globl divide_error,debug,nmi,int3,overflow,bounds,invalid_op
.globl double_fault,coprocessor_segment_overrun
.globl invalid_TSS,segment_not_present,stack_segment
.globl general_protection,coprocessor_error,irq13,reserved

# 这两个是STUB的,之后会在system_call.s中实现
.global corprocessor_error, parallel_interrupt, device_not_available

# 先处理无 error code 压栈的情况
# 相关中断有: 除零(Fault) 调试debug(Fault) nmi(Trap) 断点指令(Trap)
divide_error:
    pushl $do_divide_error         # 把调用的函数地址压栈
no_error_code:                      # 无出错号处理的入口
    xchgl %eax, (%esp)              # _do_divide_error -> eax, eax 被交换入栈
    pushl %ebx
    pushl %ecx
    pushl %edx
    pushl %edi
    pushl %esi
    pushl %ebp
    push %ds                        # 16位的段寄存器入栈后也要占用4个字节
    push %es
    push %fs
    push $0                         # 将 0 作为出错码入栈
    lea 44(%esp), %edx              # 取栈中原调用函数地址处的栈指针位置，并入栈
    pushl %edx
    movl $0x10, %edx                # 初始化段寄存器 ds、es和fs，加载内核数据段选择符
    mov %dx, %ds
    mov %dx, %es
    mov %dx, %fs
    call *%eax                      # * 间接调用，即调用 do_divide_error()
    addl $8, %esp                   # 相当于两次pop，弹出c函数两个参数
    pop %fs
    pop %es
    pop %ds
    popl %ebp
    popl %esi
    popl %edi
    popl %edx
    popl %ecx
    popl %ebx
    popl %eax
    iret


# int1 debug 调试中断入口点 类型 Fault
debug:
	pushl $do_int3		# _do_debug
	jmp no_error_code

# int2 Non maskable interrupts 入口点 类型 Trap
nmi:
	pushl $do_nmi
	jmp no_error_code

# int3 断点指令引起中断的入口点 类型 Trap
int3:
	pushl $do_int3
	jmp no_error_code

# int4 溢出错误入口点 类型 Trap
overflow:
	pushl $do_overflow
	jmp no_error_code

# int5 边界检查出错 类型 Fault
bounds:
	pushl $do_bounds
	jmp no_error_code

# int6 无效指令 类型 Fault
invalid_op:
	pushl $do_invalid_op
	jmp no_error_code

# int9 协处理器段超出 类型 Abort
coprocessor_segment_overrun:
	pushl $do_coprocessor_segment_overrun
	jmp no_error_code

# int15 其他 Intel 保留中断的入口点
reserved:
	pushl $do_reserved
	jmp no_error_code

# int45 -- irq13
# 协处理器处理完一个操作的时候就会发送 IRQ13 信号，通知CPU操作完成，80387执行计算时
# CPU 会等待其完成，下面通过写协处理端口(0xF0)消除BUSY信号，并重新激活80387的扩展请求
# 引脚 PERREQ
irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al
	outb %al,$0x20
	jmp 1f
1:	jmp 1f
1:	outb %al,$0xA0
	popl %eax
	jmp coprocessor_error

# 下面的中断会在压入中断返回地址之后将出错号一同压栈，因此返回时需要弹出出错号
# Double Fault, 类型 Abort 有出错码
# 当CPU在调用一个异常处理程序的时候又检测到另一个异常，而且这两个异常无法被串行处

double_fault:
	pushl $do_double_fault
error_code:
	xchgl %eax, 4(%esp)		# 将出错号%eax交换，同时%eax入栈
	xchgl %ebx, (%esp)		# 将要调用的C函数的地址与%ebx交换，同时%ebx入栈
	pushl %ecx
	pushl %edx
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %ds
	pushl %es
	pushl %fs
	pushl %eax
	lea 44(%esp), %eax
	pushl %eax
	movl $0x10, %eax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	call *%ebx				# 间接调用, %ebx 中存放的就是要调用的C函数的地址
	addl $8, %esp
	pop %fs
	pop %es
	pop %ds
	pop %ebp
	popl %edi
	popl %esi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret

# int10 无效的任务状态段(TSS) 类型 Fault
invalid_TSS:
	pushl $do_invalid_TSS
	jmp error_code

# int11 段不存在 类型 Fault
segment_not_present:
	pushl $do_segment_not_present
	jmp error_code

# int12 堆栈段错误 类型 Fault
stack_segment:
	pushl $do_stack_segment
	jmp error_code

# int13 一般保护性错误 类型 Fault
general_protection:
	pushl $do_general_protection
	jmp error_code

# coprocessor error 先在这里实现一个stub的，之后会实现
coprocessor_error:
	pushl $do_stub
	jmp error_code

parallel_interrupt:
	pushl $do_stub
	jmp error_code

device_not_available:
	pushl $do_stub
	jmp error_code

# int7 设备不存在 将在 kernel/system_call.s 中实现
# int14 页错误 将在 mm/page.s 中实现
# int16 协处理器错误 将在 kernel/system_call.s 中实现
# int 0x20 时钟中断 将在 kernel/system_call.s 中实现
# int 0x80 系统调用 将在 kernel/system_call.s 中实现
