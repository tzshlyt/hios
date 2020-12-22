#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <serial_debug.h>

#define MAJOR_NR 3
#include "blk.h"

// 读CMOS参数宏函数。
// 这段宏读取CMOS中硬盘信息。与init/main.c中读取 CMOS 时钟信息的宏完全一样。
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr, 0x70); \
inb_p(0x71); \
})

#define MAX_HD		2               // 系统支持的最多硬盘数

// 硬盘信息结构 (Harddisk information struct)。
// 各字段分别是磁头数、每磁道扇区数、柱面数、写前预补偿柱面号、磁头着陆区柱面号、控制字节。
struct hd_i_struct {
	int head,sect,cyl,wpcom,lzone,ctl;
};
struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;

// 定义硬盘分区结构。
// 给出每个分区从硬盘 0 道开始算起的物理起始扇区号和分区扇区总数。
// 其中5的倍数处的项(例如hd[0]和hd[5]等)代表整个硬盘的参数。
static struct hd_struct {
	long start_sect;        // 分区在硬盘中起始物理（绝对）扇区
	long nr_sects;          // 分区中扇区总数
} hd[5*MAX_HD]={{0,0},};

extern void hd_interrupt(void);
extern void rd_load(void);

int sys_setup(void * BIOS) {
    s_printk("sys_setup()\n");
    static int callabble = 1;
    int i, drive;
    struct partition *p;
    struct buffer_head *bh;

    if (!callabble)
        return -1;
    callabble = 0;

    for (drive = 0; drive < 2; drive++) {
        hd_info[drive].cyl = *(unsigned short *) BIOS;
        hd_info[drive].head = *(unsigned char *) (2+BIOS);
        hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
        hd_info[drive].ctl = *(unsigned char *) (8+BIOS);
        hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
        hd_info[drive].sect = *(unsigned char *) (14+BIOS);
        BIOS += 16;                                             // 每个硬盘参数表长16字节，这里 BIOS 指向下一表
    }
    // setup.s程序在取BIOS硬盘参数表信息时，如果系统中只有1个硬盘，就会将对应第2个硬盘的16字节全部清零。
    // 因此这里只要判断第2个硬盘柱面数是否为0就可以知道是否有第2个硬盘了。
    if (hd_info[1].cyl)
        NR_HD=2;
    else
        NR_HD=1;
    // 到这里，硬盘信息数组 hd_info[] 已经设置好，并且确定了系统含有的硬盘数NR_HD。现在开始设置硬盘分区结构数组 hd[]。
    // 该数组的项 0 和项 5 分别表示两个硬盘的整体参数，而项1一4和6-9分别表示两个硬盘的4个分区的参数。
    // 因此这里仅设置表示硬盘整体信息的两项(项0和5)。
    for (i=0 ; i < NR_HD ; i++) {
        hd[i*5].start_sect = 0;
        hd[i*5].nr_sects = hd_info[i].head*
            hd_info[i].sect*hd_info[i].cyl;
	}

    // 到此为止我们已经真正确定了系统中所含的硬盘个数NR_HD。现在我们来读取每个硬盘上第1个扇区中的分区表信息，
    // 用来设置分区结构数组hd[]中硬盘各分区的信息。首先利用读块函数 bread() 读硬盘第1个数据块(fs/buffer.c)，
    // 第1个参数(0x300、0x305)分别是两个硬盘的设备号，第2个参数(0)是所需读取的块号。若读操作成功，则数据会被存放在缓冲块bh的数据区中。
    // 若缓冲块头指针 bh 为 0，则说明读操作失败，则显示出错信息并停机。否则我们根据硬盘第1个扇区最后两个字节应该是 0xAA55 来判断扇区中数据的有效性，
    // 从而可以知道扇区中位于偏移 0x1BE 开始处的分区表是否有效。若有效则将硬盘分区表信息放入硬盘分区结构数组 hd[] 中。最后释放 bh 缓冲区。
    for (drive = 0; drive < NR_HD; drive++) {
        if (!(bh = bread(0x300 + drive*5, 0))) {                                     // 0x300,0x305 是设备号
            printk("Unable to read partition table of drive %d\n\r", drive);
			panic("");
        }
        if (bh->b_data[510] != 0x55 || (unsigned char)
		    bh->b_data[511] != 0xAA) {                                               // 判断硬盘标志0xAA55
			printk("Bad partition table on drive %d\n\r",drive);
			panic("");
		}
        p = 0x1BE + (void *)bh->b_data;                                             // 分区表位于第1扇区0x1BE处
        for (i = 1; i < 5; i++, p++) {
			hd[i+5*drive].start_sect = (long)p->start_sect;
			hd[i+5*drive].nr_sects = (long)p->nr_sects;
		}
		brelse(bh);                                                                 // 释放内存
    }
    // 现在总算完成设置硬盘分区结构数组 hd[] 的任务。如果确实有硬盘存在并且已读入其分区表，则显示“分区表正常”信息。
    // 然后尝试在系统内存虚拟盘中加载启动盘中包含的根文件系统映像(blk_drv/ramdisk.c)。
    // 即在系统设置有虚拟盘的情况下判断启动盘上是否还含有根文件系统的映像数据。
    // 如果有(此时该启动盘称为集成盘)则尝试把该映像加载并存放到虚拟盘中，然后把此时的根文件系统设备号 ROOT_DEV 修改成虚拟盘的设备号。
    // 最后安装根文件系统(fs/super.c)。
    if (NR_HD)
		printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
	rd_load();
	mount_root();
    return 0;
}

static void read_intr(void) {
    s_printk("read_intr()\n");
}

static void write_intr(void) {
    s_printk("write_intr()\n");
}

void do_hd_request(void) {
    s_printk("do_hd_request()\n");
}

void unexpected_hd_interrupt(void) {
    printk("Unexpected HD interrupt\n");
}

// 硬盘系统初始化
// 设置硬盘中断描述符，并允许硬盘控制器发送中断请求信号。
// 该函数设置硬盘设备的请求项处理函数指针为 do_hd_request(), 然后设置硬盘中断门
// 描述符。Hd_interrupt(kernel/system_call.s)是其中断处理过程。硬盘中断号为
// int 0x2E(46),对应8259A芯片的中断请求信号IRQ13.接着复位接联的主8250A int2
// 的屏蔽位，允许从片发出中断请求信号。再复位硬盘的中断请求屏蔽位(在从片上)，
// 允许硬盘控制器发送中断信号。中断描述符表 IDT 内中断门描述符设置宏 set_intr_gate().
void hd_init() {
    s_printk("hd_init()\n");
    blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;      // do_hd_request()
	set_intr_gate(0x2E, &hd_interrupt);
	outb_p(inb_p(0x21)&0xfb, 0x21);                      // 复位接联的主8259A int2的屏蔽位
	outb(inb_p(0xA1)&0xbf, 0xA1);                        // 复位硬盘中断请求屏蔽位(在从片上)
}