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

#define LOW_MEM 0x100000                        // 内存低1MB，是系统代码所在
#define PAGING_MEMORY (15*1024*1024)            // 分页内存15MB，主内存区最多15M
#define PAGING_PAGES (PAGING_MEMORY >> 12)      // 分页后物理内存页数(3840)
#define MAP_NR(addr) (((addr) - LOW_MEM) >> 12) // 计算物理地址映射的页号
#define USED 100                                // 页面被占用标志

// 从 from 复制 1 页内存到 to 处( 4K 字节)
#define copy_page(from, to) \
    __asm__("cld; rep; movsl;"::""S" (from), "D" (to), "c" (1024)")

#define invalidate() \
    __asm__("mov %%eax, %%cr3"::"a" (0))

static long HIGH_MEMORY = 0;

// 物理内存映射字节图( 1 字节代表 1 页内存)。每个页面对应的字节用于标志页面当前被引用
// （占用）次数。它最大可以映射 15MB 内存空间。
static unsigned char mem_map[PAGING_PAGES] = {0,};

static inline void oom() {
    panic("Out Of Memory!! QWQ\n");
}

// 物理内存初始化
// 对1MB以上内存页页面为单位管理前初始化
void mem_init(unsigned long start_mem, unsigned long end_mem) {
    int i;
    HIGH_MEMORY = end_mem;                          // 设置内存最高端（16MB）
    for (i = 0; i < PAGING_PAGES; i ++)             // 将 1MB 到 16MB 所有内存页对应的内存映射字节数组项置为已占用状态
        mem_map[i] = USED;

    i = MAP_NR(start_mem);                          // start_mem 对应的项号
    end_mem -= start_mem;
    end_mem >> 12;                                  // 主内存区页面数
    while(end_mem-- > 0) {
        mem_map[i++] = 0;                           // 主内存区页面对应字节值清零
    }
    return;
}

// 计算内存空闲页面数并显示
// 调试使用
void calc_mem(void) {
    int i, j, k, free = 0;
    long *pg_tbl, *dir;

    for (i = 0; i < PAGING_PAGES; i++) {
        if (!mem_map[i]) free++;
    }
    printk("%d pages free (of %d in total)\n", free, PAGING_PAGES);

    for(i = 2; i < 1024; i++) {
        if (dir[i] & 1) {
            pg_tbl = (long *)(0xfffff000 & dir[i]);
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


// 获取第一个（按顺序来说最后一个）空闲的内存物理页, 并标记为已使用，如果没有空闲页，返回0
// 遍历 mem_map，直到遇到某个物理页映射为没有被占用状态
unsigned long get_free_page(void) {
    register unsigned long __res asm("ax");

    // std : 置位DF位
    // repne: repeat not equal
    // scasb: 意思是 al - di, 每比较一次di自动变化
    __asm__("std ; repne ; scasb\n\t"   // std 置位方向位, al(0) 与对应每个页面的(di)内容比较
        "jne 1f\n\t"                    // 如果没有等于0的字节，则跳转结束（返回0）
        "movb $1,1(%%edi)\n\t"          // 1 => [1+edi], 将对应页面内存映像比特位置1
        "sall $12,%%ecx\n\t"            // 页面数*4K = 相对页面起始地址
        "addl %2,%%ecx\n\t"             // 再加上低端内存地址，得到页面实际物理起始地址
        "movl %%ecx,%%edx\n\t"          // 将页面实际起始地址 => edx
        "movl $1024,%%ecx\n\t"          // 1024 => ecx
        "leal 4092(%%edx),%%edi\n\t"    // 4096+edx => edi （该页面的末端）
        "rep ; stosl\n\t"               // 将edi所指内存清零 （反方向，即将该页面清零）
        " movl %%edx,%%eax\n"           // 将页面起始地址 => eax（返回值）
        "1: cld"
        :"=a" (__res)
        :"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
        "D" (mem_map+PAGING_PAGES-1)
        );
    return __res;
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

        free_page(0xfffff000 & *dir);
        *dir = 0;
    }

    // invalidate();       // 刷新页变换高速缓冲
    return 0;
}

// 把一物理内存页映射到线性地址空间
// 在处理缺页异常 do_no_page() 中会调此函数 
// page - 分配的主内存中某一页（页帧，页框）的指针
// address - 线性地址
unsigned long put_page(unsigned long page, unsigned long address) {
    unsigned long *pg_tbl, tmp;

    if (page < LOW_MEM || page >= HIGH_MEMORY)
        printk("Trying to put page %x at %x\n", page, address);

    if (mem_map[MAP_NR(page)] != 1)     // 该page页面是否是已经申请的页面，如果没有发出警告
        printk("mem_map disagrees with %x at %x\n", page, address);

    // 根据线性地址 address 计算其在页目录表中对应的目录项指针，并从中取得二级页表地址
    pg_tbl = (unsigned long *)((address >> 20) & 0xffc);

    printk("Params: pg_tbl = %x, entry = %x\n", pg_tbl, (address >> 12) & 0x3ff);
    if((*pg_tbl) & 1) {   // 如果该目录项有效（P=1）, 即指定的页表在内存中
        printk("Page table now available\n");
        pg_tbl = (unsigned long *)(*pg_tbl & 0xfffff000);
    }
    else {               // 否则申请一空闲页面给页表使用，并在相应目录项置相应标志，然后把页表地址放到pg_tbl变量中
        if (!(tmp = get_free_page())) {
            printk("NO FREE PAGE!");
            return 0;
        }

        *pg_tbl = tmp | 7;
        printk("Tmp = %x\n", tmp);
        printk("Page Table = %x\n", *pg_tbl);
        pg_tbl = (unsigned long *) tmp;
    }
    printk("Put Page Success\n");
    // 最后找到的页表 page_table 中设置相关页表项内容
    // 即把物理页面 page 的地址填入表项同时置位3个标志（U/S、W/R、P）
    // 该页表项在页表中的索引值等于线性地址 位21-位12 组成的 10bit 值，每个页表共可有 1024 项（0 -- 0x3ff）
    pg_tbl[(address >> 12) & 0x3ff] = page | 7;
    // invalidate();    // 不需要刷新页变换高速缓冲
    return page;
}


// 获取一页物理内存并将其映射到指定的线性地址处
// address - 线性地址 
void get_empty_page(unsigned long address) {
    unsigned long tmp;
    if (!(tmp = get_free_page()) || !put_page(tmp, address)) {
        free_page(tmp);
        oom();
    }
    return;
}

// 解除页面的写入保护
// 用于页异常中断过程中写保护异常处理（写时复制）
// 本函数供 do_wp_page() 调用
// 若页面没有共享，则把页面设置成可写，然后退出
// 若页面共享状态，则需重新申请一新页面并复制被写页面内容,以供写进程单独使用，共享被取消
void un_wp_page(unsigned long *table_entry) {
    unsigned long old_page, new_page;
    old_page = *table_entry & 0xfffff000;

    if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)] == 1) {    // 页面没有被共享
        *table_entry |= 2;      // 置位 R/W
        invalidate();
        return;
    }

    // 申请一页空闲页面给执行写操作的进程单独使用，取消页面共享
    // 在刷新页变换高速缓冲之后，最后将原页面内容复制到新页面上
    if (!(new_page = get_free_page()))
        oom();

    if (old_page >= LOW_MEM)
        mem_map[MAP_NR(old_page)]--;

    *table_entry = new_page | 7;
    invalidate();
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
    // dir = (unsigned long *)((address >> 20) & 0xffc)
    // pag = *(dir)
    if(!( (page = *((unsigned long *)((address >> 20) & 0xffc)) ) & 1)) {
        return ;
    }

    // 取页表首地址
    page &= 0xfffff000;
    page += ((address >> 10) & 0xffc);      // 因为pape 是 unsigned long类型，每项4个字节，相当于 >>12 然后 << 2
    if(*(unsigned long *)page & 3 == 1) {   // 页表P = 1, R/W = 0
        un_wp_page((unsigned long *)page);
    }
    return;
}

// 当用户试图往一共享页上写时，该函数处理已存在的内存页面（写时复制）
// 它是通过将页面复制到一个新的地址上并递减原来页面共享计数值实现的
// 如果它在代码空间，就显示段出错信息并退出
// 执行写保护页面处理
// 页异常中断处理过程中调用的c函数，在 page.s 中被调用
// error_code - cpu 自动产生
// address - 页面线性地址
void do_wp_page(unsigned long error_code, unsigned long address) {
    un_wp_page((unsigned long *) (((address >> 10) & 0xffc) +
        ((*(unsigned long *)((address >> 20) & 0xffc))) & 0xfffff000));
}

// 执行缺页处理
// 访问不存在页面的处理函数，在页异常中断处理过程中调用，在 page.s 中调用
// error_code - cup 自动产生
// address - 页面线性地址
void do_no_page(unsigned long error_code, unsigned long address) {
    unsigned long tmp;
    unsigned long page;

    address &= 0xfffff000;
    if (!(page = get_free_page()))
        oom();
    if (put_page(page, address))
        return;
    free_page(page);
    oom();
}


