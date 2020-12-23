// 对文件系统中超级块的操作
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <serial_debug.h>
#include <linux/fs.h>

// 超级块结构表数组（NR_SUPER = 8）
// 最多加载8个文件系统
struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;       // 根文件系统设备号

static void lock_super(struct super_block * sb) {
    cli();
    while (sb->s_lock)
        sleep_on(&(sb->s_wait));
    sb->s_lock = 1;
    sti();
}

static void free_super(struct super_block * sb) {
    cli();
    sb->s_lock = 0;
    wake_up(&(sb->s_wait));
    sti();
}

static void wait_on_supper(struct super_block * sb) {
    cli();
    while(sb->s_lock)
        sleep_on(&(sb->s_wait));
    sti();
}

struct super_block * get_super(int dev) {
    struct super_block * s;

    if (!dev)
        return NULL;

    s = 0+super_block;
    while(s < NR_SUPER+super_block) {
        if (s->s_dev == dev) {
            wait_on_supper(s);
            if (s->s_dev == dev)
                return s;
            s = 0+super_block;
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
    // 如果该设备的超级块已经在超级块表中，则直接返回该超级块的指针。否则，首先在超级块
    // 数组中找出一个空项（也即字段s_dev=0的项）。如果数组已经占满则返回空指针。
    if ((s = get_super(dev)))
        return s;
    for (s = 0+super_block ;; s++) {
        if (s >= NR_SUPER+super_block)
            return NULL;
        if (!s->s_dev)
            break;
    }
    // 对该超级块初始化
    s->s_dev = dev;
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
    *( (struct d_super_block *) s) = *( (struct d_super_block *) bh->b_data);
    brelse(bh);
    if (s->s_magic != SUPER_MAGIC) {        // 对于该版Linux内核，只支持MINIX文件系统1.0版本，其魔数是0x1371
        s->s_dev = 0;
        free_super(s);
        return NULL;
    }
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
    // 以防止文件系统分配0号i节点。同样的道理，也将逻辑块位图的最低位设置为1.最后函数解锁该
    // 超级块，并放回超级块指针。
    s->s_imap[0]->b_data[0] |= 1;
    s->s_zmap[0]->b_data[0] |= 1;
    free_super(s);
    return s;
}

// 安装根文件系统
void mount_root(void) {
    s_printk("mount_root()\n");
    int i, free;
    struct super_block *p;

    if (32 != sizeof(struct d_inode))
        panic("bad i-node size");

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

    if (!(p = read_super(ROOT_DEV)))
        panic("Unable to mount root");
}
