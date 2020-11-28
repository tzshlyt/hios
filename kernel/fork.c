// 用于创建子进程

#include <linux/sched.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <serial_debug.h>


extern void write_verify(unsigned long address);

long last_pid = 0;		// 最新进程号，其值会由 get_empty_process() 生成

// 进程空间区域写前验证函数
// 对于80386 CPU，在执行特权级 0 代码时不会理会用户空间中的页面是否是也保护的，
// 因此在执行内核代码时用户空间中数据页面来保护标志起不了作用，写时复制机制
// 也就失去了作用。verify_area()函数就用于此目的。但对于80486或后来的CPU，其
// 控制寄存器 CRO 中有一个写保护标志WP(位16)，内核可以通过设置该标志来禁止特权
// 级0的代码向用户空间只读页面执行写数据，否则将导致发生写保护异常。
// 从而486以上CPU可以通过设置该标志来达到本函数的目的。
// 该函数对当前进程逻辑地址从addr到addr+size这一段范围以页为单位执行写操作前
// 的检测操作。由于检测判断是以页面为单位进行操作，因此程序首先需要找出addr所
// 在页面开始地址start，然后start加上进程数据段基址，使这个start变成CPU 4G线性
// 空间中的地址。最后循环调用write_verify()对指定大小的内存空间进行写前验证。
// 若页面是只读的，则执行共享检验和复制页面操作。
void verify_area(void *addr, unsigned int size) {
	unsigned long start;

	// 首先将起始地址start调整为其所在左边界开始位置，同时相应地调整验证区域大小。
    // 下句中的 start & 0xfff 用来获得指定起始位置addr(也即start)在所在
    // 页面中的偏移值，原验证范围 size 加上这个偏移值即扩展成以 addr 所在页面起始
    // 位置开始的范围值。因此在下面也需要把验证开始位置 start 调整成页面边界值。
	start = (unsigned long) addr;
	size += start & 0xfff;			// 获得指定起始位置addr在所在页面中的偏移值
	start &= 0xfffff000;			// 此时 start 是当前进程空间中的逻辑地址

    // 下面 start 加上进程数据段在线性地址空间中的起始基址，变成系统整个线性空间
    // 中的地址位置。对于linux-0.11内核，其数据段和代码在线性地址空间中的基址
    // 和限长均相同。
	start += get_base(current->ldt[2]);
	while ((long)size > 0) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}

// 复制内存页表
// nr: 新任务号
// p: 新任务数据结构指针
// 操作成功返回 0，否则返回错误号
// 该函数为新任务在线性地址空间中设置代码段和数据段基址、限长，并复制页表。
// 由于Linux系统采用了写时复制(copy on write)技术，因此这里仅为新进程设置自己的页目录表项和页表项，
// 而没有实际为新进程分配物理内存页面。此时新进程与其父进程共享所有内存页面。
// 操作成功返回0，否则返回出错号。
int copy_mem(int nr, struct task_struct *p) {
	unsigned long old_data_base, new_data_base, data_limit;
    unsigned long old_code_base, new_code_base, code_limit;

	// 首先取当前进程局部描述符表中代表中代码段描述符和数据段描述符项中的的段限长(字节数)。
    // 0x0f是代码段选择符：0x17是数据段选择符。
    // 然后取当前进程代码段和数据段在线性地址空间中的基地址。由于Linux-0.11内核
    // 还不支持代码和数据段分立的情况，因此这里需要检查代码段和数据段基址
    // 和限长是否都分别相同。否则内核显示出错信息，并停止运行。
    code_limit = get_limit(0x0f);									// 0x0f 代码段选择符
    data_limit = get_limit(0x17);									// 0x17 数据段选择符
    old_code_base = get_base(current->ldt[1]);
    old_data_base = get_base(current->ldt[2]);
    if(old_code_base != old_data_base) 								// This should never happen
        panic("Codeseg not fully overlapped with dataseg");			// 由于Linux0.11内核还不支持代码和数据段分立的情况，因此这里需要检查代码段和数据段基址和限长是否都分别相同。
    if(data_limit < code_limit)										// 否则内核显示出错信息，并停止运行。
        panic("bad data limit");

 	// 然后设置创建中的新进程在线性地址空间中的基地址等于(64MB * 其任务号)，
    // 并用该值设置新进程局部描述符表中段描述符中的基地址。接着设置新进程
    // 的页目录表项和页表项，即复制当前进程(父进程)的页目录表项和页表项。
    // 此时子进程共享父进程的内存页面。正常情况下copy_page_tables()返回0，
    // 否则表示出错，则释放刚申请的页表项。
	new_data_base = new_code_base = (unsigned int)nr * 0x4000000; // 64MB * nr
    p->start_code = new_code_base;
    set_base(p->ldt[1], new_code_base);
    set_base(p->ldt[2], new_data_base);
    if(copy_page_tables(old_data_base, new_data_base, data_limit)) {
        printk("free_page_tables: from copy_mem\n");
        free_page_tables(new_data_base, data_limit);
        return -1;
    }
    return 0;
}

// 复制进程
// 下面是主要的fork子程序。它复制系统进程信息（task[n]）
// 并且设置必要的寄存器。它还整个地复制数据段。
// 该函数的参数是进入系统调用中断处理过程 system_call.s 开始，直到调用本系统调用处理过程 和 调用本函数前时
// 逐步压入栈的各寄存器的值
// 在函数参数列表中的参数，顺序是这样的，参数列表最后面的参数，对应在汇编中最先压栈的参数（就是离栈顶最远的参数)
// 这些在 system_call.s 程序中逐步压入栈的参数 包括:
// 1. CPU 执行中断指令压入的用户栈地址SS, ESP, 标志寄存器 EFLAGS 和返回地址 CS:EIP
// 2. 刚进入 system_call 时压栈的寄存器 ds, es, fs, edx, ecx, ebx
// 3. 调用 sys_fork 函数时,压入的函数返回地址(用参数 none 表示)
// 4. 在调用copy_process()之前压入栈的 gs, esi, edi, ebp 和 eax(nr) 值
//    其中 nr 是调用find_empty_process() 分配的任务数组项号
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;

	/* Only for emit the warning! */
    i = none;
    none = i;
    /* End */

	// 首先为新任务数据结构分配内存。如果内存分配出错，则返回出错码并退出。
    // 然后将新任务结构指针放入任务数组的nr项中。其中nr为任务号，由前面
    // find_empty_process()返回。接着把当前进程任务结构内容复制到刚申请到
    // 的内存页面p开始处。
	p = (struct task_struct *) get_free_page();
	if (!p)
		return -1;
	task[nr] = p;

	// NOTE!: the following statement now work with gcc 4.3.2 now, and you
	// must compile _THIS_ memcpy without no -O of gcc.#ifndef GCC4_3
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */ // 这里仅仅复制PCB(task_struct)进程结构
	// 随后对复制来的进程结构内容进行一些修改，作为新进程的任务结构。
    // 先将进程的状态置为不可中断等待状态，以防止内核调度其执行。
    // 然后设置新进程的进程号pid和父进程号father，并初始化进程运行时间片值等于其priority值(一般为15个嘀嗒)
    // 接着复位新进程的信号位图、报警定时值、会话(session)领导标志leader、进程
    // 及其子进程在内核和用户态运行时间统计值，还设置进程开始运行的系统时间start_time.
	p->state = TASK_UNINTERRUPTIBLE;		// 先将新进程的状态置为不可中断等待状态，以防止内核调度其执行
	p->pid = last_pid;						// find_empty_process() 得到
	p->father = current->pid;				// 设置父进程
	p->counter = p->priority;				// 运行时间片值
	p->signal = 0;							// 信号位图置0, 4个字节32位
	p->alarm = 0;							// 报警定时值(滴答数)
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;				// 用户态和核心态运行时间
	p->cutime = p->cstime = 0;				// 子进程用户态和和核心态运行时间
	p->start_time = jiffies;				// 进程开始运行时间(当前时间滴答数）
 	// 再修改任务状态段TSS数据，由于系统给任务结构p分配了1页新内存，所以(PAGE_SIZE+
    // (long)p)让esp0正好指向该页顶端。ss0:esp0用作程序在内核态执行时的栈。另外，
    // 每个任务在GDT表中都有两个段描述符，一个是任务的TSS段描述符，另一个是任务的LDT
    // 表描述符。下面语句就是把GDT中本任务LDT段描述符和选择符保存在本任务的TSS段中。
    // 当CPU执行切换任务时，会自动从TSS中把LDT段描述符的选择符加载到ldtr寄存器中。
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;		// esp0 指向页顶端, ss0:esp0 用作程序在内核态执行时的栈
	p->tss.ss0 = 0x10;						// 内核态栈的段选择符(与内核数据段相同)
	p->tss.eip = eip;						// 指令代码指针
	p->tss.eflags = eflags;					// 标志寄存器
	p->tss.eax = 0;							// 这是当fork()返回时新进程会返回0的原因所在
	p->tss.ecx = ecx;
	p->tss.ebx = ebx;
	p->tss.edx = edx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;				// 段寄存器仅16位有效
	p->tss.cs = cs & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);					// 任务局部表描述符的选择符（LDT描述符在GDT中）
	p->tss.trace_bitmap = 0x80000000;		// 高16位有效
	// 如果当前任务使用了协处理器，就保存其上下文。汇编指令clts用于清除控制寄存器CRO中
    // 的任务已交换(TS)标志。每当发生任务切换，CPU都会设置该标志。该标志用于管理数学协
    // 处理器：如果该标志置位，那么每个ESC指令都会被捕获(异常7)。如果协处理器存在标志MP
    // 也同时置位的话，那么WAIT指令也会捕获。因此，如果任务切换发生在一个ESC指令开始执行
    // 之后，则协处理器中的内容就可能需要在执行新的ESC指令之前保存起来。捕获处理句柄会
    // 保存协处理器的内容并复位TS标志。指令fnsave用于把协处理器的所有状态保存到目的操作数
    // 指定的内存区域中。
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	// 复制进程页表
	// 即在线性地址空间中设置新任务代码段和数据段描述符中的基址和限长，并复制页表。
    // 如果出错(返回值不是0)，则复位任务数组中相应项并释放为该新任务分配的用于任务结构的内存页。
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((unsigned long) p);
		return -1;
	}
	// 如果父进程中有文件是打开的，则将对应文件的打开次数增1。
	// 因为这里创建的子进程会与父进程共享这些打开的文件。
	// 将当前进程（父进程）的pwd， root 和executable引用次数均增1。
	// 与上面同样的道理，子进程也引用了这些i节点。
	// for (i=0; i<NR_OPEN;i++)
	// 	if ((f=p->filp[i]))
	// 		f->f_count++;
	// if (current->pwd)
	// 	current->pwd->i_count++;
	// if (current->root)
	// 	current->root->i_count++;
	// if (current->executable)
	// 	current->executable->i_count++;

	// 把进程1的任务状态描述符表 和局部描述符表挂接在全局描述符表中，
	// 标志着系统从此具备操作新进程的能力
	// 随后GDT表中设置新任务 TSS 段和 LDT 段描述符项。这两个段的限长均被设置成104字节。
    // set_tss_desc() 和 set_ldt_desc() 在 system.h 中定义。"gdt+(nr<<1)+FIRST_TSS_ENTRY"是
    // 任务 nr 的TSS描述符项在全局表中的地址。因为每个任务占用GDT表中2项，
    // 因此上式中要包括'(nr<<1)'.程序然后把新进程设置成就绪态。
    // 另外在任务切换时，任务寄存器 tr 由 CPU 自动加载。最后返回新进程号。
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */   // 就绪状态，可以被OS调度
	return last_pid; 	// 父进程返回儿子的 pid

	// 进程1创建完成，这使得进程1已经具备进程0的全部能力，它可以在主机中正常运行了。接下来进程0要切换到进程1。
}

// 为新进程取得不重复得进程号 last_pid
// 函数返回: 在任务数组中的任务号(数组项)
int find_empty_process(void) {
	int i;

	// 首先获取新的进程号。如果last_pid增1后超出进程号的整数表示范围，
    // 则重新从1开始使用pid号。然后在任务数组中搜索刚设置的pid号是否已经被任何任务使用。
    // 如果是则跳转到函数开始出重新获得一个pid号。接着在任务数组中为新任务寻找一个空闲项，
    // 并返回项号。last_pid是一个全局变量，不用返回。如果此时任务数组中64个项已经被全部
    // 占用，则返回出错码。
	// repeat:
	// 	if ((++last_pid)<0) last_pid=1;			// 超出正数范围
	// 	for(i=0 ; i<NR_TASKS ; i++)
	// 		if (task[i] && task[i]->pid == last_pid) goto repeat;
	// for(i=1 ; i<NR_TASKS ; i++)
	// 	if (!task[i])
	// 		return i;
	// return -1;

	long tmp = last_pid;    // 记录最初起始进程号，用于标记循环结束

    while(1) {
        for(i = 0; i < NR_TASKS; i++) {
            if(task[i] && task[i]->pid == last_pid)
                break;
        }
        if(i == NR_TASKS) break;
        if((++last_pid) == tmp)
            break;
        // 判断last_pid 是否超出进程号数据表示范围，如果超出
        // 则置为1
        if(last_pid < 0) last_pid = 1;
    }
    for(i = 1; i < NR_TASKS; i++)
        if(!task[i])
            return i;
    return -1;
}