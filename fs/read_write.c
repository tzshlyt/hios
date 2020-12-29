
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <serial_debug.h>

extern int file_read(struct m_inode * inode, struct file * filp, char * buf, int count);

int sys_read(unsigned int fd, char * buf, int count) {
    s_printk("sys_read()\n");
    struct file * file;
    struct m_inode * inode;
    inode = iget(ROOT_DEV, ROOT_INO);
    return file_read(inode, file, buf, count);
}