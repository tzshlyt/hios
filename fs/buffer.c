#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
#include <serial_debug.h>


// 变量end是由编译时的连接程序ld生成，用于表明内核代码的末端，即指明内核模块某段位置。
// 也可以从编译内核时生成的System.map文件中查出。这里用它来表明高速缓冲区开始于内核
// 代码某段位置。
// buffer_wait等变量是等待空闲缓冲块而睡眠的任务队列头指针。它与缓冲块头部结构中b_wait
// 指针的租用不同。当任务申请一个缓冲块而正好遇到系统缺乏可用空闲缓冲块时，当前任务
// 就会被添加到buffer_wait睡眠等待队列中。而b_wait则是专门供等待指定缓冲块(即b_wait
// 对应的缓冲块)的任务使用的等待队列头指针。
extern int end;

struct buffer_head * start_buffer = (struct buffer_head *) &end;
struct buffer_head * hash_table[NR_HASH];           // NR_HASH ＝ 307项
static struct buffer_head * free_list;              // 空闲缓冲块链表头指针
static struct task_struct * buffer_wait = NULL;     // 等待空闲缓冲块而睡眠的任务队列

// 下面定义系统缓冲区中含有的缓冲块个数。这里，NR_BUFFERS是一个定义在linux/fs.h中的
// 宏，其值即使变量名nr_buffers，并且在fs.h文件中声明为全局变量。大写名称通常都是一个
// 宏名称，Linus这样编写代码是为了利用这个大写名称来隐含地表示nr_buffers是一个在内核
// 初始化之后不再改变的“变量”。它将在后面的缓冲区初始化函数buffer_init中被设置。
int NR_BUFFERS = 0;                                 // 系统含有缓冲区块的个数

void check_disk_change(int dev) {
	if (MAJOR(dev) != 2)
		return;
	// TODO: check whether a floppy has bean changed
}

// 下面两行代码是hash（散列）函数定义和Hash表项的计算宏
// hash表的主要作用是减少查找比较元素所花费的时间。通过在元素的存储位置与关
// 键字之间建立一个对应关系(hash函数)，我们就可以直接通过函数计算立刻查询到指定
// 的元素。建立hash函数的指导条件主要是尽量确保散列在任何数组项的概率基本相等。
// 建立函数的方法有多种，这里Linux-0.11主要采用了关键字除留余数法。因为我们
// 寻找的缓冲块有两个条件，即设备号dev和缓冲块号block，因此设计的hash函数肯定
// 需要包含这两个关键值。这两个关键字的异或操作只是计算关键值的一种方法。再对
// 关键值进行MOD运算就可以保证函数所计算得到的值都处于函数数组项范围内。
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
#define hash(dev,block) hash_table[_hashfn(dev,block)]

//// 等待指定缓冲块解锁
// 如果指定的缓冲块 bh 已经上锁就让进程不可中断地睡眠在该缓冲块的等待队列 b_wait 中。
// 在缓冲块解锁时，其等待队列上的所有进程将被唤醒。虽然是在关闭中断(cli)之后去睡眠的，
// 但这样做并不会影响在其他进程上下文中影响中断。因为每个进程都在自己的
// TSS 段中保存了标志寄存器 EFLAGS 的值，所以在进程切换时 CPU 中当前 EFLAGS 的值也随之改变。
// 使用sleep_on进入睡眠状态的进程需要用 wake_up 明确地唤醒。
static inline void wait_on_buffer(struct buffer_head * bh) {
	cli();                          // 关中断
	while (bh->b_lock)              // 如果已被上锁则进程进入睡眠，等待其解锁
		sleep_on(&bh->b_wait);
	sti();                          // 开中断
}

//// 对指定设备进行高速缓冲数据与设备上数据的同步操作
// 该函数首先搜索高速缓冲区所有缓冲块。对于指定设备dev的缓冲块，若其数据已经
// 被修改过就写入盘中(同步操作)。然后把内存中i节点表数据写入高速缓冲中。之后
// 再对指定设备dev执行一次与上述相同的写盘操作。
int sync_dev(int dev) {
	int i;
	struct buffer_head * bh;

    // 首先对参数指定的设备执行数据同步操作，让设备上的数据与高速缓冲区中的数据
    // 同步。方法是扫描高速缓冲区中所有缓冲块，对指定设备dev的缓冲块，先检测其
    // 是否已被上锁，若已被上锁就睡眠等待其解锁。然后再判断一次该缓冲块是否还是
    // 指定设备的缓冲块并且已修改过(b_dirt标志置位)，若是就对其执行写盘操作。
    // 因为在我们睡眠期间该缓冲块有可能已被释放或者被挪作他用，所以在继续执行前
    // 需要再次判断一下该缓冲块是否还是指定设备的缓冲块。
	bh = start_buffer;
	for (i = 0 ; i < NR_BUFFERS ; i++, bh++) {
		if (bh->b_dev != dev)               // 不是设备dev的缓冲块则继续
			continue;
		wait_on_buffer(bh);                     // 等待缓冲区解锁
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE, bh);
	}
    // 再将i节点数据吸入高速缓冲。让i姐电表inode_table中的inode与缓冲中的信息同步。
	sync_inodes();
    // 然后在高速缓冲中的数据更新之后，再把他们与设备中的数据同步。这里采用两遍同步
    // 操作是为了提高内核执行效率。第一遍缓冲区同步操作可以让内核中许多"脏快"变干净，
    // 使得i节点的同步操作能够高效执行。本次缓冲区同步操作则把那些由于i节点同步操作
    // 而又变脏的缓冲块与设备中数据同步。
	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}
//// 从hash队列和空闲缓冲区队列中移走缓冲块。
// hash队列是双向链表结构，空闲缓冲块队列是双向循环链表结构。
static inline void remove_from_queues(struct buffer_head * bh) {
/* remove from hash-queue */
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;

    // 如果该缓冲区是该队列的头一个块，则让 hash 表的对应项指向本队列中的下一个缓冲区。
	if (hash(bh->b_dev, bh->b_blocknr) == bh)
		hash(bh->b_dev, bh->b_blocknr) = bh->b_next;
/* remove from free list */
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");

	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;

	// 如果空闲链表头指向本缓冲区，则让其指向下一缓冲区。
	if (free_list == bh)
		free_list = bh->b_next_free;
}

//// 将缓冲块插入空闲链表尾部，同时放入hash队列中。
static inline void insert_into_queues(struct buffer_head * bh) {
/* put at end of free list */
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
    // 请注意当 hash 表某项第1次插入项时，hash()计算值肯定为 Null，因此此时得到
    // 的 bh->b_next 肯定是 NULL，所以应该在 bh->b_next 不为 NULL 时才能给 b_prev 赋bh值。
	bh->b_next = NULL;
	bh->b_prev = NULL;
	if (!bh->b_dev)
		return;

	bh->b_next = hash(bh->b_dev, bh->b_blocknr);
	hash(bh->b_dev, bh->b_blocknr) = bh;
	if (bh->b_next)
		bh->b_next->b_prev = bh;
}

//// 利用hash表在高速缓冲区中寻找给定设备和指定块号的缓冲区块。
// 如果找到则返回缓冲区块的指针，否则返回NULL。
static struct buffer_head * find_buffer(int dev, int block) {
	struct buffer_head * tmp;

    // 搜索hash表，寻找指定设备号和块号的缓冲块。
	for (tmp = hash(dev,block) ; tmp != NULL ; tmp = tmp->b_next)
		if (tmp->b_dev == (unsigned short)dev && tmp->b_blocknr == (unsigned long)block)
			return tmp;
	return NULL;
}

//// 利用hash表在高速缓冲区中寻找指定的缓冲块。若找到则对该缓冲块上锁并
// 返回块头指针。
struct buffer_head * get_hash_table(int dev, int block) {
	struct buffer_head * bh;

	for (;;) {
        // 在高速缓冲中寻找给定设备和指定块的缓冲区块，如果没有找到则返回NULL。
		if (!(bh = find_buffer(dev, block)))
			return NULL;
        // 对该缓冲块增加引用计数，并等待该缓冲块解锁。由于经过了睡眠状态，
        // 因此有必要在验证该缓冲块的正确性，并返回缓冲块头指针。
		bh->b_count++;
		wait_on_buffer(bh);
		if (bh->b_dev == (unsigned short)dev && bh->b_blocknr == (unsigned long)block)
			return bh;
        // 如果在睡眠时该缓冲块所属的设备号或块设备号发生了改变，则撤消对它的
        // 引用计数，重新寻找。
		bh->b_count--;
	}
}

// 下面宏用于同时判断缓冲区的修改标志和锁定标志，并且定义修改标志的权重要比锁定标志大。
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)
//// 取高速缓冲中指定的缓冲块
// 检查指定（设备号和块号）的缓冲区是否已经在高速缓冲中。如果指定块已经在
// 高速缓冲中，则返回对应缓冲区头指针退出；如果不在，就需要在高速缓冲中设置一个
// 对应设备号和块号的新项。返回相应的缓冲区头指针。
// getblk 返回的缓冲块可能是一个新的空闲块，也可能正好是含有我们需要数据的缓冲块，它已经存在于高速缓冲区中。
struct buffer_head * getblk(int dev, int block) {
	struct buffer_head * tmp, * bh;

repeat:
    // 搜索hash表，如果指定块已经在高速缓冲中，则返回对应缓冲区头指针，退出。
	if ((bh = get_hash_table(dev, block)))
		return bh;
    // 扫描空闲数据块链表，寻找空闲缓冲区。
    // 首先让tmp指向空闲链表的第一个空闲缓冲区头
	tmp = free_list;
	do {
        // 如果该缓冲区正被使用（引用计数不等于0），则继续扫描下一项。对于
        // b_count = 0的块，即高速缓冲中当前没有引用的块不一定就是干净的
        // (b_dirt=0)或没有锁定的(b_lock=0)。因此，我们还是需要继续下面的判断和选择。
        // 例如当一个任务该写过一块内容后就释放了，于是该块b_count()=0
        // 但b_lock不等于0；当一个任务执行breada()预读几个块时，只要ll_rw_block()
        // 命令发出后，它就会递减b_count; 但此时实际上硬盘访问操作可能还在进行，
        // 因此此时b_lock=1, 但b_count=0.
		if (tmp->b_count)
			continue;
        // 如果缓冲头指针 bh 为空，或者 tmp 所指缓冲头的标志(修改、锁定)权重小于 bh
        // 头标志的权重，则让 bh 指向 tmp 缓冲块头。如果该 tmp 缓冲块头表明缓冲块既
        // 没有修改也没有锁定标志置位，则说明已为指定设备上的块取得对应的高速
        // 缓冲块，则退出循环。否则我们就继续执行本循环，看看能否找到一个BANDNESS()
        // 最小的缓冲块。
		if (!bh || BADNESS(tmp) < BADNESS(bh)) {
			bh = tmp;
			if (!BADNESS(tmp))
				break;
		}
/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);
    // 如果循环检查发现所有缓冲块都正在被使用(所有缓冲块的头部引用计数都 > 0)中，
    // 则睡眠等待有空闲缓冲块可用。当有空闲缓冲块可用时本进程会被明确的唤醒。
    // 然后我们跳转到函数开始处重新查找空闲缓冲块。
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
    // 执行到这里，说明我们已经找到了一个比较合适的空闲缓冲块了。于是先等待该缓冲区解锁。
    // 如果在我们睡眠阶段该缓冲区又被其他任务使用的话，只好重复上述寻找过程。
	wait_on_buffer(bh);
	if (bh->b_count)
		goto repeat;
    // 如果该缓冲区已被修改，则将数据写盘，并再次等待缓冲区解锁。同样地，若该缓冲区
    // 又被其他任务使用的话，只好再重复上述寻找过程。
	while (bh->b_dirt) {
		sync_dev(bh->b_dev);
		wait_on_buffer(bh);
		if (bh->b_count)
			goto repeat;
	}
	/* NOTE!! While we slept waiting for this block, somebody else might */
	/* already have added "this" block to the cache. check it */
    // 在高速缓冲 hash 表中检查指定设备和块的缓冲块是否乘我们睡眠之际已经被加入进去。
    // 如果是的话，就再次重复上述寻找过程。
	if (find_buffer(dev, block))
		goto repeat;
	/* OK, FINALLY we know that this buffer is the only one of it's kind, */
	/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
    // 于是让我们占用此缓冲块。置引用计数为1，复位修改标志和有效(更新)标志。
	bh->b_count = 1;
	bh->b_dirt = 0;
	bh->b_uptodate = 0;
    // 从 hash 队列和空闲队列块链表中移出该缓冲区头，让该缓冲区用于指定设备和其上的指定块。
    // 然后根据此新的设备号和块号重新插入空闲链表和 hash 队列新位置处。并最终返回缓冲头指针。
	remove_from_queues(bh);
	bh->b_dev = (unsigned short)dev;
	bh->b_blocknr = (unsigned long)block;
	insert_into_queues(bh);
	return bh;
}

// 释放指定缓冲块。
// 等待该缓冲块解锁。然后引用计数递减1，并明确地唤醒等待空闲缓冲块的进程。
void brelse(struct buffer_head * buf) {
	if (!buf)
		return;
	wait_on_buffer(buf);
	if (!(buf->b_count--))
		panic("Trying to free free buffer");
	wake_up(&buffer_wait);
}


//// 从设备上读取数据块。
// 该函数根据指定的设备号 dev 和数据块号 block，首先在高速缓冲区中申请一块缓冲块。
// 如果该缓冲块中已经包含有有效的数据就直接返回该缓冲块指针，否则
// 就从设备中读取指定的数据块到该缓冲块中并返回缓冲块指针。
struct buffer_head * bread(int dev,int block) {
	struct buffer_head * bh;

    // 在高速缓冲区中申请一块缓冲块。如果返回值是NULL，则表示内核出错，停机。
    // 然后我们判断其中是否已有可用数据。如果该缓冲块中数据是有效的（已更新）可以直接使用，则返回。
	if (!(bh = getblk(dev, block)))
		panic("bread: getblk returned NULL\n");
	if (bh->b_uptodate)			// 数据有效
		return bh;
    // 否则我们就调用底层快设备读写ll_rw_block函数，产生读设备块请求。
    // 然后等待指定数据块被读入，并等待缓冲区解锁。在睡眠醒来之后，如果该缓冲区已更新，
    // 则返回缓冲区头指针，退出。否则表明读设备操作失败，于是释放该缓冲区，
    // 返回 NULL，退出。
	ll_rw_block(READ, bh);
	wait_on_buffer(bh);
	if (bh->b_uptodate)
		return bh;
	brelse(bh);
	return NULL;
}

void buffer_init(unsigned long buffer_end) {
    struct buffer_head * h = start_buffer;
    s_printk("buffer_init at [%u, %u]\n", start_buffer, buffer_end);
	void * b;
	int i;

// 首先根据参数提供的缓冲区高端位置确定实际缓冲区高端位置b。如果缓冲区高端等于1Mb，
    // 则因为从640KB - 1MB被显示内存和BIOS占用，所以实际可用缓冲区内存高端位置应该是
    // 640KB。否则缓冲区内存高端一定大于1MB。
	if (buffer_end == 1<<20)
		b = (void *) (640*1024);
	else
		b = (void *) buffer_end;
    // 这段代码用于初始化缓冲区，建立空闲缓冲区块循环链表，并获取系统中缓冲块数目。
    // 操作的过程是从缓冲区高端开始划分1KB大小的缓冲块，与此同时在缓冲区低端建立
    // 描述该缓冲区块的结构buffer_head,并将这些buffer_head组成双向链表。
    // h是指向缓冲头结构的指针，而h+1是指向内存地址连续的下一个缓冲头地址，也可以说
    // 是指向h缓冲头的末端外。为了保证有足够长度的内存来存储一个缓冲头结构，需要b所
    // 指向的内存块地址 >= h 缓冲头的末端，即要求 >= h+1.
	while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
		h->b_dev = 0;                       // 使用该缓冲块的设备号
		h->b_dirt = 0;                      // 脏标志，即缓冲块修改标志
		h->b_count = 0;                     // 缓冲块引用计数
		h->b_lock = 0;                      // 缓冲块锁定标志
		h->b_uptodate = 0;                  // 缓冲块更新标志(或称数据有效标志)
		h->b_wait = NULL;                   // 指向等待该缓冲块解锁的进程
		h->b_next = NULL;                   // 指向具有相同hash值的下一个缓冲头
		h->b_prev = NULL;                   // 指向具有相同hash值的前一个缓冲头
		h->b_data = (char *) b;             // 指向对应缓冲块数据块（1024字节）
		h->b_prev_free = h-1;               // 指向链表中前一项
		h->b_next_free = h+1;               // 指向连表中后一项
		h++;                                // h指向下一新缓冲头位置
		NR_BUFFERS++;                       // 缓冲区块数累加
		if (b == (void *) 0x100000)         // 若b递减到等于1MB，则跳过384KB
			b = (void *) 0xA0000;           // 让b指向地址0xA0000(640KB)处
	}
	h--;                                    // 让h指向最后一个有效缓冲块头
	free_list = start_buffer;               // 让空闲链表头指向头一个缓冲快
	free_list->b_prev_free = h;             // 链表头的b_prev_free指向前一项(即最后一项)。
	h->b_next_free = free_list;             // h的下一项指针指向第一项，形成一个环链
    // 最后初始化hash表，置表中所有指针为NULL。
	for (i=0;i<NR_HASH;i++)
		hash_table[i]=NULL;
}