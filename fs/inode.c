
#include <linux/fs.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <string.h>
#include <sys/stat.h>

// 内存中i节点表(NR_INODE=32)
struct m_inode inode_table[NR_INODE]={{0,},};

// 读指定i节点号的i节点信息
static void read_inode(struct m_inode * inode);
// 写i节点信息到高速缓冲中
static void write_inode(struct m_inode * inode);


static inline void lock_inode(struct m_inode * inode) {
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock = 1;
	sti();
}

static inline void unlock_inode(struct m_inode * inode) {
	inode->i_lock = 0;
	wake_up(&inode->i_wait);
}

//// 等待指定的i节点可用
// 如果i节点已被锁定，则将当前任务置为不可中断的等待状态，并添加到该
// i节点的等待队列i_wait中。直到该i节点解锁并明确地唤醒本地任务。
static inline void wait_on_inode(struct m_inode * inode) {
    cli();
    while (inode->i_lock)
        sleep_on(&inode->i_wait);
    sti();
}

//// 同步所有i节点
// 把内存i节点表中所有i节点与设备上i节点作同步操作
void sync_inodes() {
    int i;
    struct m_inode * inode;

    // 将该i节点写入高速缓冲区中。
    // 缓冲区管理程序buffer.c会在适当时机将他们写入盘中。
    inode = 0+inode_table;
    for (i = 0; i < NR_INODE; i++, inode++) {
        wait_on_inode(inode);
        if (inode->i_dirt && !inode->i_pipe)     // 被修改且不是管道节点
            write_inode(inode);
    }
}

//// 放回(放置)一个i节点（回写入设备）
// 主要用于把i节点引用计数值递减 1, 并且若是管道i节点，则唤醒等待的进程。
// 若是块设备文件i节点则刷新设备。
// 并且若i节点的链接计数为0，则释放该i节点占用的所有磁盘逻辑块，并释放该i节点。
void iput(struct m_inode * inode) {
    if (!inode)
        return;

    wait_on_inode(inode);
    if (!inode->i_count)    // i节点已经空闲, 其它代码有错误
        panic("iput: trying to free free inode");

    // 如果是管道i节点，则唤醒等待该管道的进程，引用次数减1，如果还有引用则返回。
    // 否则释放管道占用的内存页面，并复位该节点的引用计数值、已修改标志和管道标志，并返回。
    // 对于管道节点，inode->i_size存放着内存页地址。
    if (inode->i_pipe) {
        wake_up(&inode->i_wait);
        if (--inode->i_count)
            return;
        free_page(inode->i_size);
        inode->i_count = 0;
        inode->i_dirt = 0;
        inode->i_pipe = 0;
        return;
    }

    // 如果i节点对应的设备号＝0，则将此节点的引用计数递减1，返回。
    // 例如用于管道操作的i节点，其 i 节点的设备号为 0
    if (!inode->i_dev) {
        inode->i_count--;
        return;
    }

    // 如果是块设备文件的i节点，此时逻辑块字段0(i_zone[0])中是设备号，则刷新该设备。
    // 并等待i节点解锁。
    if (S_ISBLK(inode->i_mode)) {
        sync_dev(inode->i_zone[0]);
        wait_on_inode(inode);
    }

    // 如果i节点的引用计数大于1，则计数递减1后就直接返回(因为该i节点还有人在用，不能释放)，
    // 否则就说明i节点的引用计数值为1。如果i节点的链接数为0，则说明i节点对应文件被删除。
    // 于是释放该i节点的所有逻辑块，并释放该i节点。
    // 函数free_inode()用于实际释放i节点操作，即复位i节点对应的i节点位图bit位，清空i节点结构内容。
repeat:
    if (inode->i_count > 1) {
        inode->i_count--;
        return;
    }
    // 到这里 inode->i_count == 1
    if (!inode->i_nlinks) {     // 链接数为0，说明i节点对应文件被删除
        truncate(inode);        // 释放i节点的所有逻辑块
        free_inode(inode);      // 释放i节点
        return;
    }

    // i节点已作过修改，则回写更新该i节点
    if (inode->i_dirt) {
        write_inode(inode);
        wait_on_inode(inode);
        goto repeat;
    }

    // 到这说明 i_count == 1, i_nlinks != 0, i_drit == 0
    inode->i_count--;       // i_count == 0, 表示已释放
    return;
}

//// 从 i 节点表(inode_table)中获取一个空闲i节点项。
// 寻找引用计数 count 为 0 的 i 节点，并将其写盘后清零，返回指针。引用计数被置1.
struct m_inode * get_empty_inode(void) {
    struct m_inode * inode;
    static struct m_inode * last_inode = inode_table;   // 指向 i 节点第 1 项
    int i;

    do {
        inode = NULL;
        for (i = NR_INODE; i; i--) {
            if (++last_inode >= inode_table + NR_INODE) // 到了最后1个后从头开始，先++相当于从下标 1 开始，所以到了最后才使用第0个
                last_inode = inode_table;
            if (!last_inode->i_count) {                 // 找到空闲 i 节点
                inode = last_inode;
                if (!inode->i_dirt && !inode->i_lock)   // 未修改且未锁定，可以使用该节点
                    break;
            }
        }
        // for 循环结束后 inode 可能为 3 种情况
        // 1. inode == NULL，未找到空闲节点
        // 2. inode.i_count == 0, 但是 i_dirt 或 i_lock 不为 0，修改过或者被锁定
        // 3. inode.i_count == 0, 且未修改过和锁定
    } while (inode->i_count);

    if (!inode) {                                       // 没有找到空闲 i 节点，打印调试信息并停机
        for (i = 0; i < NR_INODE; i++)
            printk("%x: %d\t", inode_table[i].i_dev, inode_table[i].i_num);
        panic("No free inodes in mem");
    }
    wait_on_inode(inode);                               // 等待解锁（如果被上锁）
    while (inode->i_dirt) {                             // 如果已修改，刷新 i 节点
        write_inode(inode);
        wait_on_inode(inode);                           // 因为刷新可能会睡眠，因此需要再次循环等待该节点解锁
    }
    memset(inode, 0, sizeof(*inode));
	inode->i_count = 1;
	return inode;
}

//// 获得一个i节点
// 参数：dev - 设备号； nr - i 节点号。
// 从设备上读取指定节点号i节点到内存i节点表中，并返回该i节点指针。
// 首先在位于高速缓冲区中的i节点表中搜寻，若找到指定节点号的i节点则在经过一些判断处理后返回该i节点指针。
// 否则从设备dev上读取指定i节点号的i节点信息放入i节点表中，并返回该i节点指针。
struct m_inode * iget(int dev, int nr) {
    struct m_inode * inode, * empty;

    if (!dev)
		panic("iget with dev==0");

    // 预先从i节点表中取一个空闲i节点备用
    empty = get_empty_inode();

    // 接着扫描i节点表。寻找参数指定节点号nr的i节点。并递增该节点的引用次数。
    inode = inode_table;
    while (inode < NR_INODE+inode_table) {
        if (inode->i_dev != dev || inode->i_num != nr) {
            inode++;
            continue;
        }
        // 找到指定i节点
        wait_on_inode(inode);
        if (inode->i_dev != dev || inode->i_num != nr) {    // 在等待该节点解锁过程中，i节点表可能会发生变化。
            inode = inode_table;
            continue;
        }

        // 将该i节点引用计数增1，然后再做进一步检查
        inode->i_count++;
        // 是否是另一个文件系统的安装点，若是则寻找被安装文件系统根节点并返回。
        // 如果该i节点的确是其他文件系统的安装点，则在超级块表中搜寻安装在此i节点的超级块。
        // 如果没有找到，则显示出错信息，并放回本函数开始时获取的空闲节点empty，
        // 返回该i节点指针。
        if (inode->i_mount) {       // 是另一个文件系统的安装点
            int i;
            for (i = 0; i < NR_SUPER; i++) {
                if (super_block[i].s_imount == inode)   //  在超级块表中搜寻安装在此i节点的超级块
                    break;
            }
            if (i >= NR_SUPER) {                        // 没有找到
                printk("Mounted inode hasn't got sb\n");
                if (empty)
                    iput(empty);                        // 放回 empty
                return empty;
            }

            // 找到安装到inode节点的文件系统超级块
            // 于是将该i节点写盘放回，并从安装在此i节点上的文件系统超级块中取设备号，并令i节点号为ROOT_INO，
            // 即为1.然后重新扫描整个i节点表，以获取该被安装文件系统的根i节点信息。
            iput(inode);
            dev = super_block[i].s_dev;
            nr = ROOT_INO;
            inode = inode_table;
            continue;
        }

        // 最终我们找到了相应的i节点。因此可以放弃本函数开始处临时申请的空闲的i节点，
        // 返回找到的i节点指针。
        if (empty)
            iput(empty);
        return inode;
    }

    // 在i节点表中没有找到指定的i节点
    // 则利用前面申请的空闲i节点empty在i节点表中建立该i节点。
    // 并从相应设备上读取该i节点信息，返回该i节点指针。
    if (!empty)
        return NULL;
    inode = empty;
    inode->i_dev = (unsigned short)dev;
    inode->i_num = (unsigned short)nr;
    read_inode(inode);
    return inode;
}

//// 读取指定 i 节点信息。
// 从设备上读取含有指定i节点信息的i节点盘块，然后复制到指定的i节点结构中。
// 为了确定i节点所在的设备逻辑块号（或缓冲块），必须首先读取相应设备上的超级块，以获取用于计算逻辑
// 块号的每块i节点数信息INODES_PER_BLOCK.在计算出i节点所在的逻辑块号后，就把该逻辑块读入一缓冲块中。
// 然后把缓冲块中相应位置处的i节点内容复制到参数指定的位置处
static void read_inode(struct m_inode * inode) {
    struct super_block * sb;
	struct buffer_head * bh;
	int block;

    // 先锁定 i 节点
    lock_inode(inode);
    // 获取 i 节点的超级块
    if (!(sb = get_super(inode->i_dev)))
        panic("trying to write inode without device");

    // 该i节点所在的设备逻辑块号 ＝（启动块+超级块）+i节点位图占用的块数+逻辑块位图占用的块数
    // +（i节点号-1）/每块含有的i节点数。虽然i节点号从0开始编号，但第i个0号i节点不用，并且
    // 磁盘上也不保存对应的0号i节点结构。因此存放i节点的盘块的第i块上保存的是i节点号是1--16
    // 的i节点结构而不是0--15的。因此在上面计算i节点号对应的i节点结构所在盘块时需要减1，即：
    // B=（i节点号-1)/每块含有i节点结构数。例如，节点号16的i节点结构应该在B=（16-1）/16 = 0的
    // 块上。这里我们从设备上读取该i节点所在的逻辑块，并复制指定i节点内容到inode指针所指位置处。
    block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks + ((inode->i_num-1) / (int)INODES_PER_BLOCK);

    if (!(bh = bread(inode->i_dev, block)))
		panic("unable to read i-node block");

    *(struct d_inode *)inode =
		((struct d_inode *)bh->b_data)
			[(inode->i_num-1)%(int)INODES_PER_BLOCK];
    brelse(bh);
	unlock_inode(inode);
}

static void write_inode(struct m_inode * inode) {
    struct super_block * sb;
	struct buffer_head * bh;
	int block;

    // 先锁定 i 节点
    lock_inode(inode);
    if (!inode->i_dirt || !inode->i_dev) {
        unlock_inode(inode);
        return;
    }

    // 获取 i 节点的超级块
    if (!(sb = get_super(inode->i_dev)))
        panic("trying to write inode without device");

    // 该 i 节点所在的设备逻辑块号＝（启动块+超级块）+ i节点位图占用的块数 + 逻辑块位图占用的块数 +（i节点号-1）/ 每块含有的i节点数
    // 我们从设备上读取 i 节点所在的逻辑块，并将该 i 节点信息复制到逻辑块对应 i 节点的项位置处。
    block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks + ((inode->i_num-1)/(int)INODES_PER_BLOCK);
    if (!(bh = bread(inode->i_dev, block)))
        panic("unable to read i-node block");

    ((struct d_inode *)bh->b_data)
        [(inode->i_num-1)%(int)INODES_PER_BLOCK] =
            *(struct d_inode*)inode;

    // 然后置缓冲区已修改标志，而 i 节点内容已经与缓冲区中的一致，因此修改标志置零。
    // 然后释放该含有i节点的缓冲区，并解锁该i节点。
    bh->b_dirt = 1;
	inode->i_dirt = 0;
    brelse(bh);
    unlock_inode(inode);
}