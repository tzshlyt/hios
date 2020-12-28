// 处理文件系统的逻辑块位图和i节点位图
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

//// 复位指定地址开始的第nr位偏移处的bit位。返回原bit位值的反码。
// 输入：%0-eax(返回值)；%1-eax(0)；%2-nr,位偏移值；%3-(addr)，addr的内容。
// btrl指令用于测试并复位bit位。其作用与上面的btsl类似，但是复位指定bit位。
// 指令setnb用于根据进位标志CF设置操作数(%al).如果CF=1则%al=0,否则%al=1.
#define clear_bit(nr, addr) ({\
register int res ; \
__asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

//// 释放设备dev上数据区中的逻辑块block.
// 复位指定逻辑块block对应的逻辑块位图bit位
// 参数：dev是设备号，block是逻辑块号（盘块号）
void free_block(int dev, int block) {
    struct super_block * sb;
	struct buffer_head * bh;

    // 断参数 block 的有效性
    if (!(sb = get_super(dev)))
		panic("trying to free block on nonexistent device");
	if (block < sb->s_firstdatazone || block >= sb->s_nzones)
		panic("trying to free block not in datazone");

    // 然后从hash表中寻找该块数据
    // 若找到了则判断其有效性，并清已修改和更新标志，释放该数据块
    // 该段代码的主要用途是如果该逻辑块目前存在于高速缓冲区中，就释放对应的缓冲块。
    bh = get_hash_table(dev, block);
    if (bh) {
        if (bh->b_count != 1) {
			printk("trying to free block (%x:%d), count=%d\n",
                dev, block, bh->b_count);
			return;
		}
        bh->b_dirt = 0;
		bh->b_uptodate = 0;
		brelse(bh);
    }

    // 接着我们复位block在逻辑块位图中的bit（置0），先计算block在数据区开始算起的数据逻辑块号(从1开始计数)。
    // 然后对逻辑块(区块)位图进行操作，复位对应的bit位。
    // 如果对应bit位原来就是0，则出错停机。由于1个缓冲块有1024字节，即8192比特位，
    // 因此block/8192即可计算出指定块block在逻辑位图中的哪个块上
    // 而block&8192可以得到block在逻辑块位图 当前块中的bit偏移位置。
    block -= sb->s_firstdatazone - 1;
    if (clear_bit(block&8191, sb->s_zmap[block/8192]->b_data)) {
		printk("block (%x:%d) ",dev, block+sb->s_firstdatazone-1);
		panic("free_block: bit already cleared");
	}

    // 最后置相应逻辑块位图所在缓冲区已修改标志。
	sb->s_zmap[block/8192]->b_dirt = 1;
}

//// 向设备申请一个逻辑块。
// 函数首先取得设备的超级块，并在超级块中的逻辑块位图中寻找第一个0值bit位(代表一个
// 空闲逻辑块)。然后位置对应逻辑块在逻辑块位图中的bit位。接着为该逻辑块在缓冲区中取得
// 一块对应缓冲块。最后将该缓冲块清零，并设置其已更新标志和已修改标志。并返回逻辑块
// 号。函数执行成功则返回逻辑块号，否则返回0.
int new_block(int dev) {
    int j;
    return j;
}

//// 释放指定的i节点
// 该函数首先判断参数给出的i节点号的有效性和可释放性。若i节点仍然在使用中则不能被释放。
// 然后利用超级块信息对i节点位图进行操作，复位i节点号对应的i节点位图中bit位，并清空i节点结构。
void free_inode(struct m_inode * inode) {
    struct super_block * sb;
	struct buffer_head * bh;

    // 检测有效性
    if (!inode)
		return;
    if (!inode->i_dev) {                    // 未使用
		memset(inode, 0, sizeof(*inode));
		return;
	}

    // 还有其他程序引用，则不能释放，说明内核有问题
	if (inode->i_count > 1) {
		printk("trying to free inode with count=%d\n",inode->i_count);
		panic("free_inode");
	}

    // 连接数不为0，则表示还有其他文件目录项在使用该节点，因此也不应释放，而应该放回等
    if (inode->i_nlinks)
		panic("trying to free inode with links");

    if (!(sb = get_super(inode->i_dev)))
		panic("trying to free inode on nonexistent device");
	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)       // 0 号节点保留没有使用
		panic("trying to free inode 0 or nonexistant inode");

    if (!(bh = sb->s_imap[inode->i_num>>13]))
		panic("nonexistent imap in superblock");

    // 现在我们复位i节点对应的节点位图中的bit位。如果该bit位已经等于0，则显示出错警告信息。
    if (clear_bit(inode->i_num&8191, bh->b_data))
		printk("free_inode: bit already cleared.\n\r");
	bh->b_dirt = 1;                                             // 置i节点位图所在缓冲区已修改置位
	memset(inode, 0, sizeof(*inode));
}

//// 为设备dev建立一个新i节点。初始化并返回该新i节点的指针。
// 在内存i节点表中获取一个空闲i节点表项，并从i节点位图中找一个空闲i节点。
struct m_inode * new_inode(int dev) {
    struct m_inode * inode;
    return inode;
}