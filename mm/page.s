
# 包含底层页异常处理代码

.global page_fault              # 声明为全局变量。将在 traps.c 中用于设置页异常描述符

page_fault:
    xchgl %eax, (%esp)          # 取出错误码到eax
    pushl %ecx
    pushl %edx
    pushl %ds
    pushl %es
    pushl %fs

    movl $0x10, %edx            # 置内核数据段选择符
    mov %dx, %ds
    mov %dx, %es
    mov %dx, %fs
    movl %cr2, %edx             # 取引起页面异常的线性地址
    pushl %edx                  # 将该线性地址和出错码压入栈中，作为将调用函数的参数
    pushl %eax
    testl $1, %eax              # 测试页存在标志P（位0），如果不是缺页引起的异常则跳转
    jne 1f
    call do_no_page            # 调用缺页处理函数 mm/memory.c 中
    jmp 2f
1:  call do_wp_page            # 调用写保护处理函数 mm/mmemory.c 中
2:  addl $8, %esp               # 丢弃压入栈中的两个参数

    pop %fs
    pop %es
    pop %ds
    pop %edx
    pop %ecx
    pop %eax
    iret
