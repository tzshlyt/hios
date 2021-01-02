// 文件系统中的一些常数和结构
#ifndef _FS_H
#define _FS_H

#include <sys/types.h>

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)       // 没有用到
 * 1 - /dev/mem             // 内存设备
 * 2 - /dev/fd              // 软盘
 * 3 - /dev/hd              // 硬盘
 * 4 - /dev/ttyx            // tty 串行终端
 * 5 - /dev/tty             // tty 终端
 * 6 - /dev/lp              // 打印设备
 * 7 - unnamed pipes        // 没有命名的管道
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

#define INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct d_inode)))
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct dir_entry)))

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

// 磁盘上的索引节点（i 节点）数据结构, 32字节
struct d_inode {
	unsigned short i_mode;
	unsigned short i_uid;
	unsigned long i_size;
	unsigned long i_time;
	unsigned char i_gid;
	unsigned char i_nlinks;
	unsigned short i_zone[9];
};

// 内存中 i 节点结构
// 存放文件系统中文件或目录名的索引节点
/*
    i_mode 保存文件类型和访问属性

     15                8  7                  0
    ｜-｜-｜-｜-｜-｜-|-｜R｜W｜X｜R｜W｜X｜R｜W｜X｜
      \------/  \----/   \----/ \----/  \----/
     文件类型   执行文件设置的信息    文件访问权限

    文件类型：                              执行文件设置的信息
        001: FIFO文件（八进制）                  01: 用户 ID
        002: 字符设备                           02: 组 ID
        004: 目录文件                           04: 对于目录，受限删除标志
        006: 块设备文件
        010: 常规文件


    文件中的数据存放在磁盘块的数据区，而文件名存在 i 节点，数据磁盘块号存放在i节点 i_zone[] 中
    i_zone[0] 到 i_zone[6] 用于存放文件开始的7个磁盘号，称直接块，若文件<=7K, 可以很快找到
    若文件大一些，就需要用到一次间接块 i_zone[7], 这个块中存放着附加的盘块号，对于MINIX可以存放512个盘块号，因此可以寻址512个盘块
    若还要大，则需二次间接块 i_zone[8]， 二次间接块的一级盘块的作用类似一次间接盘块，因此可以寻址512*512个块盘
    另外，对于 /dev/目录下的设备文件来说，它们并不占用数据块，即它们文件的长度为0。设备文件名的i节点仅用于保存其所定义设备属性和设备号，设备号被存在zone[0]中
*/
struct m_inode {
	unsigned short i_mode;                                                  // 文件类型和属性（rwx位）
	unsigned short i_uid;                                                   // 用户id（文件拥有者标识符）
	unsigned long i_size;                                                   // 文件大小（字节数）
	unsigned long i_mtime;                                                  // 修改时间
	unsigned char i_gid;                                                    // 组id
	unsigned char i_nlinks;                                                 // 链接数（多少个文件目录项指向该i节点）
	unsigned short i_zone[9];                                               // 文件所占用的盘上逻辑块号数组，其中
                                                                            // 直接（0-6）、间接（7）或双重间接（8）逻辑块号
                                                                            // 对于设备特殊文件名的i节点，其zone[0]中存放的是该文件名所指设备的设备号
/* these are in memory also */
	struct task_struct * i_wait;											// 等待该 i 节点的进程
	unsigned long i_atime;													// 最后访问时间
	unsigned long i_ctime;													// i 节点自身修改时间
	unsigned short i_dev;													// i 节点所在的设备号
	unsigned short i_num;													// i 节点号
	unsigned short i_count;													// i 节点被使用的次数，0表示该i节点空闲
	unsigned char i_lock;													// 锁定标志
	unsigned char i_dirt;													// 已修改（脏）标志
	unsigned char i_pipe;													// 用作管道标志
	unsigned char i_mount;													// 安装了其它文件系统标志
	unsigned char i_seek;													// 搜索标志（lseek时）
	unsigned char i_update;													// 已更新标志
};

// 文件结构（用于在文件句柄与 i 节点之间建立关系）
struct file {
	unsigned short f_mode;													// 文件操作模式（RW位）
	unsigned short f_flags;													// 文件打开和控制标志
	unsigned short f_count;													// 对应文件引用计数值
	struct m_inode * f_inode;												// 指向对应 i 节点
	off_t f_pos;															// 文件位置（读写偏移值）
};

// 内存中磁盘超级块结构
// 存放设备上文件系统结构的信息，并说明各部分大小
struct super_block {
	unsigned short s_ninodes;												// i节点数
	unsigned short s_nzones;												// 逻辑块为单位的逻辑块数（总块数）
	unsigned short s_imap_blocks;											// i 节点位图所占用的数据块数
	unsigned short s_zmap_blocks;											// 逻辑块位图所占用的数据块数
	unsigned short s_firstdatazone;											// 数据区开始第一个数据逻辑块号
	unsigned short s_log_zone_size;											// log（磁盘块数/逻辑块）以2为底，每个逻辑块包含的磁盘块数，MINIX1.0该值为0
	unsigned long s_max_size;												// 文件最大长度，当然这个值首磁盘容量的限制
	unsigned short s_magic;													// 文件系统魔数，MINIX1.0该值为 0x137f
/* These are only in memory */
	struct buffer_head * s_imap[8];											// i 节点位图缓冲块指针数组（占用8块，可表示64M）1块1024字节可代表8192个盘块
	struct buffer_head * s_zmap[8];											// 逻辑块位图缓冲块指针数组, MINIX1.0 最大块设备容量（长度）是64M
	unsigned short s_dev;													// 超级块所在的设备号
	struct m_inode * s_isup;												// 被安装文件系统根目录i节点（super i）
	struct m_inode * s_imount;												// 该文件系统被安装到的i节点
	unsigned long s_time;													// 修改时间
	struct task_struct * s_wait;											// 等待该超级块的进程的指针
	unsigned char s_lock;													// 锁定标志
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

// 文件目录项结构，共16字节
struct dir_entry {
	unsigned short inode;													// i 节点号
	char name[NAME_LEN];													// 文件名，长度 NAME_LEN=14
};

extern struct file file_table[NR_FILE];
extern struct super_block super_block[NR_SUPER];
extern int nr_buffers;

extern void check_disk_change(int dev);
extern struct m_inode * iget(int dev,int nr);
extern void truncate(struct m_inode * inode);
extern struct buffer_head * get_hash_table(int dev, int block);
extern void sync_inodes(void);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern int new_block(int dev);
extern void free_block(int dev, int block);
extern int bmap(struct m_inode * inode,int block);
extern int open_namei(const char * pathname, int flag, int mode, struct m_inode ** res_inode);
extern void iput(struct m_inode * inode);
extern struct m_inode * iget(int dev,int nr);
extern struct m_inode * new_inode(int dev);
extern void free_inode(struct m_inode * inode);
extern int sync_dev(int dev);
extern struct super_block * get_super(int dev);
extern int ROOT_DEV;

extern void mount_root(void);

#endif