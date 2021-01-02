#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <fcntl.h>
#include <sys/stat.h>
//// 复制文件句柄
// 参数fd是欲复制的文件句柄，arg指定新文件句柄的最小值。
// 返回新文件句柄或出错。
static int dupfd(unsigned int fd, unsigned int arg) {
    if (fd >= NR_OPEN || !current->filp[fd])
		return -EBADF;
	if (arg >= NR_OPEN)
		return -EINVAL;

    while (arg < NR_OPEN)
		if (current->filp[arg])
			arg++;
		else
			break;
    if (arg >= NR_OPEN)
		return -EMFILE;

    // 在执行时关闭标志位图close_on_exec中复位该句柄位。
    // 即在运行exec()类函数时，不会关闭用dup()创建的句柄。
    current->close_on_exec &= ~(1<<arg);
    (current->filp[arg] = current->filp[fd])->f_count++;
    return arg;
}

//// 复制文件句柄系统调用
// 复制指定文件句柄oldfd，新句柄的值是当前最小的未用句柄值。
// 参数：fileds - 被复制的文件句柄。
// 返回新文件句柄值。
int sys_dup(unsigned int fildes) {
	return dupfd(fildes, 0);
}