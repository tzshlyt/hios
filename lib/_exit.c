#define __LIBRARY__
#include <unistd.h>

// 内核使用的程序(退出)终止函数
void _exit(int exit_code)
{
    __asm__ volatile("int $0x80"::"a" (__NR_exit),"b" (exit_code));
}