# 该键盘驱动汇编程序主要包括键盘中断处理程序

.text
.global keyboard_interrupt

keyboard_interrupt:
	pushl %eax
	pushl %ebx
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	movl $0x10, %eax                # 将 ds、es 段寄存器置为内核数据段
	mov %ax, %ds
	mov %ax, %es
	xor %al, %al
	inb $0x60, %al                  # 读取扫描码
	push %ax
	movb $0x20, %al                 # 向 8259 中断芯片发送 EOI (中断结束)信号
	outb %al, $0x20
	call do_keyboard_interrupt
	pop  %ax
	pop  %es
	pop  %ds
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret
