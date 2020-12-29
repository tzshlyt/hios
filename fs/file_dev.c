#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <serial_debug.h>

int file_read(struct m_inode * inode, struct file *filp, char * buf, int count) {
    int left, chars, nr = 0;
    struct buffer_head * bh;
    struct dir_entry * de;

    if ((left = count) < 0)
        return 0;

    int block = inode->i_zone[0];
    bh = bread(inode->i_dev, block);
    if (bh) {
        do {
            de = nr + bh->b_data;
            s_printk("%s\t%d\t", de->name, de->inode);
            nr += 16;
            struct m_inode *id = iget(inode->i_dev, de->inode);
            if (id) {
                s_printk("%x\t%d\n", id->i_mode, id->i_size);
            }
        } while (nr < 16*20);
    }
    
    return 0;
}