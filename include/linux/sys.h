#ifndef _SYS_H
#define _SYS_H

#include<linux/sched.h>

extern int sys_setup();
extern int sys_exit();
extern int sys_fork();
extern int sys_read();
extern int sys_waitpid();
extern int sys_pause();
extern int stub_syscall();
extern int serial_debugstr(char *str);
extern int sys_kill(int pid, int sig);
extern int sys_sigaction(int signum, struct sigaction *action, struct sigaction *old_action);
extern int sys_sgetmask(void);
extern int sys_ssetmask(int newMask);
extern int sys_alarm(long seconds);
extern int sys_sleep(long seconds);

// Just for debug use
extern int tty_read(unsigned channel, char *buf, int nr);
extern int _user_tty_write(unsigned channel, char *buf, int nr);

//TODO 目前除了少数syscall之外其余的syscall均为stub状态
fn_ptr sys_call_table[] = {
    sys_setup,
    sys_exit,
    sys_fork,     // 2
    sys_read,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    sys_waitpid,
    stub_syscall,
    stub_syscall,
    sys_sleep, // 10
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall, // 20
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    sys_pause,    // 29
    stub_syscall, // 30
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    sys_kill,     // 37
    stub_syscall,
    stub_syscall,
    stub_syscall, // 40
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall, // 50
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall, // 60
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    stub_syscall,
    sys_sigaction, // 67
    sys_sgetmask,
    sys_ssetmask,
    stub_syscall, // 70
    stub_syscall,
    serial_debugstr,    // 调试
    tty_read,
    _user_tty_write
};

#endif