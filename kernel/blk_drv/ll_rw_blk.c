// 低层块设备读/写操作
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <linux/fs.h>
#include "blk.h"

// 请求项数组队列, 32 个
struct request request[NR_REQUEST];

// 用于在请求数组没有空闲项时进程的临时等待处
struct task_struct * wait_for_request = NULL;

/*  blk_dev_struct is:
 *	do_request-address
 *	next-request
 */
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */
	{ NULL, NULL },		/* dev mem */
	{ NULL, NULL },		/* dev fd */
	{ NULL, NULL },		/* dev hd */
	{ NULL, NULL },		/* dev ttyx */
	{ NULL, NULL },		/* dev tty */
	{ NULL, NULL }		/* dev lp */
};

// 锁定指定缓冲块。
// 如果指定的缓冲块已经被其他任务锁定，则使自己睡眠(不可中断地等待)，直到被执行解锁缓冲块的任务明确地唤醒。
static inline void lock_buffer(struct buffer_head * bh) {
	cli();
	while (bh->b_lock)                      // 如果缓冲区已被锁定则睡眠，直到缓冲区解锁
		sleep_on(&bh->b_wait);
	bh->b_lock = 1;                         // 立刻锁定该缓冲区
	sti();
}

static inline void unlock_buffer(struct buffer_head * bh) {
	if (!bh->b_lock)
		printk("ll_rw_block.c: buffer not locked\n\r");
	bh->b_lock = 0;
	wake_up(&bh->b_wait);
}

// 向链表中加入请求项
// 参数dev是指定块设备结构指针，该结构中有处理请求项函数指针和当前正在请求项指针; req是已设置好内容的请求项结构指针。
// 本函数把已经设置好的请求项req添加到指定设备的请求项链表中。如果该设备的当前请求请求项指针为空，则可以设置req为当前请求项并立刻调用设备请求项处理函数。
// 否则就把req请求项插入到该请求项链表中。
static void add_request(struct blk_dev_struct *dev, struct request *req) {
    struct request *tmp;

    req->next = NULL;
    cli();
    if (req->bh)
        req->bh->b_dirt = 0;                    // 清缓冲区脏标志
    if (!(tmp = dev->current_request)) {        // 设备是否正忙
        dev->current_request = req;             // 本次是第1个请求项
        sti();
        (dev->request_fn)();                    // 执行请求函数, 对于硬盘是 do_hd_request()
        return;
    }
    // 如果目前该设备已经有当前请求项在处理，则首先利用电梯算法搜索最佳插入位置，然后将当前请求插入到请求链表中。
    // 最后开中断并退出函数。电梯算法的作用是让磁盘磁头的移动距离最小，从而改善(减少)硬盘访问时间。
    // 下面 for 循环中 if 语句用于把 req 所指请求项与请求队列(链表)中已有的请求项作比较，找出 req 插入该队列的正确位置顺序。
    // 然后中断循环，并把req插入到该队列正确位置处。
    for (; tmp->next; tmp=tmp->next) {
        if ((IN_ORDER(tmp, req) ||
            !IN_ORDER(tmp, tmp->next)) &&
            IN_ORDER(req, tmp->next))
                break;
    }
    req->next = tmp->next;
    tmp->next = req;
    sti();
}

// 创建请求项并插入请求队列中
static void make_request(int major, int rw, struct buffer_head * bh) {
	struct request * req;
	int rw_ahead;               // 逻辑值，用于判断是否为 READA 或 WRITEA 命令

// WRITEA/READA是一种特殊情况一它们并非必要，所以如果缓冲区已经上锁，
// 我们就不用管它，否则的话它只是一个一般的读操作。
// 这里'READ’和’ WRITE’后面的’A'字符代表英文单词Ahead，表示提前预读/写数据块的意思。
// 该函数首先对命令READA/WRITEA的情况进行一些处理。对于这两个命令，当指定的缓冲区正在使用而已被上锁时，就放弃预读/写请求。
// 否则就作为普通的READ/WRITE命令进行操作。另外，如果参数给出的命令既不是READ也不是WRITE，则表示内核程序有错，显示出错信息并停机。
// 注意，在修改命令之前这里已为参数是否是预读/写命令设置了标志rw_ahead。
	if ((rw_ahead = (rw == READA || rw == WRITEA))) {
		if (bh->b_lock)
			return;
		if (rw == READA)
			rw = READ;
		else
			rw = WRITE;
	}
	if (rw != READ && rw != WRITE)
		panic("Bad block dev command, must be R/W/RA/WA");
    // 对命令rw进行了一番处理之后，现在只有READ或WRITE两种命令。
    // 在开始生成和添加相应读/写数据请求项之前，我们再来看看此次是否有必要添加请求项。在两种情况下可以不必添加请求项。
    // 一是当命令是写(WRITE)，但缓冲区中的数据在读入之后并没有被修改过;
    // 二是当命令是读(READ)，但缓冲区中的数据已经是更新过的，即与块设备上的完全一样。
    // 因此这里首先锁定缓冲区对其检查一下。如果此时缓冲区已被上锁，则当前任务就会睡眠，直到被明确地唤醒。
    // 如果确实是属于上述两种情况，那么就可以直接解锁缓冲区，并返回。
    // 这几行代码体现了高速缓冲区的用意，在数据可靠的情况下就无须再执行硬盘操作，而直接使用内存中的现有数据。
    // 缓冲块的 b_dirt 标志用于表示缓冲块中的数据是否已经被修改过。
    // b_uptodate 标志用于表示缓冲块中的数据是与块设备上的同步，即在从块设备上读入缓冲块后没有修改过。
	lock_buffer(bh);
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
repeat:
// 我们不能让队列中全都是写请求项:我们需要为读请求保留一些空间:读操作是优先的。请求队列的后三分之一空间仅用于读请求项。
// 现在我们必须为本函数生成并添加读/写请求项了。
// 首先我们需要在请求数组中寻找到一个空闲项(槽)来存放新请求项。搜索过程从请求数组末端开始。
// 根据上述要求，对于读命令请求，我们直接从队列末尾开始搜索，而对于写请求就只能从队列2/3处向队列头处搜索空项填入。
// 于是我们开始从后向前搜索，当请求结构 request 的设备字段 dev 值 =-1 时，表示该项未被占用(空闲)。
// 如果没有一项是空闲的(此时请求项数组指针已经搜索越过头部)，则查看此次请求是否是提前读/写(EADA或WRITEA)，如果是则放弃此次请求操作。
// 否则让本次请求操作先睡眠(以等待请求队列腾出空项)，过一会再来搜索请求队列。
	if (rw == READ)
		req = request + NR_REQUEST;                     // 对于读请求
	else
		req = request + ((NR_REQUEST*2)/3);             // 对于写请求
/* find an empty request */
	while (--req >= request)
		if (req->dev < 0)
			break;
/* if none found, sleep on new requests: check for rw_ahead */
	if (req < request) {
		if (rw_ahead) {
			unlock_buffer(bh);
			return;
		}
		sleep_on(&wait_for_request);
		goto repeat;
	}
/* fill up the request-info, and add it to the queue */
// 向空闲请求项中填写请求信息，并将其加入队列中
// 程序执行到这里表示已找到一个空闲的请求项。
// 设置好新请求项后调用 add_request() 把它添加到请求队列中。
	req->dev = bh->b_dev;
	req->cmd = rw;
	req->errors=0;
	req->sector = bh->b_blocknr<<1;     // 起始扇区。块号转换成扇区号（1块 = 2扇区）
	req->nr_sectors = 2;                // 本请求项需要读写的扇区数
	req->buffer = bh->b_data;           // 请求项缓冲区指针指向需要读写的数据缓冲区
	req->waiting = NULL;                // 任务等待操作执行完成的地方
	req->bh = bh;                       // 缓冲块头指针
	req->next = NULL;                   // 指向下一项请求
	add_request(major+blk_dev, req);
}

// 低层读写数据块
void ll_rw_block(int rw, struct buffer_head * bh) {
	int major;

	if ((major = MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
	    !(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	make_request(major, rw, bh);
}

// 块设备初始化函数，由初始化程序main.c调用
// 初始化请求数组，将所有请求项置为空闲（dev = -1）,有32项（NR_REQUEST = 32）
void blk_dev_init(void) {
	int i;

	for (i = 0; i < NR_REQUEST; i++) {
		request[i].dev = -1;
		request[i].next = NULL;
	}
}