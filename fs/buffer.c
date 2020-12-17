#include <linux/fs.h>
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

// 下面定义系统缓冲区中含有的缓冲块个数。这里，NR_BUFFERS是一个定义在linux/fs.h中的
// 宏，其值即使变量名nr_buffers，并且在fs.h文件中声明为全局变量。大写名称通常都是一个
// 宏名称，Linus这样编写代码是为了利用这个大写名称来隐含地表示nr_buffers是一个在内核
// 初始化之后不再改变的“变量”。它将在后面的缓冲区初始化函数buffer_init中被设置。
int NR_BUFFERS = 0;                                 // 系统含有缓冲区块的个数

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