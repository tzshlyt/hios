// 用于创建子进程

#include <linux/sched.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/kernel.h>


extern void write_verify(unsigned long address);

long last_pid = 0;		// 最新进程号，其值会由 get_empty_process() 生成

// 空间区域写前验证
// 80486以上cup可以通过控制寄存器CR0达到本函数目的
void verify_area(void *addr, unsigned int size) {
	unsigned long start;
	start = (unsigned long) addr;
	size += start & 0xfff;			// 获得指定起始位置addr在所在页面中的偏移值
	start &= 0xfffff000;			// 此时 start 是当前进程空间中的逻辑地址

	start += get_base(current->ldt[2]);
	while (size) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}

// 复制内存页表，为新任务在线性地址空间中设置代码段和数据段基地址、限长，并复制页表
// nr: 新任务号
// p: 新任务数据结构指针
// 操作成功返回 0，否则返回错误号
int copy_mem(int nr, struct task_struct *p) {
	unsigned long old_data_base, new_data_base, data_limit;
    unsigned long old_code_base, new_code_base, code_limit;

    code_limit = get_limit(0x0f);									// 0x0f 代码段选择符
    data_limit = get_limit(0x17);									// 0x17 数据段选择符
    old_code_base = get_base(current->ldt[1]);
    old_data_base = get_base(current->ldt[2]);
    if(old_code_base != old_data_base) 								// This should never happen
        panic("Codeseg not fully overlapped with dataseg");			// 由于Linux0.11内核还不支持代码和数据段分立的情况，因此这里需要检查代码段和数据段基址和限长是否都分别相同。
    if(data_limit < code_limit)										// 否则内核显示出错信息，并停止运行。
        panic("bad data limit");

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

// 下面是主要的fork子程序。它复制系统进程信息（task【n】）
// 并且设置必要的寄存器。它还整个地复制数据段。
// 该函数的参数是进入系统调用中断处理过程前逐步压入栈的各寄存器的值
// 在函数参数列表中的参数，顺序是这样的，参数列表最后面的参数，对应在汇编中最先压栈的参数（就是离栈顶最远的参数)
// 然后这里的参数涉及到很多，解释一下为什么会有这些参数
// 首先 CPU 进入中断调用，压栈SS, ESP, 标志寄存器 EFLAGS 和返回地址CS:EIP
// 然后是system_call里压栈的寄存器 ds, es, fs, edx, ecx, ebx
// 然后是调用sys_fork函数时,压入的函数返回地址
// 以及sys_fork里压栈的那些参数 gs, esi, edi, ebp, eax(nr
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
    i = eflags;
    eflags = i;
    /* End */

	p = (struct task_struct *) get_free_page();
	if (!p)
		return -1;
	task[nr] = p;

	// NOTE!: the following statement now work with gcc 4.3.2 now, and you
	// must compile _THIS_ memcpy without no -O of gcc.#ifndef GCC4_3
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */ // 这里仅仅复制PCB(task_struct)进程结构
	// 初始化 PCB, TSS
	p->state = TASK_UNINTERRUPTIBLE;		// 先将新进程的状态置为不可中断等待状态，以防止内核调度其执行
	p->pid = last_pid;						// find_empty_process() 得到
	p->father = current->pid;
	p->counter = p->priority;				// 运行时间片
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;				// 用户态和核心态运行时间
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;				// 进程开始运行时间

	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;		// esp0 指向页顶端, ss0:esp0 用作程序在内核态执行时的栈
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	// p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.ebx = ebx;
	p->tss.edx = edx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);					// 任务局部表描述符的选择符（LDT描述符在GDT中）
	p->tss.trace_bitmap = 0x80000000;		// 高16位有效
	// 如果之前的进程使用了协处理器，这里复位TS标志
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	// 复制进程页表
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

	// 设置好TSS和LDT描述符，然后将任务置位
    // 就绪状态，可以被OS调度
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */

	return last_pid; 	// 父进程返回儿子的pid
}

// 为新进程取得不重复得进程号 last_pid
int find_empty_process(void) {
	int i;

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