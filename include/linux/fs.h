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

// 磁盘上的索引节点（i 节点）数据结构
struct d_inode {
	unsigned short i_mode;													// 文件类型和属性（rwx位）
	unsigned short i_uid;													// 用户id（文件拥有者标识符）
	unsigned long i_size;													// 文件大小（字节数）
	unsigned long i_time;													// 修改时间
	unsigned char i_gid;													// 组id
	unsigned char i_nlinks;													// 链接数（多少个文件目录项指向该i节点）
	unsigned short i_zone[9];												// 直接（0-6）、间接（7）或双重间接（8）逻辑块号
};

// 内存中 i 节点结构
struct m_inode {
	unsigned short i_mode;
	unsigned short i_uid;
	unsigned long i_size;
	unsigned long i_mtime;
	unsigned char i_gid;
	unsigned char i_nlinks;
	unsigned short i_zone[9];
/* these are in memory also */
	struct task_struct * i_wait;											// 等待该 i 节点的进程
	unsigned long i_atime;													// 最后访问时间
	unsigned long i_ctime;													// i 节点自身修改时间
	unsigned short i_dev;													// i 节点所在的设备号
	unsigned short i_num;													// i 节点号
	unsigned short i_count;													// i 节点被使用的次数，0表示该i节点空闲
	unsigned char i_lock;													// 锁定标志
	unsigned char i_dirt;													// 已修改（脏）标志
	unsigned char i_pipe;													// 管道标志
	unsigned char i_mount;													// 安装标志
	unsigned char i_seek;													// 搜索标志（lseek时）
	unsigned char i_update;													// 更新标志
};

// 文件结构
struct file {
	unsigned short f_mode;													// 文件操作模式（RW位）
	unsigned short f_flags;													// 文件打开和控制标志
	unsigned short f_count;													// 对应文件引用计数值
	struct m_inode * f_inode;												// 指向对应 i 节点
	off_t f_pos;															// 文件位置（读写偏移值）
};

// 内存中磁盘超级块结构
struct super_block {
	unsigned short s_ninodes;												// 节点数
	unsigned short s_nzones;												// 逻辑块数
	unsigned short s_imap_blocks;											// i 节点位图所占用的数据块数
	unsigned short s_zmap_blocks;											// 逻辑块位图所占用的数据块数
	unsigned short s_firstdatazone;											// 第一个数据逻辑块号
	unsigned short s_log_zone_size;											// log（数据块数/逻辑块）以2为底
	unsigned long s_max_size;												// 文件最大长度
	unsigned short s_magic;													// 文件系统魔数
/* These are only in memory */
	struct buffer_head * s_imap[8];											// i 节点位图缓冲块指针数组（占用8块，可表示64M）1块1024字节可代表8k个盘块
	struct buffer_head * s_zmap[8];											// 逻辑块位图缓冲块指针数组
	unsigned short s_dev;													// 超级块所在的设备号
	struct m_inode * s_isup;												// 被安装的文件系统根目录的 i 节点（super i）
	struct m_inode * s_imount;												// 被安装到 i 节点
	unsigned long s_time;													// 修改时间
	struct task_struct * s_wait;											// 等待该超级块的进程
	unsigned char s_lock;													// 被锁定标志
	unsigned char s_rd_only;												// 只读标志
	unsigned char s_dirt;													// 已修改（脏）标志
};

// 磁盘上超级块结构
struct d_super_block {
	unsigned short s_ninodes;
	unsigned short s_nzones;
	unsigned short s_imap_blocks;
	unsigned short s_zmap_blocks;
	unsigned short s_firstdatazone;
	unsigned short s_log_zone_size;
	unsigned long s_max_size;
	unsigned short s_magic;
};

// 文件目录项结构
struct dir_entry {
	unsigned short inode;													// i 节点号
	char name[NAME_LEN];													// 文件名，长度 NAME_LEN=14
};

extern struct file file_table[NR_FILE];
extern int nr_buffers;

extern void sync_inodes(void);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern int ROOT_DEV;

extern void mount_root(void);

#endif