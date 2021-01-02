#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <serial_debug.h>

//// 文件读函数 - 根据i节点和文件结构，读取文件中数据。
// 返回值是实际读取的字节数，或出错号(小于0)
int file_read(struct m_inode * inode, struct file *filp, char * buf, int count) {
    int left, chars, nr = 0;
    struct buffer_head * bh;
    struct dir_entry * de;

    if ((left = count) <= 0)
        return 0;

    nr = bmap(inode, 0);
    bh = bread(inode->i_dev, nr);

    int entries = count / (sizeof(struct dir_entry));
    int i = 0;
    de = (struct dir_entry *) bh->b_data;
    while (i < entries) {
        s_printk("%s\t%d\t", de->name, de->inode);
        struct m_inode *id = iget(inode->i_dev, de->inode);
        if (id) {
            s_printk("%x\t%d\n", id->i_mode, id->i_size);
        }
        de++;
        i++;
    }
    return 0;
}