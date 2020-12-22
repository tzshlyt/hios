#ifndef _HDREG_H
#define _HDREG_H

// 硬盘分区表结构
// 硬盘逻辑上分为1--4个分区，每个分区之间的扇区号是邻接的。
// 分区表由4个表项组成，每个表项由16字节组成，对应一个分区的信息，存放有分区的大小和起止的柱面号、磁道号和扇区号。
// 分区表存放在硬盘0柱面0头第1个扇区的0x1BE-0x1FD处。
struct partition {
	unsigned char boot_ind;		/* 0x80 - active (unused) */  // 引导标志。4个分区中只能有一个分区是可引导的。0x00:不从该分区引导操作系统，0x80从该分区引导
	unsigned char head;		/* ? */         // 分区起始磁头
	unsigned char sector;		/* ? */     // 分区起始扇区号（位0-5）和起始柱面号高2位（位6-7）
	unsigned char cyl;		/* ? */         // 分区起始柱面低8位
	unsigned char sys_ind;		/* ? */     // 分区类型字节。0x0b-DOS,0x80-Old Minix;0x83-Linux...
	unsigned char end_head;		/* ? */     // 分区的结束磁头号
	unsigned char end_sector;	/* ? */     // 结束扇区号（位0-5）和结束柱面号高2位（位6-7）
	unsigned char end_cyl;		/* ? */     // 结束柱面号低 8 位
	unsigned int start_sect;	/* starting sector counting from 0 */       // 分区起始物理扇区号
	unsigned int nr_sects;		/* nr of sectors in partition */            // 分区占用扇区数
};

#endif