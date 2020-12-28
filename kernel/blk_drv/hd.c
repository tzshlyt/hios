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

void do_hd_request(void);

// 读CMOS参数宏函数。
// 这段宏读取CMOS中硬盘信息。与init/main.c中读取 CMOS 时钟信息的宏完全一样。
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr, 0x70); \
inb_p(0x71); \
})

/* Max read/write errors/sector */
#define MAX_ERRORS	7
#define MAX_HD		2               // 系统支持的最多硬盘数

static int recalibrate = 0;         // 重新校正标志
static int reset = 0;               // 复位标志

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

#define port_read(port, buf, nr) \
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr))

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
		printk("Partition table%s ok.\n",(NR_HD>1)?"s":"");
	rd_load();
	mount_root();
    return 0;
}

// 判断并循环等待硬盘控制器就续
// 由于现在 PC 机速度很快，因此可以加大一些循环次数
static int controller_ready(void) {
	int retries=100000;

	while (--retries && (inb_p(HD_STATUS)&0x80));
	return (retries);
}

// 检测硬盘执行命令后的状态
static int win_result(void) {
	int i = inb_p(HD_STATUS);

	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
		== (READY_STAT | SEEK_STAT))
		return(0); /* ok */
	if (i&1) i = inb(HD_ERROR);
	return (1);
}


// 读写硬盘失败处理调用函数
// 如果读扇区时的出错次数大于或等于7次时，则结束当前请求项并唤醒等待该请求的进程，而且对应缓冲区更新标志复位，表示数据没有更新。
// 如果读一扇区时的出错次数已经大于3次，则要求执行复位硬盘控制器操作(设置复位标志)。
static void bad_rw_inter(void) {
    if (++CURRENT->errors >= MAX_ERRORS)
		end_request(0);
	if (CURRENT->errors > MAX_ERRORS/2)
		reset = 1;
}

// 读操作中断调用函数
static void read_intr(void) {
    // s_printk("read_intr()\n");
    // 错误检测
    if (win_result()) {
        bad_rw_inter();
        do_hd_request();        // 再次请求硬盘作相应（复位）处理
        return;
    }
    // 连续读入扇区数据到请求项的缓冲区
    port_read(HD_DATA, CURRENT->buffer, 256);   // 256是指内存字，即512字节
    CURRENT->errors = 0;
    CURRENT->buffer += 512;
    CURRENT->sector++;
    if (--CURRENT->nr_sectors) {
        do_hd = &read_intr;         // 等待硬盘在读出另1个扇区数据后发出中断并再次调用本函数
        return;
    }
    // 全部扇区读完
    end_request(1);                 // 数据已更新标志置位
    do_hd_request();
}

// 写操作中断调用函数
static void write_intr(void) {
    s_printk("write_intr()\n");

}

static void hd_out(unsigned int drive, unsigned int nsect, unsigned int sect,
		unsigned int head, unsigned int cyl, unsigned int cmd,
		void (*intr_addr)(void))
{
	register int port asm("dx");                // 定义局部寄存器变量并存放在指定寄存器 dx 中

    // 驱动器号 drive 只能是 0 和 1， 磁头号不能 > 15
	if (drive > 1 || head > 15)
		panic("Trying to write bad sector");
	if (!controller_ready())
		panic("HD controller not ready");
	do_hd = intr_addr;                          // 硬盘中断发生时将调用的c函数指针 do_hd
	outb_p(hd_info[drive].ctl, HD_CMD);         // 向控制寄存器输出控制字节
	port = HD_DATA;                             // 置dx为数据寄存器端口(0x1f0)
	outb_p(hd_info[drive].wpcom>>2, ++port);    // 参数:写预补偿柱面号(需除4)
	outb_p(nsect, ++port);                      // 参数:读/写扇区总数
	outb_p(sect, ++port);                       // 参数:起始扇区
	outb_p(cyl, ++port);                        // 参数:柱面号低8位
	outb_p(cyl>>8, ++port);                     // 参数:柱面号高8位
	outb_p(0xA0|(drive<<4)|head, ++port);       // 参数:驱动器号+磁头号
	outb(cmd,++port);                           // 命令:硬盘控制命令
}

//// 执行硬盘读写请求操作。
// 该函数根据设备当前请求项中的设备号和起始扇区号信息首先计算得到对应硬盘上的柱面号、当前磁道中扇区号、磁头号数据，
// 然后再根据请求项中的命令(READ/WRITE)对硬盘发送相应读/写命令。
// 若控制器复位标志或硬盘重新校正标志已被置位，那么首先会去执行复位或重新校正操作。
// 若请求项此时是块设备的第1个(原来设备空闲)，则块设备当前请求项指针会直接指向该请求项(参见11_rw_b1k.c)，并会立刻调用本函数执行读写操作。
// 否则在一个读写操作完成而引发的硬盘中断过程中，若还有请求项需要处理，则也会在硬盘中断过程中调用本函数。参考kernel/system call.s
void do_hd_request(void) {
    // s_printk("do_hd_request()\n");
    int i,r = 0;
	unsigned int block,dev;
	unsigned int sec,head,cyl;
	unsigned int nsect;

    // 首先检测请求项合法性
    INIT_REQUEST;
    dev = MINOR(CURRENT->dev);          // 子设备号即对应硬盘上各分区
	block = CURRENT->sector;            // 请求的起始扇区

    if (dev >= (unsigned int)(5*NR_HD) || block + 2 > (unsigned int)hd[dev].nr_sects) {   // 一次要求读写一块数据（2个扇区）
        end_request(0);
        goto repeat;
    }

    block += (unsigned int)hd[dev].start_sect;        // 获取磁盘的绝对扇区号
    dev /= 5;                           // 此时 dev 代表硬盘号(硬盘 0 还是硬盘 1)
    // 根据绝对扇区号 block 和硬盘号 dev，计数磁道中扇区号（sec）、所在柱面号（cyl）、和磁头号（head）
    // 初始时 eax = block, edx = 0,
    // divl 指令把 edx:eax组成的扇区号除以每磁道扇区数 hd_info[dev].sect, 所得整数保存在eax中，余数在edx中，
    // 其中 eax 中是到指定位置对应总磁道数，edx中是当前磁道上扇区号
    __asm__("divl %4":"=a" (block),"=d" (sec):"0" (block),"1" (0),
		"r" (hd_info[dev].sect));
    // 初始时 eax = 计数出的总磁道数，edx = 0，hd_info[dev].head 硬盘总磁头数
    // 其中 eax 中是柱面号，edx中是当前磁头号 head
	__asm__("divl %4":"=a" (cyl),"=d" (head):"0" (block),"1" (0),
		"r" (hd_info[dev].head));
	sec++;                              // 对计数所得当前磁道扇区号进行调整
	nsect = CURRENT->nr_sectors;        // 欲读写的扇区数

    if (reset) {
        // TODO:
    }

    if (recalibrate) {
        // TODO:
    }

    if (CURRENT->cmd == WRITE) {
        // TODO:
	} else if (CURRENT->cmd == READ) {
		hd_out(dev, nsect, sec, head, cyl, WIN_READ, &read_intr);
	} else
		panic("unknown hd-command");
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