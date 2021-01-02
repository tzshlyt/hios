// 定义了 i 节点中文件属性和类型 i_mode 字段所使用到的一些标志位常量符

#ifndef _CONST_H
#define _CONST_H

#define BUFFER_END 0x200000                 // 缓冲使用内存的末端（代码中没有使用）

// i 节点 i_mode 字段的各标志
#define I_TYPE          0170000             // 指明i节点类型（类型屏蔽码）
#define I_DIRECTORY	    0040000             // 目录文件
#define I_REGULAR       0100000             // 常规文件
#define I_BLOCK_SPECIAL 0060000             // 块设备
#define I_CHAR_SPECIAL  0020000             // 字符设备
#define I_NAMED_PIPE	0010000             // 命名管道节点
#define I_SET_UID_BIT   0004000             // 在执行时设置有效用户 ID 类型
#define I_SET_GID_BIT   0002000             // 在执行时设置有效组 ID 类型

#endif
