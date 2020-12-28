// 对文件系统中超级块的操作
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <serial_debug.h>
#include <linux/fs.h>

// 对指定设备执行高速缓冲与设备上数据的同步操作
int sync_dev(int dev);
// 等待击键
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
//// 测试指定位偏移处bit位的值，并返回该原bit位值。
// 嵌入式汇编宏，参数bitnr是bit位偏移值，addr是测试bit位操作的起始地址。
// %0 - ax(__res), %1 - 0, %2 - bitnr, %3 - addr, 下面一行定义了一个寄存器变量。
// 该变量将被保存在eax寄存器中，以便于高效访问和操作，指令bt用于对bit位进行测试，它
// 会把地址addr（%3）和bit位偏移量bitnr（%2）指定的bit位的值放入进位标志CF中，
// 指令setb用于根据进位标志CF设置操作数%al.如果CF=1，则%al=1,否则%al=0.
#define set_bit(bitnr, addr) ({ \
register int __res ; \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

// 超级块结构表数组（NR_SUPER = 8）, 最多加载8个文件系统
struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;       // 根文件系统设备号

//// 锁定超级块
static void lock_super(struct super_block * sb) {
    cli();
    while (sb->s_lock)
        sleep_on(&(sb->s_wait));
    sb->s_lock = 1;
    sti();
}

//// 对指定超级块解锁
static void free_super(struct super_block * sb) {
    cli();
    sb->s_lock = 0;
    wake_up(&(sb->s_wait));
    sti();
}

//// 睡眠等待超级解锁
static void wait_on_supper(struct super_block * sb) {
    cli();
    while(sb->s_lock)
        sleep_on(&(sb->s_wait));
    sti();
}

//// 取指定设备的超级块
// 在超级块表（数组）中搜索指定设备dev的超级块结构信息。
// 若找到刚返回超级块的指针，否则返回空指针。
struct super_block * get_super(int dev) {
    struct super_block * s;

    // 设备为 0 则返回 NULL
    if (!dev)
        return NULL;

    // 搜索整个超级块数组
    s = 0+super_block;
    while(s < NR_SUPER+super_block) {
        if (s->s_dev == dev) {
            wait_on_supper(s);          // 先等待解锁，在等待期间，可能被其它设备使用，再次判断
            if (s->s_dev == dev)
                return s;
            s = 0+super_block;          // 被其它设备使用了，重新开始搜索
        } else {
            s++;
        }
    }
    return NULL;
}

//// 读取指定设备的超级块
// 如果指定设备dev上的文件系统超级块已经在超级块表中，则直接返回该超级块项的指针。
// 否则就从设备dev上读取超级块到缓冲块中，并复制到超级块中。并返回超级块指针。
static struct super_block * read_super(int dev) {
    struct super_block * s;
	struct buffer_head * bh;
	int i, block;

    // 判断参数有效性
    if (!dev)
        return NULL;
    // 然后检查该设备是否可更换过盘片（也即是否软盘设备）
    // 如果更换盘片，则高速缓冲区有关设备的所有缓冲块均失效，
    // 需要进行失效处理，即释放原来加载的文件系统。
    check_disk_change(dev);
    // 如果该设备的超级块已经在超级块表中，则直接返回该超级块的指针。
    // 否则，首先在超级块数组中找出一个空项（也即字段s_dev=0的项）。如果数组已经占满则返回空指针。
    if ((s = get_super(dev)))
        return s;
    for (s = 0+super_block ;; s++) {
        if (s >= NR_SUPER+super_block)
            return NULL;            // 没有空项，返回 NULL
        if (!s->s_dev)              // 找到一个空项
            break;
    }
    // 对该空项超级块初始化
    s->s_dev = (unsigned short)dev;
    s->s_isup = NULL;
    s->s_imount = NULL;
    s->s_time = 0;
    s->s_rd_only = 0;
    s->s_dirt = 0;
    lock_super(s);
    if (!(bh = bread(dev, 1))) {            // 超级块位于第2个逻辑块（1号块）中，第1个是引导块
        s->s_dev = 0;
        free_super(s);
        return NULL;
    }
    // 将设备上读取的超级块信息从缓冲块数据区复制到超级块数组相应项结构中
    *( (struct d_super_block *) s) = *( (struct d_super_block *) bh->b_data);
    brelse(bh);
    // 现在我们从设备dev上得到了文件系统的超级块
    if (s->s_magic != SUPER_MAGIC) {        // 对于该版Linux内核，只支持MINIX文件系统1.0版本，其魔数是0x1371
        s->s_dev = 0;
        free_super(s);
        return NULL;
    }
    // 下面开始读取设备上i节点的位图和逻辑块位图数据
    // 首先初始化内存超级块结构中位图空间
    // 然后从设备上读取i节点位图和逻辑块位图信息，并存放在超级块对应字段中。
    // i节点位图保存在设备上2号块开始的逻辑块中，共占用s_imap_blocks个块，
    // 逻辑块位图在i节点位图所在块的后续块中，共占用s_zmap_blocks个块。
    for (i = 0; i < I_MAP_SLOTS; i++) {
        s->s_imap[i] = NULL;
    }
    for (i = 0; i < Z_MAP_SLOTS; i++) {
        s->s_zmap[i] = NULL;
    }
    block = 2;
    for (i = 0; i < s->s_imap_blocks; i++) {
        if ((s->s_imap[i] = bread(dev, block)))
            block++;
        else
            break;
    }
    for (i = 0; i < s->s_zmap_blocks; i++) {
        if ((s->s_zmap[i] = bread(dev, block)))
            block++;
        else
            break;
    }
    // 如果读出的位图块数不等于位图应该占有的逻辑块数，说明文件系统位图信息有问题
    // 因此只能释放前面申请并占用的所有资源
    if (block != 2 + s->s_imap_blocks + s->s_zmap_blocks) {
        for(i = 0; i < I_MAP_SLOTS; i++)
			brelse(s->s_imap[i]);
		for(i = 0; i < Z_MAP_SLOTS; i++)
			brelse(s->s_zmap[i]);
        s->s_dev = 0;
        free_super(s);
        return NULL;
    }
    // 否则一切成功，另外，由于对申请空闲i节点的函数来讲，如果设备上所有的i节点已经全被使用
    // 则查找函数会返回0值。因此0号i节点是不能用的，所以这里将位图中第1块的最低bit位设置为1，
    // 以防止文件系统分配0号i节点。同样的道理，也将逻辑块位图的最低位设置为1.
    // 最后函数解锁该超级块，并放回超级块指针。
    s->s_imap[0]->b_data[0] |= 1;
    s->s_zmap[0]->b_data[0] |= 1;
    free_super(s);
    return s;
}

//// 安装根文件系统
// 该函数属于系统初始化操作的一部分。函数首先初始化文件表数组file_table[]和超级块表（数组）
// 然后读取根文件系统超级块，并取得文件系统根i节点。最后统计并显示出根文件系统上的可用资源（空闲块数和空闲i节点数）
// 该函数会在系统开机进行初始化设置时被调用。
void mount_root(void) {
    s_printk("mount_root()\n");
    int i, free;
    struct super_block *p;
    struct m_inode * mi;

    // 若磁盘i节点结构不是32字节，则出错停机。该判断用于防止修改代码时出现不一致情况。
    if (32 != sizeof(struct d_inode))
        panic("bad i-node size");

    // 首先初始化文件表数组（共64项，即系统同时只能打开64个文件）和超级块表。
    // 这里将所有文件结构中的引用计数设置为0（表示空闲），
    // 并把超级块表中各项结构的设备字段初始化为0（也表示空闲）。
    // 如果根文件系统所在设备是软盘的话，就提示“插入根文件系统盘，并按回车键”，并等待按键。
    for (i = 0; i < NR_FILE; i++) {
        file_table[i].f_count = 0;
    }
    if (MAJOR(ROOT_DEV) == 2) {
        printk("Insert root floppy and press ENTER\n");
        wait_for_keypress();
    }
    for(p = &super_block[0]; p < &super_block[NR_SUPER]; p++) {
        p->s_dev = 0;
        p->s_lock = 0;
        p->s_wait = NULL;
    }

    // 初始化工作之后，我们开始安装根文件系统。
    // 于是从根设备上读取文件系统超级块，
    // 并取得文件系统的根i节点（1号节点）在内存i节点表中的指针。
    // 如果读根设备上超级块失败或取根节点失败，则都显示信息并停机。
    if (!(p = read_super(ROOT_DEV)))
        panic("Unable to mount root");
    if (!(mi = iget(ROOT_DEV, ROOT_INO)))
        panic("Unable to read root i-node");
    // 现在我们对超级块和根i节点进行设置。
    // 把根i节点引用次数递增3次。
    // 因为后面  p->s_isup = p->s_imount = mi; 行上也引用了该i节点。
    // 另外，iget()函数中i节点引用计数已被设置为 1。
    // 然后置该超级块的被安装文件系统i节点和被安装到i节点字段为该i节点。
    // 再设置当前进程的当前工作目录和根目录i节点。
    // 此时当前进程是1号进程（init进程）。
    mi->i_count += 3;   	/* NOTE! it is logically used 4 times, not 1 */
    p->s_isup = p->s_imount = mi;
    current->pwd = mi;
    current->root = mi;
    // 然后我们对根文件系统的资源作统计工作。
    // 统计该设备上空闲块数 和 空闲i节点数。
    // 首先令i等于超级块中表明的设备逻辑块总数。然后根据逻辑块相应bit位的占用情况统计出空闲块数。
    // 这里宏函数set_bit()只是在测试bit位，而非设置bit位。
    // “i&8191”用于取得i节点号在当前位图块中对应的bit位偏移值。
    // "i>>13"是将i除以8192，也即除一个磁盘块包含的bit位数。
    free = 0;
    i = p->s_nzones;
    while (--i >= 0) {
		if (!set_bit(i&8191, p->s_zmap[i>>13]->b_data))
			free++;
    }
    printk("%d/%d free blocks\n",free,p->s_nzones);
    // 我们再统计设备上空闲i节点数
    // 首先令i等于超级块中表明的设备上i节点总数+1. 加1是将0节点也统计进去，
    // 然后根据i节点位图相应bit位的占用情况计算出空闲i节点数。
    free=0;
	i = p->s_ninodes+1;
	while (--i >= 0) {
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
    }
	printk("%d/%d free inodes\n",free,p->s_ninodes);
}
