#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <serial_debug.h>

//// 打开（或创建）文件系统调用
// 如果调用操作成功，则返回文件句柄(文件描述符)，否则返回出错码。
int sys_open(const char *filename, int flag, int mode) {
    s_printk("sys_open()\n");
    struct m_inode * inode;
    struct file * f;
    int i, fd;

    // 将用户设置的文件模式和屏蔽码相与，产生许可的文件模式
    mode &= 0777 & ~current->umask;         // init进程 umask=022=00010010b

    for (fd = 0; fd < NR_OPEN; fd++) {
        if (!current->filp[fd])             // 进程文件结构中找到空闲项
            break;
    }
    if (fd >= NR_OPEN)
        return -EINVAL;

    // 设置当前进程的执行时关闭文件句柄(close_on_exec)位图，复位对应的 bit 位
    // close_on_exec是一个进程所有文件句柄的bit标志。每个bit位代表一个打开着的文件描述符，
    // 用于确定在调用系统调用execve()时需要关闭的文件句柄。
    // 当程序使用fork()函数创建了一个子进程时，通常会在该子进程中调用execve()函数加载执行另一个新程序。
    // 此时子进程中开始执行新程序。若一个文件句柄在close_on_exec中的对应bit位被置位，
    // 那么在执行execve()时该对应文件句柄将被关闭，否则该文件句柄将始终处于打开状态。
    // 当打开一个文件时，默认情况下文件句柄在子进程中也处于打开状态。因此这里要复位对应 bit 位。
    current->close_on_exec &= (unsigned long)~(1<<fd);
    f = 0+file_table;
    for (i = 0; i < NR_FILE; i++, f++) {
        if (!f->f_count)                    // 文件表中文找到空闲项（引用计数器为0）
            break;
    }
    if (i >= NR_FILE)
        return -EINVAL;

    // 进程对应文件句柄fd的文件结构指针指向搜索到的文件结构，并文件引用计数加1
    (current->filp[fd] = f)->f_count++;

    // open_namei() 打开操作
    if ((i = open_namei(filename, flag, mode, &inode)) < 0) {
        current->filp[fd] = NULL;
        f->f_count = 0;
        return i;
    }

    // 对不同类型文件处理
    // 如果打开字符设备
    if (S_ISCHR(inode->i_mode)) {
        // tty 串行终端设备
        // 如果当前进程是组首领并且当前进程的 tty 字段小于0(没有终端)，
        // 则设置当前进程的 tty 号为该i节点的子设备号，并设置 当前进程tty 对应的 tty表项 的父进程组号等于当前进程的进程组号
        // 表示为该进程组（会话期）分配控制终端。
        if (MAJOR(inode->i_zone[0]) == 4) {         // ttyxx major==4
            if (current->leader && current->tty < 0) {
				current->tty = MINOR(inode->i_zone[0]);
				tty_table[current->tty].pgrp = current->pgrp;
			}
        // tty 终端设备
        // 若当前进程没有 tty, 则说明出错
        } else if (MAJOR(inode->i_zone[0]) == 5) {     // tty major==5
            if (current->tty < 0) {
                iput(inode);                        // 放回 i 节点
                current->filp[fd] = NULL;
                f->f_count = 0;
                return -EPERM;
            }
        }
    }

    // 如果打开块设备， 需要检查盘片是否被更换
    // 若更换过则需让缓冲区中该设备的所有缓冲块失效
    if (S_ISBLK(inode->i_mode)) {
        check_disk_change(inode->i_zone[0]);
    }

    // 初始化打开文件结构
    f->f_mode = inode->i_mode;
    f->f_flags = (unsigned short)flag;
    f->f_count = 1;
    f->f_inode = inode;
    f->f_pos = 0;
    return fd;
}