#include <unistd.h>
#include <linux/head.h>
#include <linux/sched.h>
#include <serial_debug.h>

extern int sys_alarm(long seconds);

int stub_syscall(void) {
    return 0;
}

int sys_sleep(long seconds) {
    // s_printk("sys_sleep entered seconds = %d\n", seconds);

    sys_alarm(seconds);
    current->state = TASK_INTERRUPTIBLE;
    schedule();
    return 0;
}