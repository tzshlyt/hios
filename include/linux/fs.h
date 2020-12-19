// 文件系统中的一些常数和结构
#ifndef _FS_H
#define _FS_H

#include <sys/types.h>

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)
 * 1 - /dev/mem
 * 2 - /dev/fd
 * 3 - /dev/hd
 * 4 - /dev/ttyx
 * 5 - /dev/tty
 * 6 - /dev/lp
 * 7 - unnamed pipes
 */

#define IS_SEEKABLE(x) ((x)>=1 && (x)<=3)	// 判断设备是否是可以寻找定位的

#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead - don't pause */
#define WRITEA 3	/* "write-ahead" - silly, but somewhat useful */

void buffer_init(unsigned long buffer_end);

#define MAJOR(a) (((unsigned)(a))>>8)		// 主设备号
#define MINOR(a) ((a)&0xff)					// 次设备号

#define NAME_LEN 14							// 名字长度值
#define ROOT_INO 1							// 根 i 节点

#define I_MAP_SLOTS 8						// i 节点位图槽数
#define Z_MAP_SLOTS 8						// 逻辑块（区段块）位图槽数
#define SUPER_MAGIC 0x137F					// 文件系统魔法数

#define NR_OPEN 20							// 打开文件数
#define NR_INODE 32							// 系统同时最多使用 I 节点个数
#define NR_FILE 64							// 系统最多文件个数（文件数组项数）
#define NR_SUPER 8							// 系统所含超级块个数（超级块数组项数）
#define NR_HASH 307							// 缓冲区Hash表数组项数
#define NR_BUFFERS nr_buffers				// 系统所含缓冲块个数。初始化后不再改变
#define BLOCK_SIZE 1024						// 数据块长度（字节值）
#define BLOCK_SIZE_BITS 10					// 数据块长度所占比特位数
#ifndef NULL
#define NULL ((void *) 0)
#endif

// 缓冲块头数据结构
struct buffer_head {
	char * b_data;			    /* pointer to data block (1024 bytes) */	// 指针
	unsigned long b_blocknr;	/* block number */							// 块号
	unsigned short b_dev;		/* device (0 = free) */						// 数据源的设备号
	unsigned char b_uptodate;												// 更新标志
	unsigned char b_dirt;		/* 0-clean,1-dirty */						// 修改标志
	unsigned char b_count;		/* users using this block */				// 使用的用户数
	unsigned char b_lock;		/* 0 - ok, 1 -locked */						// 缓冲区是否被锁定
	struct task_struct * b_wait;											// 指向等待该缓冲区解锁的任务
	struct buffer_head * b_prev;											// hash 队列上前一块（这4个指针用于缓冲区的管理）
	struct buffer_head * b_next;
	struct buffer_head * b_prev_free;										// 空闲表上前一块
	struct buffer_head * b_next_free;
};

extern int nr_buffers;


#endif