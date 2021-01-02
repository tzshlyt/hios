#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* open/fcntl - NOCTTY, NDELAY isn't implemented yet */
#define O_ACCMODE	   03   /* mask for above modes */  // 文件访问模式屏蔽码
/* open-only flags */  // 打开文件open()或者文件控制函数fcntl()使用的文件访问模式。同时只能使用三者之一
#define O_RDONLY	   00   /* open for reading only */
#define O_WRONLY	   01   /* open for writing only */
#define O_RDWR		   02   /* open for reading and writing */


// 下面是文件创建和操作标志，用于open()，可与上面访问模式用‘位或’的方式一起使用
#define O_CREAT		00100	/* not fcntl */     // 如果文件不拆创建。fcntl函数不用
#define O_EXCL		00200	/* not fcntl */     // 独立使用文件标志
#define O_NOCTTY	00400	/* not fcntl */     // 不分配控制终端
#define O_TRUNC		01000	/* not fcntl */     // 若文件已存在且是写操作，则长度截为0
#define O_APPEND	02000                       // 添加方式打开
#define O_NONBLOCK	04000	/* not fcntl */     // 非阻塞方式打开和操作文件
#define O_NDELAY	O_NONBLOCK                  // 非阻塞方式打开和操作文件

extern int open(const char * filename, int flags, ...);

#endif
