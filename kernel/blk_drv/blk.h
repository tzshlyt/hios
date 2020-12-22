// 硬盘设备参数头文件

#ifndef _BLK_H
#define _BLK_H

#define NR_BLK_DEV	7           // 块设备数量

/*
* 下面定义的 NR_REQUEST 是请求队列中所包含的项数
* 注意，写操作仅使用这些项中低端的2/3项; 读操作优先处理
* 32项好象是一个合理的数字，已经足够从电梯算法中获得好处，但当缓冲区在队列中而锁住时又不显得是很大的数
* 64就看上去太大了(当大量的写/同步操作运行时很容易引起长时间的暂停)
*/
#define NR_REQUEST	32

/*
 下面是 request 结构的一个扩展形式，因而当实现以后，我们
 就可以在分页请求中使用同样的request结构。在分页处理中，
 bh 是 NULL，而 waiting 则用于等待读/写的完成。
 下面是请求队列中项的结构。其中如果字段 dev=-1，则表示队列中该项没有被使用。
 字段cmd可取常量 READ(0)或WRITE(1)(定义在include/linux/fs.h)。
*/
struct request {
	int dev;		/* -1 if no request */      // 发请求的设备号
	int cmd;		/* READ or WRITE */
	int errors;                                 // 操作时产生的错误次数
	unsigned long sector;                       // 起始扇区（1块=2扇区）
	unsigned long nr_sectors;                   // 读/写扇区数
	char * buffer;                              // 数据缓冲区
	struct task_struct * waiting;               // 任务等待操作执行完成的地方
	struct buffer_head * bh;                    // 缓冲区头指针
	struct request * next;                      // 指向下一请求项
};

// 下面的定义用于电梯算法:注意读操作总是在写操作之前进行。
// 这是很自然的:因为读操作对时间的要求要比写操作严格得多。
// 下面宏中参数s1和s2的取值是上面定义的请求结构 request 的指针。
// 该宏定义用于根据两个参数指定的请求项结构中的信息(命令cmd (READ或WRITE)、设备号dev以及所操作的扇区号sector)来判断出两个请求项结构的前后排列顺序。
// 这个顺序将用作访问块设备时的请求项执行顺序。
// 这个宏会在程序b1k_drv/11_rw_b1k.c中函数 add_request() 中被调用。
// 该宏部分地实现了 I/O 调度功能，即实现了对请求项的排序功能(另一个是请求项合并功能)。
#define IN_ORDER(s1,s2) \
((s1)->cmd < (s2)->cmd || ((s1)->cmd == (s2)->cmd && \
((s1)->dev < (s2)->dev || ((s1)->dev == (s2)->dev && \
(s1)->sector < (s2)->sector))))

// 块设备结构
struct blk_dev_struct {
	void (*request_fn)(void);           // 请求操作的函数指针
	struct request * current_request;   // 当前正在请求的信息结构
};

extern struct blk_dev_struct blk_dev[NR_BLK_DEV];

#ifdef MAJOR_NR                                 // 主设备号

/* harddisk */
#define DEVICE_NAME "harddisk"                  // 硬盘名称
#define DEVICE_INTR do_hd                       // 设备中断处理程序 do_hd()
#define DEVICE_REQUEST do_hd_request            // 设备请求函数
#define DEVICE_NR(device) (MINOR(device)/5)     // 设备号（0-1）每个硬盘可有4个分区
#define DEVICE_ON(device)                       // 硬盘一直在工作，无须开启和关闭
#define DEVICE_OFF(device)

#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif

#endif

#endif