#ifndef _UNISTD_H
#define _UNISTD_H

#ifndef NULL
#define NULL    ((void *)0)
#endif

#include <sys/types.h>

#ifdef __LIBRARY__

#define __NR_setup      0
#define __NR_exit       1
#define __NR_fork       2
#define __NR_read       3
#define __NR_waitpid	7
#define __NR_sleep      10
#define __NR_alarm      27
#define __NR_pause      29
#define __NR_kill       37
#define __NR_sigaction  67
#define __NR_sgetmask   68
#define __NR_ssetmask   69

// Just for debug
#define __NR_sys_debug  72
#define __NR_user_tty_read 73
#define __NR_user_tty_write 74

/* 例如
static inline int fork(void) {
    long __res;
    __asm__ volatile("int $0x80\n\t"
            :"=a" (__res)
            :"0" (__NR_fork));
    if( __res >= 0)
        return (int) __res;
    return -1;
}

static inline int sys_debug(char *str) {
    long __res;
    __asm__ volatile("int $0x80\n\t"
            :"=a" (__res)
            :"0" (__NR_sys_debug), "b" ((long)(str)));
    if (__res >= 0)
        return (int) __res;
    return -1;
}
*/

#define _syscall0(type, name)  \
type name(void) \
{ \
    long __res; \
    __asm__ volatile("int $0x80\n\t" \
            : "=a" (__res)          \
            : "0" (__NR_##name)); \
    if (__res >= 0) \
        return (type) __res; \
    /*errno = -__res;*/ \
    return  -1;\
}

#define _syscall1(type, name, atype, a) \
type name(atype a) \
{ \
    long __res; \
    __asm__ volatile("int $0x80\n\t" \
            : "=a" (__res) \
            : "0" (__NR_##name), "b" ((long) a)); \
    if (__res >= 0) \
        return (type) __res; \
    /*errno = -__res;*/ \
    return -1; \
}

#define _syscall2(type, name, atype, a, btype, b) \
type name(atype a, btype b) \
{ \
    long __res; \
    __asm__ volatile("int $0x80\n\t" \
            : "=a" (__res) \
            : "0" (__NR_##name), "b" ((long) a), "c" ((long) b)); \
    if (__res >= 0) \
        return (type) __res; \
    /*errno = -__res;*/ \
    return -1; \
}

#define _syscall3(type, name, atype, a, btype, b, ctype, c) \
type name(atype a, btype b, ctype c) \
{ \
    long __res; \
    __asm__ volatile("int $0x80\n\t" \
            : "=a" (__res) \
            : "0" (__NR_##name), "b" ((long) a), "c" ((long) b), "d" ((long) c)); \
    if (__res >= 0) \
        return (type) __res; \
    /* errno = -__res;*/ \
    return -1; \
}

#endif

void _exit(int status);
pid_t waitpid(pid_t pid, int * wait_stat, int options);
pid_t wait(int * wait_stat);
int alarm(long seconds);
// int read(int fildes, char * buf, off_t count);

#endif