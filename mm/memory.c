/*
 *  页表项/页目录项结构:
 *
 *      31               12     9     7   6   5     3     2     1    0
 *      --------------------------------------------------------------
 *      |    页帧地址     |  AVL | 0 0 | D | A | 0 0 | U/S | R/W  | P |
 *      --------------------------------------------------------------
 *
 *      P: Present 标志，表明地址转换是否有效
 *      R/W: 读写标志
 *      U/S:
 *
 *
 *
 *  线性地址结构:
 *
 *      31             22           12               0
 *      -----------------------------------------------
 *      |    页目录项    |   页表项   ｜    页内偏移  |
 *      -----------------------------------------------
 *
 */

#include <linux/kernel.h>
#include <linux/head.h>
#include <serial_debug.h>
#include <linux/mm.h>

#define LOW_MEM 0x100000ul                      // 内存低1MB，是系统代码所在
#define PAGING_MEMORY (15*1024*1024)            // 分页内存15MB，主内存区最多15M
#define PAGING_PAGES (PAGING_MEMORY >> 12)      // 分页后物理内存页数(3840)
#define MAP_NR(addr) (((addr) - LOW_MEM) >> 12) // 计算物理地址映射的页号
#define USED 100                                // 页面被占用标志

// 从 from 复制 1 页内存到 to 处( 4K 字节)
#define copy_page(from, to) \
    __asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024))

#define invalidate() \
    __asm__ volatile("mov %%eax, %%cr3"::"a" (0))

static unsigned long HIGH_MEMORY = 0;
void un_wp_page(unsigned long * table_entry);

// 物理内存映射字节图( 1 字节代表 1 页内存)。每个页面对应的字节用于标志页面当前被引用（占用）次数。
// 它最大可以映射 15MB 内存空间。
// 对于不能用做主内存页面的位置(缓冲区)均都预先被设置成USED（100）.
static unsigned char mem_map[PAGING_PAGES] = {0,};

static inline void oom() {
    panic("Out Of Memory!! QWQ\n");
}

// 物理内存初始化
// 该函数对1MB以上的内存区域以页面为单位进行管理前的初始化设置工作。一个页面长度
// 为4KB bytes.该函数把1MB以上所有物理内存划分成一个个页面，并使用一个页面映射字节
// 数组mem_map[]来管理所有这些页面。对于具有16MB内存容量的机器，该数组共有3840
// 项((16MB-1MB)/4KB)，即可管理3840个物理页面。每当一个物理内存页面被占用时就把
// mem_map[]中对应的字节值增1；若释放一个物理页面，就把对应字节值减1。若字节值为0，
// 则表示对应页面空闲；若字节值大于或等于1，则表示对应页面被占用或被不同程序共享占用。
// 在该版本的Linux内核中，最多能管理16MB的物理内存，大于16MB的内存将弃之不用。
// 对于具有16MB内存的PC机系统，在没有设置虚拟盘RAMDISK的情况下start_mem通常是4MB，
// end_mem是16MB。因此此时主内存区范围是4MB-16MB,共有3072个物理页面可供分配。而
// 范围0-1MB内存空间用于内核系统（其实内核只使用0-640Kb，剩下的部分被部分高速缓冲和
// 设备内存占用）。
// 参数start_mem是可用做页面分配的主内存区起始地址（已去除RANDISK所占内存空间）。
// end_mem是实际物理内存最大地址。而地址范围start_mem到end_mem是主内存区。
void mem_init(unsigned long start_mem, unsigned long end_mem) {
    unsigned long i;
    HIGH_MEMORY = end_mem;                          // 设置内存最高端（16MB）
    for (i = 0; i < PAGING_PAGES; i ++)             // 首先将 1MB 到 16MB 所有内存页对应的内存映射字节数组项置为已占用状态
        mem_map[i] = USED;

    i = (unsigned long)MAP_NR(start_mem);           // 主内存区起始位置对应的项号
    end_mem -= start_mem;
    end_mem >>= 12;                                 // 主内存区页面数
    while(end_mem-- > 0) {
        mem_map[i++] = 0;                           // 主内存区页面对应页面字节值清零
    }
    return;
}

// 计算内存空闲页面数并显示
// 调试使用
void calc_mem(void) {
    int i, j, k, free = 0;
    long *pg_tbl;

    for (i = 0; i < PAGING_PAGES; i++) {
        if (!mem_map[i]) free++;
    }
    printk("%d pages free (of %d in total)\n", free, PAGING_PAGES);

    for(i = 2; i < 1024; i++) {
        if (pg_dir[i] & 1) {
            pg_tbl = (long *)(0xfffff000 & pg_dir[i]);
            for (j = k = 0; j < 1024; j++) {
                if (pg_tbl[j] & 1) {
                    k++;
                }
            }
            printk("PageDir[%d] uses %d pages\n", i, k);
        }
    }
    return;
}

// 获取第一个（按顺序来说最后一个）空闲的内存物理页, 并标记为已使用，如果没有空闲页，返回 0
// 遍历 mem_map，直到遇到某个物理页映射为没有被占用状态
// 输入:
// %1(ax=0) - 0;
// %2(LOW_MEM)内存字节位图管理的起始位置;
// %3(cx=PAGING_PAGES);
// %4(edi=mem_map+PAGING_PAGES-1).
// 上面%4寄存器实际指向mem_map[]内存字节位图的最后一个字节。
// 本函数从位图末端开始向前扫描所有页面标志（页面总数PAGING_PAGE），
// 若有页面空闲（内存位图字节为0）则返回页面地址。注意！本函数只是指出在主内存区的一页空闲物理内存页面，
// 但并没有映射到某个进程的地址空间中去。后面的put_page()函数即用于把指定页面映射到某个进程地址空间中。
// 当然对于内核使用本函数并不需要再使用put_page()进行映射，
// 因为内核代码和数据空间（16MB）已经对等地映射到物理地址空间。
unsigned long get_free_page(void) {
    register unsigned long __res asm("ax");

    // std : 置位DF位, 向低地址减小
    // repne: repeat not equal
    // scasb: 意思是 al - di, 每比较一次di自动变化
    __asm__ volatile("std ; repne; scasb\n\t"   // std 置位方向位, al(0) 与对应每个页面的(di)内容比较
        "jne 1f\n\t"                            // 如果没有等于0的字节，则跳转结束（返回0）
        "movb $1,1(%%edi)\n\t"                  // 1 => [1+edi], 将对应页面内存映像比特位置1
        "sall $12,%%ecx\n\t"                    // 页面数*4K = 相对页面起始地址
        "addl %2,%%ecx\n\t"                     // 再加上低端内存地址，得到页面实际物理起始地址
        "movl %%ecx,%%edx\n\t"                  // 将页面实际起始地址 => edx
        "movl $1024,%%ecx\n\t"                  // 1024 => ecx
        "leal 4092(%%edx),%%edi\n\t"            // 4096+edx => edi （该页面的末端）
        "rep ; stosl\n\t"                       // 将edi所指内存清零 （反方向，即将该页面清零）
        "movl %%edx,%%eax\n"                    // 将页面起始地址 => eax（返回值）
        "1: cld"                                // cld相对应的指令是std,操作方向标志位DF,内存地址是增大（DF=0，向高地址增加）还是减小（DF=1，向低地址减小）
        :"=a" (__res)
        :"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
        "D" (mem_map+PAGING_PAGES-1)
        );
    return __res;                               // 返回空闲物理页面地址(若无空闲页面则返回0).
}

// 释放一页物理页，用于函数 free_page_tables()
// 将mem_map中相应状态减1
// addr - 物理地址
void free_page(unsigned long addr) {
    if (addr < LOW_MEM) return;
    if (addr >= HIGH_MEMORY) return;

    addr = MAP_NR(addr);                // 计算出页号
    if (mem_map[addr]--) return;        // 如果页使用状态大于0，则减1返回
    mem_map[addr] = 0;                  // 如果页面字节原本就是0，表示该物理页面本来就空闲，说明内核代码出问题
    panic("Trying to free free page");
}

// 释放页表连续内存块，exit() 需要该函数
// 根据指定线性地址和限长(页表个数), 释放对应内存页表指定的内存块并置表项空闲
// 页目录于物理地址0开始，1024项 * 4 字节 = 4K 字节
// 每个页目录项指定一个页表，内核页表从物理地址 0x1000 处开始(页目录后), 共4个页表。每个页表 1024项 * 4 字节 = 4K 字节
// 各进程（除在内核代码中的进程0和1）的页表所占据的页面在进程被创建时由内核为其在主内存区申请得到
// 每个页表项对应1页物理内存，因此一页最多映射 4MB 物理内存
// from - 起始线性基地址
// size - 释放内存字节长度
int free_page_tables(unsigned long from, unsigned long size) {
    unsigned long *pg_tbl;
    unsigned long *dir, nr;

    // 检查是否为 4MB 内存边界(该函数仅仅处理连续的4MB内存块)
    // 实际上是页表项和页内偏移为0
    if (from & 0x3fffff)
        panic("free_page_tables called with wrong alignment");
    // 如from = 0，则出错，说明试图释放内核和缓冲区
    if (!from)
        panic("try to free up swapper memory space");

    // 计算size长度所占的页目录项数（4MB 的进位整数倍）
    size = (size + 0x3fffff) >> 22;                     // 如size = 4.01Mb, 计算结果size = 2

    // 计算给出线性基地址对应的起始目录项
    // 对应的目录项号 = from >> 22，因为每个项占 4 个字节，并且由于页目录表从物理地址0开始存放
    // 因此 实际目录项指针 = 目录项号 << 2 ，也即（from >> 20）
    // & oxffc 确保目录项指针范围有效，即屏蔽目录项指针最后 2 位，因为只移动了 20 位，因此最后 2 位是页表项索引的内容，应屏蔽
    dir = (unsigned long *)((from >> 20) & 0xffc);       // _pg_dir = 0

    // 此时 size 是释放的页表个数，即页目录项数
    for (; size-->0; dir++) {
        if (!(*dir & 1))                                 // 该目录未被使用
            continue;

        pg_tbl = (unsigned long *)(*dir & 0xfffff000);   // 取页表地址
        for (nr = 0; nr < 1024; nr++) {
            if (*pg_tbl & 1) {
                free_page(0xfffff000 & *pg_tbl);         // 释放此页
            }
            *pg_tbl = 0;
            pg_tbl++;
        }

        free_page(0xfffff000 & *dir);                   // 释放该页表所占内存页面
        *dir = 0;
    }

    invalidate();       // 刷新页变换高速缓冲
    return 0;
}

// 把一物理内存页面映射到线性地址空间指定处
// 或者说是把线性地址空间中指定地址address出的页面映射到主内存区页面 page 上。
// 主要工作是在相关页面目录项和页表项中设置指定页面的信息。若成功则返回物理页面地址。
// 在处理缺页异常的C函数do_no_page()中会调用此函数。对于缺页引起的异常，
// 由于任何缺页缘故而对页表作修改时，并不需要刷新CPU的页变换缓冲(或称Translation Lookaside
// Buffer - TLB),即使页表中标志P被从0修改成1.因为无效叶项不会被缓冲，
// 因此当修改了一个无效的页表项时不需要刷新。在次就表现为不用调用Invalidate()函数。
// 参数page是分配的主内存区中某一页面(页帧，页框)的指针;address是线性地址。
// 在处理缺页异常 do_no_page() 中会调此函数
// page - 分配的主内存中某一页（页帧，页框）的指针
// address - 线性地址
unsigned long put_page(unsigned long page, unsigned long address) {
    unsigned long *pg_tbl, tmp;

    // 首先判断参数给定物理内存页面page的有效性。如果该页面位置低于LOW_MEM（1MB）
    // 或超出系统实际含有内存高端HIGH_MEMORY，则发出警告。LOW_MEM是主内存区可能
    // 有的最小起始位置。当系统物理内存小于或等于6MB时，主内存区起始于LOW_MEM处。
    // 再查看一下该page页面是否已经申请的页面，即判断其在内存页面映射字节图mem_map[]
    // 中相应字节是否已经置位。若没有则需发出警告。
    if (page < LOW_MEM || page >= HIGH_MEMORY)
        printk("Trying to put page %x at %x\n", page, address);

    if (mem_map[MAP_NR(page)] != 1)     // 该page页面是否是已经申请的页面，如果没有发出警告
        printk("mem_map disagrees with %x at %x\n", page, address);

    // 然后根据参数指定的线性地址 address 计算其在也目录表中对应的目录项指针，并从中取得二级页表地址。
    // 如果该目录项有效(P=1),即指定的页表在内存中，则从中取得指定页表地址放到page_table 变量中。
    // 否则就申请一空闲页面给页表使用，并在对应目录项中置相应标志(7 - User、U/S、R/W).
    // 然后将该页表地址放到 page_table 变量中。
    pg_tbl = (unsigned long *)((address >> 20) & 0xffc);

    // printk("Params: pg_tbl = %x, entry = %x\n", pg_tbl, (address >> 12) & 0x3ff);
    if((*pg_tbl) & 1) {   // 如果该目录项有效（P=1）, 即指定的页表在内存中
        // printk("Page table now available\n");
        pg_tbl = (unsigned long *)(*pg_tbl & 0xfffff000);
    }
    else {               // 否则申请一空闲页面给页表使用，并在相应目录项置相应标志，然后把页表地址放到pg_tbl变量中
        if (!(tmp = get_free_page())) {
            printk("NO FREE PAGE!");
            return 0;
        }

        *pg_tbl = tmp | 7;
        // printk("Tmp = %x\n", tmp);
        // printk("Page Table = %x\n", *pg_tbl);
        pg_tbl = (unsigned long *) tmp;
    }
    // printk("Put Page Success\n");
    // 最后找到的页表 page_table 中设置相关页表项内容
    // 即把物理页面 page 的地址填入表项同时置位3个标志（U/S、W/R、P）
    // 该页表项在页表中的索引值等于线性地址 位21-位12 组成的 10bit 值，每个页表共可有 1024 项（0 -- 0x3ff）
    pg_tbl[(address >> 12) & 0x3ff] = page | 7;
    // invalidate();    // 不需要刷新页变换高速缓冲
    return page;
}


//// 取得一页空闲内存页并映射到指定线性地址处。
// get_free_page()仅是申请取得了主内存区的一页物理内存。而本函数则不仅是获取到
// 一页物理内存页面，还进一步调用put_page()，将物理页面映射到指定的线性地址处。
// 参数 address 是指定页面的线性地址
// 获取一页物理内存并将其映射到指定的线性地址处
// address - 线性地址
void get_empty_page(unsigned long address) {
    unsigned long tmp;
    // 如果不能取得有一空闲页面，或者不能将所取页面放置到指定地址处，则显示内存不够信息。
    if (!(tmp = get_free_page()) || !put_page(tmp, address)) {
        free_page(tmp);
        oom();
    }
    return;
}

// 写页面验证
// 判断页项中 R/W 标志
// 若页面不可写，则复制页面
// 在 fork.c 中被 verify_area() 调用
// address - 页面线性地址
void write_verify(unsigned long address) {
    unsigned long page;

    // 检查页目录项是否存在
    // 首先取指定线性地址对应的页目录项，根据目录项中的存在位P判断目录项对应的
    // 页表是否存在(存在位P=12),若不存在(P=0)则返回。这样处理是因为对于不存在的
    // 页面没有共享和写时复制可言，并且若程序对此不存在的页面执行写操作时，系统
    // 就会因为缺页异常而去执行do_no_page()，并为这个地方使用put_page()函数映射
    // 一个物理页面。
    // 接着程序从目录项中取页表地址，加上指定页面在页表中的页表项偏移值，得对应
    // 地址的页表项指针。在该表项中包含这给定线性地址对应的物理页面。
    // dir = (unsigned long *)((address >> 20) & 0xffc)
    // pag = *(dir)
    if(!( (page = *((unsigned long *)((address >> 20) & 0xffc)) ) & 1)) {
        return ;
    }

    // 取页表首地址
    page &= 0xfffff000;
    page += ((address >> 10) & 0xffc);      // 因为pape 是 unsigned long类型，每项4个字节，相当于 >>12 然后 << 2
    // 然后判断该页表项中的位1(R/W)、位0(P)标志。如果该页面不可写(R/W=0)且存在，
    // 那么就执行共享检验和复制页面操作(写时复制)。否则什么也不做，直接退出。
    if((*(unsigned long *)page & 3) == 1) {   // 页表P = 1, R/W = 0
        un_wp_page((unsigned long *)page);
    }
    return;
}

// 取消写保护页面函数。用于页异常中断过程中写保护异常的处理(写时复制)。
// 在内核创建进程时，新进程与父进程被设置成共享代码和数据内存页面，并且所有这些
// 页面均被设置成只读页面。而当新进程或原进程需要向内存页面写数据时，CPU就会检测
// 到这个情况并产生页面写保护异常。于是在这个函数中内核就会首先判断要写的页面是否被共享。
// 若没有则把页面设置成可写然后退出。
// 若页面是出于共享状态，则需要重新申请一新页面并复制被写页面内容，以供写进程单独使用, 共享被取消。
// 本函数供 do_wp_page() 调用。
// table_entry: 为页表项物理地址。[up_wp_page -- Un-Write Protect Page]
void un_wp_page(unsigned long *table_entry) {
    s_printk("un_wp_page(0x%x) ", table_entry);
    unsigned long old_page, new_page;
    // 首先取参数指定的页表项中物理页面位置(地址)并判断该页面是否是共享页面。
    // 如果原页面地址大于内存低端LOW_MEM（表示在主内存区中），并且其在页面映射字节
    // 图数组中值为1（表示页面仅被引用1次，页面没有被共享），则在该页面的页表项
    // 中置R/W标志(可写),并刷新页变换高速缓冲，然后返回。即如果该内存页面此时只
    // 被一个进程使用，并且不是内核中的进程，就直接把属性改为可写即可，不用再重
    // 新申请一个新页面。
    old_page = *table_entry & 0xfffff000;
    s_printk("old_page = 0x%x\n", old_page);
    if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)] == 1) {    // 页面没有被共享
        s_printk("Above 1MB\n");
        *table_entry |= 2;      // 置位 R/W
        invalidate();
        return;
    }

    // 否则就需要在主内存区申请一页空闲页面给执行写操作的进程单独使用，取消页面
    // 共享。如果原页面大于内存低端(则意味着mem_map[]>1,页面是共享的)，则将原页
    // 面的页面映射字节数组递减1。然后将指定页表项内容更新为新页面地址，并置可读
    // 写等标志（U/S、R/W、P）。在刷新页变换高速缓冲之后，最后将原页面内容复制
    // 到新页面上。
    if (!(new_page = get_free_page()))
        oom();

    if (old_page >= LOW_MEM)
        mem_map[MAP_NR(old_page)]--;

    *table_entry = new_page | 7;
    invalidate();
    copy_page(old_page, new_page);
    // 页面内容拷贝后，进程A就可以在新的一页中完成压栈(写)动作了。
    // A执行一段时间后轮到进程B，进程B仍然使用原页面，假设也要在原页面中写操作，
    // 但是现在原页面的属性仍然是“只读”的，这一点在进程A创建进程B时就是这样设置的，
    // 一直都没有改变过，所以在这种情况下，又需要进行页写保护处理，仍然是映射到un_wp_page函数中
    // 由于原页面的引用计数已经被削减为1了，所以现在就要将原页面的属性设置为“可读可写”，然后函数返回。
    // 实际上执行了两次 un_wp_page，
    // 如果父进程A先写操作，父进程使用新页并将属性设置为可读写，子进程使用老页设置可读写并引用计数减1
    // 反之子进程B先写操作，子进程使用新页并将属性设置为可读写，父进程使用老页设置可读写并引用计数减1
    // 值得注意的是，页写保护是一个由系统执行的动作，在整个页写保护动作发生的过程中，
    // 用户进程仍然正常执行，它并不知道自己在内存中被复制了，也不知道自己被复制到哪个页面了。
    // 系统对用户进程的组织管理和协调，就表现在这里，用户进程能够正常执行是由系统保证的，
    // 它自己并不需要知道系统是如何保证的。
}

//// 执行写保护页处理
// 是写共享页面处理函数。是页异常中断处理过程中调用的C函数。在page.s程序中被调用。
// error_code - cpu 自动产生
// address - 页面线性地址
// 写共享页面时，需复制页面（写时复制）
void do_wp_page(unsigned long error_code, unsigned long address) {
    s_printk("Page Fault(Write) at [0x%x], errono %d\n", address, error_code);
    error_code = error_code; // 纯粹为了消除警告
    // 调用上面函数 un_wp_page() 来处理取消页面保护。但首先需要为其准备好参数。
    // 参数是 线性地址address 指定页面在页表中的 页表项物理地址，
    // 其计算方法是：
    // 1.((address>>10) & 0xffc): 计算指定线性地址中页表项在页表中的偏移地址；因
    // 为根据线性地址结构，(address>>12)就是页表项中的索引，但每项占4个字节，因
    // 此乘4后：(address>>12)<<2=(address>>10)&0xffc就可得到页表项在表中的偏移
    // 地址。与操作&0xffc用于限制地址范围在一个页面内。又因为只移动了10位，因此
    // 最后2位是线性地址低12位中的最高2位，也应屏蔽掉。因此求线性地址中页表项在
    // 页表中偏移地址直观一些的表示方法是(((address>>12)&ox3ff)<<2).
    // 2.(0xfffff000 & *((address>>20) &0xffc)):用于取目录项中页表的地址值；其中，
    // ((address>>20) &0xffc)用于取线性地址中的目录索引项在目录表中的偏移地址。
    // 因为address>>22是目录项索引值，但每项4个字节，因此乘以4后：(address>>22)<<2
    // = (address>>20)就是指定在目录表中的偏移地址。&0xffc用于屏蔽目录项索引值中
    // 最后2位。因为只移动了20位，因此最后2位是页表索引的内容，应该屏蔽掉。而
    // *((address>>20) &0xffc)则是取指定目录表项内容中对应页表的物理地址。最后与
    // 上0xfffff000用于屏蔽掉页目录项内容中的一些标志位(目录项低12位)。直观表示为
    // (0xfffff000 & *(unsigned log *) (((address>>22) & 0x3ff)<<2)).
    // 3.由1中页表项中偏移地址加上2中目录表项内容中对应页表的物理地址即可得到页
    // 表项的指针(物理地址)。这里对共享的页面进行复制。
    un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));
    mm_print_pageinfo(address);
}

// TODO: 未完成
// 执行缺页处理
// 访问不存在页面的处理函数，在页异常中断处理过程中调用，在 page.s 中调用
// error_code - cup 自动产生
// address - 页面线性地址
// 该函数首先尝试与已加载的相同文件进行页面共享，或者只是由于进程动态申请内
// 存页面而只需映射一页物理内存即可。若共享操作不成功，那么只能从相应文件中读入
// 所缺的数据页面到指定线性地址处。
void do_no_page(unsigned long error_code, unsigned long address) {
    // unsigned long tmp;
    unsigned long page;

    s_printk("Page Fault at [0x%x], errono %d\n", address, error_code);
    address &= 0xfffff000;
    if (!(page = get_free_page()))
        oom();

    // 最后把引起缺页异常的一页物理页面映射到指定线性地址address处。
    // 若操作成功就返回。否则就释放内存页，显示内存不够。
    if (put_page(page, address)) {
        // mm_print_pageinfo(address);
        return;
    }
    free_page(page);
    oom();
}

// 复制页目录表项和页表项
// 注意！我们并不是复制任何内存块，内存块的地址需要是 4Mb 的倍数，
// 正好一个页目录项对应的内存长度，不管怎么样，它仅被 fork() 使用。
// 复制指定线性地址和长度内存对应的页目录项和页表项，从而被复制的页目录和页表对
// 应的原物理内存页面区被两套页表映射而共享使用。复制时，需申请新页面来存放新页
// 表，原物理内存区将被共享。此后两个进程（父进程和其子进程）将共享内存区，直到
// 有一个进程执行谢操作时，内核才会为写操作进程分配新的内存页(写时复制机制)。
// 对于进程 0 和 1，只拷贝前 160 页共640Kb
// from , to 线性地址
// size - 需要复制（共享）的内存长度，单位是字节
// 页目录项存在于页目录表中，用以管理页表;页表项存在于页表中，用以管理页面。
// 因此，这里就要设置进程1的页目录项，以及为进程1创建页表并将进程0的页表项复制到进程1的页表中，
// 以此为进程1将来执行代码创造条件。进程1此时还没有对应的程序，它的页表又是从进程0对应页表拷贝过来的，
// 所以它们管理的页面完全一致，这就导致它暂时和进程0共享一套内存页面管理结构,
// 等将来它有了自己的程序，再把关系解除，并重新组织自己的内存管理结构。
/*
    0M      进程0页表          640KB          1M               16M
    |---------------------------|------------|----------------|  物理内存
            进程1页表


    0       64M  段限制长640k   128M              192M            256M             4G-1
    |--------|------------------|-----------------|---------------|---------------|


   0x00 内核                     0xfffff(1M)                           0xffe000 0xfff000 0xffffff(16M-1)
    |------|-----------------------|--------------------|---------------|---------|------|
   /  \                                                                / ｜      / \
  /    .. \ ..                                                      /    /     /      \
 /             \                                            ......     /     /            \
  1个页目录 和4个页表                              ......  /           /       进程1管理结构所在页面
 |   \                                       /                  /              /   \
            16     3.设置页目录项             0       159     1023            /          \
 |=|=|=|=|---|------------| 页目录           |========|-------| 页表         |=====|------|
  |                                            ^   get_free_page            1.设置代码段、数据段基地址
  +                                            |
 0       159    1023                           |
 |========|-------| 页表0                       |
      | 进程0页表，前160项有效          2.复制页表  |                   4. 刷新页变换高速缓冲
      [_________________________________________]

*/
int copy_page_tables(unsigned long from, unsigned long to, unsigned long size) {
    s_printk("copy_page_tables(0x%x, 0x%x, 0x%x)\n", from, to, size);
    unsigned long * from_page_table;
    unsigned long * to_page_table;
    unsigned long this_page;
    unsigned long * from_dir, * to_dir;
    unsigned long nr;

    // 4MB 内存边界对齐
    // 首先检测参数给出的原地址 from 和目的地址 to 的有效性。原地址和目的地址都需要在 4Mb 内存边界地址上。
    // 否则出错死机。作这样的要求是因为一个页表的 1024 项可管理 4Mb 内存。
    // 源地址 from 和目的地址 to 只有满足这个要求才能保证从一个页表的第一项开始复制页表项，
    // 并且新页表的最初所有项都是有效的。然后取得源地址和目的地址的其实目录项指针(from_dir 和 to_dir).
    // 再根据参数给出的长度 size 计算要复制的内存块占用的页表数(即目录项数)。
    if ((from & 0x3fffff) || (to & 0x3fffff)) {
        panic("copy_page_tables called with wrong alignment");
    }
    // 每个页目录表项是4字节, 相当于: (from>>22) * 4
    // 例子:
    // to: 64M  (00000100 00000000 00000000 00000000)2
    // to_dir就是(00000000 00000000 00000000 01000000)2
    // 而to_dir的高10为0，中10位0，低12位为(0001000000)2
    // 这个二进制数就是十进制的64，这表示第 0 个页偏移64位。第0个页就是页目录表所在的页,
    // 这个页偏移 64 位其实就是第 16 个页表项的地址
    // 操作系统想把 from 指向的那个页表的各个页表项丢到第 16 项页目标表项指向的页表上面去。
    from_dir = 0 + (unsigned long *) ((from >> 20) & 0xffc);    /* _pg_dir = 0 */
    to_dir = 0 + (unsigned long *) ((to >> 20) & 0xffc);        // 64M 计算得 0x40 = 64
    size = ((unsigned)(size + 0x3fffff)) >> 22;                 // 把不足一个4MB(一个页表所能控制的内存长度）的size取整为一个4MB, 640kb 计算得 1

    // s_printk("from_dir = 0x%x, *from_dir = 0x%x\n", from_dir, *from_dir);
    // s_printk("to_dir = 0x%x, *to_dir = 0x%x\n", to_dir, *to_dir);

    // 在得到了源起始目录项指针 from_dir 和目的起始目录项指针 to_dir 以及需要复制的
    // 页表个数 size 后，下面开始对每个页目录项依次申请1页内存来保存对应的页表，
    // 并且开始页表项复制操作。如果目的目录指定的页表已经存在(P=1)，则出错死机。
    // 如果源目录项无效，即指定的页表不存在(P=1),则继续循环处理下一个页目录项。
    for (; size-- > 0; from_dir++, to_dir++)  {
        // 若目的目录项指定的页表已经存在，则出错死机
        if (1 & *to_dir) {                                      // 最后 1 位属性位是 P 位
            panic("copy_page_tables: already exist");
        }
        // 若源目录项无效，继续处理下一个
        if (!(1 & *from_dir)) {
            continue;
        }

        // 取空闲页面保存目的目录项对应的页表
        // 在验证了当前源目录项和目的项正常之后，我们取源目录项中页表地址 from_page_table。
        // 为了保存目的目录项对应的页表，需要在主内存区中申请 1 页空闲内存页。
        // 如果取空闲页面函数 get_free_page() 返回0，则说明没有申请到空闲内存页面，
        // 可能是内存不够, 于是返回-1值退出。
        from_page_table = (unsigned long *)(0xfffff000 & *from_dir);     // 把 *from_dir 的高20位取出来, 源目录项中页表的地址
        // s_printk("from_page_table = 0x%x, *from_page_table = 0x%x\n", from_page_table, *from_page_table);
        if (!(to_page_table = (unsigned long *)get_free_page())) {      // 例: to_page_table = 0xffe000, 0xfff000 在 copy_process 中使用
            return -1;                                                  //     为进程 1 管理结构
        }
        // s_printk("to_page_table = 0x%x, *to_page_table = 0x%x\n", to_page_table, *to_page_table);
        // 否则我们设置目的目录项信息，把最后3位置位，即当前目录的目录项 | 7，
        // 表示对应页表映射的内存页面是用户级的，并且可读写、存在(Usr,R/W,Present).
        // (如果U/S位是0，则R/W就没有作用。如果U/S位是1，而R/W是0，那么运行在用
        // 户层的代码就只能读页面。如果U/S和R/W都置位，则就有读写的权限)。
        // 然后针对当前处理的页目录项对应的页表，设置需要复制的页面项数。
        // 如果是在内核空间，则仅需复制头 160 页对应的页表项(nr=160),对应于开始 640KB 物理内存
        // 否则需要复制一个页表中的所有1024个页表项(nr=1024)，可映射4MB物理内存。
        *to_dir = ((unsigned long) to_page_table) | 7;          // 设置标志
        // s_printk("to_dir = 0x%x, *to_dir = 0x%x\n", to_dir, *to_dir);
        nr = (from == 0) ? 0xA0 : 1024;                         // 若内核空间，则仅需复制头160页（640KB）
        // 此时对于当前页表，开始循环复制指定的 nr 个内存页面表项。先取出源页表的内容，
        // 如果当前源页表没有使用，则不用复制该表项，继续处理下一项。
        // 否则复位表项中 R/W 标志(位1置0)，即让页表对应的内存页面只读。
        // 然后将页表项复制到目录页表中。
        for (; nr-- > 0; from_page_table++, to_page_table++) {
            this_page = *from_page_table;
            if (!(1 & this_page))                               // 当前源页面没有使用，则不用复制
                continue;
            this_page &= (unsigned long)~2;                     // 置为可读, 进程A创建进程B后继续执行,B的压栈写操作引发页写保护 page_fault
            *to_page_table = this_page;

            // 如果该页表所指物理页面的地址在1MB以上，则需要设置内存页面映射数
            // 组 mem_map[]，于是计算页面号，并以它为索引在页面映射数组相应项中
            // 增加引用次数。而对于位于1MB以下的页面，说明是内核页面，因此不需
            // 要对 mem_map[] 进行设置。因为 mem_map[] 仅用于管理主内存区中的页面使
            // 用情况。因此对于内核移动到任务 0 中并且调用 fork() 创建任务 1 时(用于
            // 运行init())，由于此时复制的页面还仍然都在内核代码区域，因此以下
            // 判断中的语句不会执行，任务 0 的页面仍然可以随时读写。只有当调用fork()
            // 的父进程代码处于主内存区(页面位置大于1MB)时才会执行。这种情况需要
            // 在进程调用execve()，并装载执行了新程序代码时才会出现。
            // *from_page_table = this_page; 这句是令源页表项所指内存页也为只读。
            // 因为现在开始有两个进程公用内存区了。若其中1个进程需要进行写操作，
            // 则可以通过页异常写保护处理为执行写操作的进程匹配 1 页新空闲页面，也
            // 即进行写时复制(copy on write)操作。
            if(this_page > LOW_MEM) {                           // 主内存中
                *from_page_table = this_page;                   // 令源页表项也只读
                mem_map[MAP_NR(this_page)]++;
            }
        }
    }
    invalidate();        // 刷新页变换高速缓冲
    return 0;
}


