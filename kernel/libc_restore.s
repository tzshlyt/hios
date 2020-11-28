
.global __sig_restore, __masksig_restore

# 用于信号处理结束后清理用户态堆栈，并恢复系统调用存放在eax中的返回值
# 若没有 blocked 则使用这个函数
__sig_restore:		# Use to restore to user routine
	addl $4, %esp	# 丢弃信号值 signr
	popl %eax		# 恢复系统调用返回值
	popl %ecx		# 恢复原用户程序寄存器值
	popl %edx
	popfl			# 恢复用户程序时的标志寄存器
	ret

# 若有 blocked 则使用这函数，blocked 供 ssetmask 使用
__masksig_restore:
	addl $4, %esp
# We need a call here to set mask
#	call __ssetmask	 	TODO:
	addl $4, %esp	# 丢弃 blocked 值
	popl %eax
	popl %ecx
	popl %edx
	popfl
	ret
