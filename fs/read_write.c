
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>
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
    s_printk("sys_read() fd = %x count = %d \n", fd, count);
    struct file * file;
    struct m_inode * inode;

    if (fd >= NR_OPEN || count < 0 || !(file = current->filp[fd]))
		return -EINVAL;

    inode = file->f_inode;

    // 如果是 目录文件 或者是 常规文件
    if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) {
        count = inode->i_size;  // TODO: 删除

        // 若文件当前读写指针值 + 读去字节数 > 文件长度
        // 则重新设置读取字节数 = 文件长度 - 当前读写指针值
        if (count+file->f_pos > inode->i_size)
			count = inode->i_size - file->f_pos;
		if (count<=0)
			return 0;
        return file_read(inode, file, buf, count);
    }

    printk("(Read)inode->i_mode=%x\n", inode->i_mode);
	return -EINVAL;
}