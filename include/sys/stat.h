#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

#define S_IFMT  00170000                            // 文件类型屏蔽码（8进制）
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)     // 常规文件
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)     // 目录文件
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)     // 字符设备
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)     // 块设备
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)     // FIFO

#endif