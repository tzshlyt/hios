
#include <sys/types.h>
#include <errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <serial_debug.h>

extern int file_read(struct m_inode * inode, struct file * filp, char * buf, int count);

//// 读文件系统调用
// 参数：fd - 文件句柄
//      buf - 缓冲区，
//      count - 预读字节数
int sys_read(unsigned int fd, char * buf, int count) {
    s_printk("sys_read() %x \n", fd);
    struct file * file;
    struct m_inode * inode;

    if (fd >= NR_OPEN || count < 0 || !(file = current->filp[fd]))
		return -EINVAL;

    inode = file->f_inode;
    count = inode->i_size;
    return file_read(inode, file, buf, count);
}