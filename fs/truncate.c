// 释放指定i节点设备上占用的所有逻辑块，包括直接块、一次间接块、二次间接块。
// 从而将文件节点对应的文件长度截为0，并释放占用的设备空间。
#include <linux/sched.h>

#include <sys/stat.h>

//// 释放所有一次间接块
static void free_ind(int dev, int block) {
    struct buffer_head * bh;
    unsigned short * p;
    int i;

    if (!block)
        return;

    if ((bh = bread(dev, block))) {
        p = (unsigned short *) bh->b_data;  // 指向缓冲块数据区
        for (i = 0; i < 512; i++, p++) {    // 每个逻辑块上可有512个块号
            if (*p) {
                free_block(dev, *p);        // 释放指定的设备逻辑块
            }
        }
        brelse(bh);
    }
    free_block(dev, block);                 // 释放一次间接块
}

//// 释放所有二次间接块
static void free_dind(int dev, int block) {
    struct buffer_head * bh;
    unsigned short * p;
    int i;

    if (!block)
        return;

    if ((bh = bread(dev, block))) {
        p = (unsigned short *) bh->b_data;
        for (i = 0; i < 512; i++, p++) {
            if (*p) {
                free_ind(dev, *p);              // 释放所有一次间接块
            }
        }
        brelse(bh);
    }
    free_block(dev, block);
}

//// 截断文件数据函数
// 将节点对应的文件长度截为0，并释放所占用的设备空间
void truncate(struct m_inode * inode) {
    int i;
    // 首先判断指定i节点的有效性，如果不是常规文件或者是目录文件，则返回
    if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
        return;

    // 释放7个直接逻辑块
    for (i = 0; i < 7; i++) {
        if (inode->i_zone[i]) {
            free_block(inode->i_dev, inode->i_zone[i]); // 释放设备上指定逻辑块号的磁盘块
            inode->i_zone[i] = 0;
        }
    }

    free_ind(inode->i_dev, inode->i_zone[7]);       // 释放所有一次间接块
    free_dind(inode->i_dev, inode->i_zone[8]);      // 释放所有二次间接块
    inode->i_zone[7] = inode->i_zone[8] = 0;

    inode->i_size = 0;                              // 文件大小置0
    inode->i_dirt = 1;                              // 置节点已修改标志

    // TODO: 系统时间
    // inode->i_mtime = inode->i_ctime = CURRENT_TIME; // 重置文件修改时间和i节点改变时间
}