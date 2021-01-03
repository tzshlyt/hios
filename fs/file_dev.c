#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <serial_debug.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

//// 文件读函数 - 根据i节点和文件结构，读取文件中数据。
// 返回值是实际读取的字节数，或出错号(小于0)
int file_read(struct m_inode * inode, struct file *filp, char * buf, int count) {
    int left, chars, nr = 0;
    struct buffer_head * bh;

    if ((left = count) <= 0)
        return 0;

    while (left) {
        if ((nr = bmap(inode, (filp->f_pos)/BLOCK_SIZE))) {      // 计算出文件当前指针所在的数据块号
            if (!(bh = bread(inode->i_dev, nr)))
                break;
        } else {
            bh = NULL;
        }
        nr = filp->f_pos % BLOCK_SIZE;                          // 文件读写指针在数据块中的偏移值 nr
        chars = MIN(BLOCK_SIZE-nr, left);                       // 希望读取的字节数为(BLOCK_SIZE-nr) 和 还需读取的字节数left做比较
        filp->f_pos += chars;
        left -= chars;

        if (bh) {
            char * p = nr + bh->b_data;
            while (chars-->0) {
                put_fs_byte(*(p++), buf++);
            }
            brelse(bh);
        } else {
            while (chars-->0) {
	            put_fs_byte(0, buf++);
            }
        }
    }
    // 修改该i节点的访问时间为当前时间
    // 返回读取的字节数，若读取字节数为0，则返回出错号
    inode->i_atime = (unsigned long)CURRENT_TIME;
    return (count-left)?(count-left):-ERROR;
}