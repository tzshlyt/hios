.code32

# 大部分异常处理的底层代码，包括数学协处理器的异常
# 本代码文件主要涉及对Intel保留中断 int0-int16 的处理( int17-int31 留作今后使用)
# 以下是一些全局函数名的声明，其原型在 traps.c 中说明。
.globl divide_error,debug,nmi,int3,overflow,bounds,invalid_op
.globl double_fault,coprocessor_segment_overrun
.globl invalid_TSS,segment_not_present,stack_segment
.globl general_protection,coprocessor_error,irq13,reserved

# 这两个是STUB的,之后会在system_call.s中实现
.global corprocessor_error, parallel_interrupt, device_not_available

# 下面这段程序处理无出错号的情况
# 先处理无 error code 压栈的情况
# 相关中断有: 除零(Fault) 调试debug(Fault) nmi(Trap) 断点指令(Trap)
# int0 - 处理被零除出错的情况。类型：错误(Fault), 错误号：无
# 在执行DIV或IDIV指令时，若除数是0，CPU就会产生这个异常。当EAX(或AX、AL)容纳
# 不了一个合法除操作的结果时也会产生这个异常。标号'_do_divide_error'实际上是 C 语言
# 函数do_divide_error编译后所产生模块中对应的名称。函数'do_divide_error'在 traps.c 中实现
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
    call *%eax                      # * 号表示调用操作数指定地址处的函数，称为间接调用，即调用 do_divide_error()
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
    popl %eax						# 弹出原来eax中的内容
    iret


# int1 -- debug 调试中断入口点。处理过程同上。类型：错误/陷阱(Fault/Trap), 错误号：无。
# 当EFLAGS中TF标志置位时而引发的中断。当发现硬件断点(数据：陷阱，代码：错误)；或者
# 开启了指令跟踪陷阱或任务交换陷阱，或者调试寄存器访问无效(错误)，CPU就会产生该异常
debug:
	pushl $do_int3		# _do_debug
	jmp no_error_code

# int2 - 非屏蔽中断调用入口点。类型：陷阱；无错误号。
# 这是仅有的被赋予固定中断向量的硬件中断。每当接收到一个NMI信号，CPU内部就会产生中断
# 向量2，并执行标准中断应答周期，因此很节省时间。NMI通常保留为极为重要的硬件事件使用。
# 当CPU收到一个i额NMI信号并且开始执行其中断处理过程时，随后所有的硬件中断都将被忽略
nmi:
	pushl $do_nmi
	jmp no_error_code

# int 3 - 断点指令引起的中断的入口点。类型：陷阱；无错误号。
# 由int 3指令引发的中断，与硬件中断无关。该指令通常由调试器插入被调试程序的代码中。
# 处理过程同_debug.
int3:
	pushl $do_int3
	jmp no_error_code

# int 4 - 溢出出错处理中断入口。类型：陷阱；无错误号。
# EFLAGS 中 OF标志置位时CPU执行INT0指令就会引发该中断。通常用于编译器跟踪算术计算溢出
overflow:
	pushl $do_overflow
	jmp no_error_code

# int5 - 边界检查出错中断入口点。类型：错误；无错误号。
# 当操作数在有效范围以外时引发的中断。当BOUND指令测试失败就会产生该中断。BOUND指令有
# 3个操作数，如果第1个不在另外两个之间，就产生异常5.
bounds:
	pushl $do_bounds
	jmp no_error_code

# int6 - 无效操作指令出错中断入口点。类型：错误；无错误号。
# CPU执行机构检测到一个无效的操作码而引起的中断。
invalid_op:
	pushl $do_invalid_op
	jmp no_error_code

# int9 - 协处理器段超出出错中断入口点。类型：放弃；无错误号。
# 该异常基本上等同于协处理器 出错保护。因为在浮点指令操作数太大时，我们就有这个机会来
# 加载或保存超出数据段的浮点值
coprocessor_segment_overrun:
	pushl $do_coprocessor_segment_overrun
	jmp no_error_code

# int15 - 其他Intel保留中断的入口点。
reserved:
	pushl $do_reserved
	jmp no_error_code

# int45 -- irq13 (=0x20+13)Linux设置的数字协处理器硬件中断。
# 协处理器处理完一个操作的时候就会发送 IRQ13 信号，通知CPU操作完成，80387执行计算时 CPU 会等待其完成，
# 下面通过写协处理端口(0xF0)消除BUSY信号，并重新激活80387的扩展请求引脚 PERREQ
irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al
	outb %al,$0x20				 # 向 8259主终端控制芯片发送EOI（中断结束）信号。
	jmp 1f						 # 这两个跳转指令起延时作用。
1:	jmp 1f
1:	outb %al,$0xA0				 # 再向 8259从终端控制芯片发送EOI（中断结束）信号。
	popl %eax
	jmp coprocessor_error

# 下面的中断会在压入中断返回地址之后将出错号一同压栈，因此返回时需要弹出出错号
# Double Fault, 类型 Abort 有出错码
# 当CPU在调用一个异常处理程序的时候又检测到另一个异常，而且这两个异常无法被串行处

# int8 - 双出错故障。类型：放弃；有错误码。
# 通常当CPU在调用前一个异常的处理程序而又检测到一个新的异常时，这两个异常会被
# 串行地进行处理，但也会碰到很少的情况，CPU不能进行这样的串行处理操作，此时
# 就会引发该中断。
double_fault:
	pushl $do_double_fault
error_code:
	xchgl %eax, 4(%esp)		# error code <-> %eax, 将出错号%eax交换，同时%eax入栈, 原来地址被保存在堆栈上
	xchgl %ebx, (%esp)		# &function <-> %ebx, 将要调用的C函数的地址与%ebx交换，同时%ebx入栈, 原来地址被保存在堆栈上
	pushl %ecx
	pushl %edx
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %ds
	pushl %es
	pushl %fs
	pushl %eax				# error code  # 出错号入栈
	lea 44(%esp), %eax		# offset  # 程序返回地址处堆栈指针位置值入栈
	pushl %eax
	movl $0x10, %eax		# 置内核数据段选择符。
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	call *%ebx				# 间接调用, %ebx 中存放的就是要调用的C函数的地址
	addl $8, %esp			# 丢弃入栈的2个用作C函数的参数。
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

# int10 - 无效的任务状态段(TSS).类型：错误；有出错码。
# CPU企图切换到一个进程，而该进程的TSS无效。根据TSS中哪一部分引起了异常，
# 当由于TSS长度超过104字节时，这个异常在当前任务中产生，因而切换被终止。
# 其他问题则会导致在切换后的新任务产生本异常。
invalid_TSS:
	pushl $do_invalid_TSS
	jmp error_code

# int11 - 段不存在。类型：错误；有出错码。
# 被引用的段不再内存中。段描述符中标志着段不再内存中。
segment_not_present:
	pushl $do_segment_not_present
	jmp error_code

# int12 - 堆栈段错误。类型：错误；有出错码。
# 指令操作试图超出堆栈段范围，或者堆栈段不再内存中。这是异常11和13的特例。
# 有些操作系统可以利用这个异常来确定什么时候应该为程序分配更多的栈空间。
stack_segment:
	pushl $do_stack_segment
	jmp error_code

# int13 - 一般保护性出错。类型：错误；有出错码。
# 表明是不属于任何其他类的错误。若一个异常产生时没有对应的处理向量(0 -- 16),
# 通常就会归到此类。
general_protection:
	pushl $do_general_protection
	jmp error_code

# coprocessor error 先在这里实现一个stub的，之后会实现
coprocessor_error:
	pushl $do_stub
	jmp error_code

parallel_interrupt:  # 本版本没有实现，这里只发EOI
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret

device_not_available:
	pushl $do_stub
	jmp error_code

# int7 设备不存在(_device_not_available), 将在 kernel/system_call.s 中实现
# int14 页错误(_page_fault), 将在 mm/page.s 中实现
# int16 协处理器错误(_coprocessor_error), 将在 kernel/system_call.s 中实现
# int 0x20 时钟中断(_timer_interrupt), 将在 kernel/system_call.s 中实现
# int 0x80 系统调用(_system_call), 将在 kernel/system_call.s 中实现
