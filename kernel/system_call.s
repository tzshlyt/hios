/**
 * 实现系统调用过程
 * 使用中断调用 int 0x80 和放在 eax 中的功能号来使用内核提供各种功能服务
 *
 * Linus原注释中的一般中断过程是指除了系统调用中断(int 0x80)和时钟中断(int 0x20)以外的其他中断。
 * 这些中断会在内核态或用户态随机发生，若在这些中断过程中也处理信号识别的话，
 * 就有可能与系统调用中断和时钟中断过程中对信号的识别处理过程相冲突，违反了内核代码非抢占原则。
 * 因此系统既无必要在这些“其他”中断中处理信号，也不允许这样做。
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

# 定义入口点
.global timer_interrupt, system_call, sys_fork, hd_interrupt

# 堆栈中各个寄存器的偏移位置
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
OLDESP = 0x28			 # 当特权级发生变化时栈会切换，用户栈指针被保存在内核态中。
OLDSS = 0x2C

nr_system_calls = 72 + 3 # sys_debug

# 以下是任务结构（task_struct）中变量偏移值，参见 sched.h
state = 0				# 进程状态码
counter = 4				# 任务运行时间计数（递减）（滴答数），运行时间片
priority = 8    		# 运行优先数。任务开始运行时counter=priority,越大则运行时间越长
signal	= 12    		# 是信号位图，每个bit代表一种信号，信号值=位偏移值+1
sigaction = 16			# MUST be 16 (=len of sigaction),
						# 信号执行属性结构数组的偏移量，对应信号将要执行的操作和标志信息。
blocked = (33*16)   	# 受阻塞信号位图的偏移量


### 错误的系统调用号
.align 2				# 内存4字节对齐
bad_sys_call:
	movl $-1, %eax		# eax 中置-1，退出中断
	iret

# 重新执行调度程序入口，调度程序在 sched.c 中
# 当调度程序 schedule() 返回时就从 ret_from_sys_call 处继续执行
.align 2
reschedule:
	pushl $ret_from_syscall					# 将ret_from_sys_call返回地址压入堆栈
	jmp schedule

### int 0x80 系统调用入口点，(调用中断int 0x80,eax 中是调用号)
.align 2
system_call:
	cmp $nr_system_calls - 1, %eax			# 调用号超过范围
	ja bad_sys_call
	push %ds								# 保存原段寄存器值
	push %es
	push %fs
	pushl %edx								# 一个系统调用最多可带3个参数
	pushl %ecx
	pushl %ebx								# ebx 中存放第1个参数
# ds, es 指向内核数据段（全局描述符表中数据段描述符）
# linux 内核默认地把段寄存器 ds, es 用于内核数据段，fs 用于用户数据段
	movl $0x10, %edx						# 0x10 指向内核数据段
	mov %dx, %ds
	mov %dx, %es
# fs 指向局部数据段（局部描述符表中数据段描述符）, 即指向执行本次系统调用的用户程序的数据段
# 注意，在 linux 0.11 中内核给任务分配的代码段和数据段是重叠的，它们的段地址和段限长相同
	movl $0x17, %edx
	mov %dx, %fs
	call *sys_call_table(, %eax, 4)			# 调用地址 = [system_call_table + %eax * 4], sys_call_table[]是一个指针数组，定义在include/linux/sys.h中
	pushl %eax								# 把系统调用返回值入栈
# 接下来查看当前任务的运行状态。如果上面 c 函数的操作或其它情况而使进程的状态从执行态变成其它状态
# 如果不在就绪状态(state != 0)就去执行调度程序。
# 如果该任务在就绪状态，但其时间片已用完(counter = 0),则也去执行调度程序。
# 例如当后台进程组中的进程执行控制终端读写操作时，那么默认条件下该后台进程组所有进程会收到 SIGTTIN 或 SIGTTOU 信号，
# 导致进程组中所有进程处于停止状态。而当前进程则会立刻返回。
	movl current, %eax						# 取当前任务数据结构地址 -> eax
	cmpl $0, state(%eax)					# 如果不在就绪状态，就去调度程序
	jne reschedule
	cmpl $0, counter(%eax)					# 时间片已用完，则去调度程序
	je reschedule

# 由于在执行 jmp schedule 之前把返回地址 ret_from_syscall 入栈，因此执行完 schedule() 后最终会返回到 ret_from_syscall 继续执行
# 从系统调用c函数返回后，对信号进行识别处理其它中断服务程序退出时也将跳转到这里进行处理后才退出中断过程
# 例如后面的处理器出错中断 int 16.
ret_from_syscall:
	# 首先判别当前任务是否是初始任务task0,如果是则不比对其进行信号量方面的处理，直接返回。
	movl current, %eax
	cmpl task, %eax
	je 3f
	# 通过对原调用程序代码选择符的检查来判断调用程序是否是用户任务。如果不是则直接退出中断。
	# 这是因为任务在内核态执行时不可抢占。否则对任务进行信号量的识别处理。
	# 这里比较选择符是否为用户代码段的选择符0x000f(RPL=3,局部表，第一个段(代码段))来判断是否为用户任务。
	# 如果不是则说明是某个中断服务程序跳转到上面的，于是跳转退出中断程序。
	# 如果原堆栈段选择符不为0x17(即原堆栈不在用户段中)，也说明本次系统调用的调用者不是用户任务，则也退出。
	cmpw $0x0f, CS(%esp)
	jne 3f
	cmpw $0x17, OLDSS(%esp)
	jne 3f
	# 下面这段代码用于处理当前任务中的信号。首先取当前任务结构中的信号位图(32位，每位代表1种信号)，
	# 然后用任务结构中的信号阻塞(屏蔽)码，阻塞不允许的信号位，取得数值最小的信号值，
	# 再把原信号位图中该信号对应的位复位(置0)，最后将该信号值作为参数之一调用do_signal().
	# do_signal() 在kernel/signal.c中，其参数包括13个入栈信息。
	movl signal(%eax),%ebx          # 取信号位图→ebx,每1位代表1种信号，共32个信号
	movl blocked(%eax),%ecx         # 取阻塞(屏蔽)信号位图→ecx
	notl %ecx                       # 每位取反
	andl %ebx,%ecx                  # 获得许可信号位图
	bsfl %ecx,%ecx                  # 从低位(位0)开始扫描位图，看是否有1的位，若有，则ecx保留该位的偏移值
	je 3f                           # 如果没有信号则向前跳转退出
	btrl %ecx,%ebx                  # 复位该信号(ebx含有原signal位图)
	movl %ebx,signal(%eax)          # 重新保存signal位图信息→current->signal.
	incl %ecx                       # 将信号调整为从1开始的数(1-32)
	pushl %ecx                      # 信号值入栈作为调用do_signal的参数之一
	call do_signal                  # 调用C函数信号处理程序(kernel/signal.c)
	popl %eax                       # 弹出入栈的信号值
3:	popl %eax						# eax 中含有上面入栈系统调用的返回值
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret									# 中断返回意味着进程0从内核态转换成用户态

### int32 - (int 0x20) 时钟中断处理程序。中断频率被设置为 100Hz。
# 定时芯片 8253/8254 是在 kernel/sched.c 中初始化的。因此这里 jiffies 每 10ms 加 1.
# 这段代码将 jiffies 增 1，发送结束中断指令给 8259控制器，然后用当前特权级作为
# 参数调用C函数 (long CPL).当调用返回时转去检测并处理信号。
.align 2
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
	movl $0x17, %eax       # fs 置为指向局部数据段（程序的数据段）
	mov %ax, %fs
	incl jiffies
# 由于初始化中断芯片时没有采用自动EOI，所以这里需要发指令结束该硬件中断
	movb $0x20, %al         # EOI to interrupt controller #1
	outb %al, $0x20			# 操作命令字OCW2送0x20端口
# 下面从栈中取出执行系统调用代码的选择符（cs段寄存器值）中的当前特权级别（0或3）并压入堆栈,
# 作为do_timer的参数， do_timer() 函数执行任务切换、计时等工作，在kernel/sched.c
	movl CS(%esp), %eax
	andl $3, %eax			# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call do_timer			# 'do_timer(long CPL)' does everything from
	addl $4, %esp			# task switching to accounting ...
	jmp ret_from_syscall

### sys_fork()调用，用于创建子进程，是system_call功能2.
# 首先调用C函数find_empty_process()，取得一个进程号PID。若返回负数则说明目前任务数组
# 已满。然后调用copy_process()复制进程。
.align 2
sys_fork:
	call find_empty_process	# 在 fork.c 中
	testl %eax, %eax		# 在eax中返回进程号pid，若返回负数则退出
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call copy_process		# 在 fork.c 中
	addl $20, %esp			# 丢弃这里所有压栈内容
1: 	ret

### int46 - (int 0x2e)硬盘中断处理程序，响应硬件中断请求IRQ4。
# 当请求的硬盘操作完成或出错就会发出此中断信号。
# 首先向8259A中断控制从芯片发送结束硬件中断指令(EOI),然后取变量do_hd中的函数指针
# 放入edx寄存器中，并置do_hd为NULL，接着判断edx函数指针是否为空。如果为空，则给edx
# 赋值指向unexpected_hd_interrupt(),用于显示出错信息。随后向8259A主芯片送EOI指令，
# 并调用edx中指针指向的函数：read_intr(), write_intr()或 unexpected_hd_interrupt().
hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax			# ds，es指向内核数据段
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax			# fs 置为指向局部数据段（程序的数据段）
	mov %ax,%fs
# 由于初始化中断控制芯片时没有采用自动EOI，所以这里需要发指令结束该硬件中断
	movb $0x20, %al
	outb %al, $0xA0			# EOI to interrupt controller #1
	jmp 1f					# give port chance to breathe
1:	jmp 1f
# do_hd 定义为一个函数指针，将被赋值 read_intr()或 write_intr()函数地址。放到 edx
# 寄存器后就将 do_hd 指针变量置为NULL。然后测试得到的函数指针，若该指针为空，则
# 赋予该指针指向C函数 unexpected_hd_interrupt()，以处理未知硬盘中断。
1:	xorl %edx, %edx
	xchgl do_hd, %edx
	testl %edx, %edx						# 测试函数指针是否为NULL
	jne 1f									# 若空，则使指针指向C函数 unexpected_hd_interrup().
	movl $unexpected_hd_interrupt, %edx
1:	outb %al, $0x20							# 送主8259A中断控制器EOI命令(结束硬件中断)
	call *%edx 								# "interesting" way of handling intr.
	pop %fs                     			# 上句调用 do_hd 指向C函数
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret