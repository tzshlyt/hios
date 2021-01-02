// 根据目录名或文件名寻找到对应i节点，以及关于目录的建立和删除、目录项的建立和删除
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <linux/fs.h>

#include <fcntl.h>
#include <const.h>
#include <errno.h>
#include <sys/stat.h>

// 下面是访问模式宏
// x是头文件fentl.h中定义的访问标志
// 这个宏根据文件访问标志x的值来索引双引号中对应的数值
// 双引号中有4个八进制数值（实际表示4个控制字符），分别表示读、写、执行的权限为：r,w,rw和wxrwxrwx
// 并且分别对应x的索引值 0-3
// 例如，如果x为2，则该宏返回八进制006，表示可读可写（rw）
// 另外，O_ACCMODE＝ 00003，是索引值x的屏蔽码。
#define ACC_MODE(x) ("\004\002\006\377"[(x)&O_ACCMODE])

#define MAY_EXEC 1              // 可执行（可进入）
#define MAY_WRITE 2             // 可写
#define MAY_READ 3              // 可读

//// 检测文件访问权限
// 参数：inode - 文件的i节点指针；mask - 访问属性屏蔽码
// 返回：访问许可返回1，否则返回0
static int permission(struct m_inode * inode, int mask) {
	int mode = inode->i_mode;

    // 如果i节点有对应的设备，但该i节点的连接计数值等于0，表示该文件已被删除，则返回。
    // 否则，如果进程的有效用户ID(euid)与i节点的用户id相同，则取文件宿主的访问权限。
    // 否则如果与组id相同，则取组用户的访问权限。
	if (inode->i_dev && !inode->i_nlinks)            // 该文件已被删除
		return 0;
	else if (current->euid == inode->i_uid)
		mode >>= 6;                                 // 文件宿主的访问权限
	else if (current->egid == inode->i_gid)
		mode >>= 3;                                 // 组用户的访问权限
	if (((mode & mask & 0007) == mask) || suser())
		return 1;
	return 0;
}

//// 指定长度字符串比较函数
// 参数：len - 比较的字符串长度；name - 文件名指针；de - 目录项结构
// 返回：相同返回1，不同返回0.
// 下面函数中的寄存器变量same被保存在eax寄存器中，以便高效访问。
static int match(int len, const char * name, struct dir_entry * de) {
    register int same;
    if (!de || !de->inode || len > NAME_LEN)
        return 0;
    if (len < NAME_LEN && de->name[len])
        return 0;

    // 在用户数据空间(fs段)执行字符串的比较操作
    // %0 - eax（比较结果same）
    // %1 - eax (eax初值0)
    // %2 - esi(名字指针)
    // %3 - edi(目录项名指针)
    // %4 - ecx(比较的字节长度值len)
    // cld 使 DF(Direction Flag) 复位
    // repe 是一个串操作前缀，它重复串操作指令，每重复一次 ECX 的值就减 1 直到 CX 为0或 ZF 为0时停止
    // cmpsb 是字符串比较指令，把 ESI 指向的数据与 EDI 指向的数一个一个的进行比较
    // 当 repe cmpsb配合使用时就是字符串比较啦，当相同时继续比较，不同时不比较
    // setz 取标志寄存器中 ZF 的值, 放到 AL 中
    __asm__("cld\n\t"                   // 清空方向位
		"fs ; repe ; cmpsb\n\t"         // 用户空间执行循环比较[esi++]和[edi++]
		"setz %%al"                     // 若结果一样（zf=1）则置 al = 1
		:"=a" (same)
		:"0" (0),"S" ((long) name),"D" ((long) de->name),"c" (len)
		);
	return same;
}

//// 查找指定目录和文件名的目录项
// 参数：*dir - 指定目录i节点的指针；
//      *name - 文件名；
//      namelen - 文件名长度；
// 返回：成功则函数高速缓冲区指针，并在 *res_dir 处返回的目录项结构指针
static struct buffer_head * find_entry(struct m_inode ** dir,
	const char * name, int namelen, struct dir_entry ** res_dir) {

    int entries;
	int block, i;
	struct buffer_head * bh;
	struct dir_entry * de;
	struct super_block * sb;

    if (namelen > NAME_LEN)
		namelen = NAME_LEN;

    // 计数该目录中目录项数
    entries = (int)((*dir)->i_size / (sizeof(struct dir_entry)));
    *res_dir = NULL;
    if (!namelen)
        return NULL;
    // 对目录项文件名是'..'的情况进行特殊处理
    // 如果当前进程指定的 根i节点 就是函数参数指定的目录
    // 则说明对于本进程来说，这个目录就是它伪根目录，即进程只能访问该目录中的项而不能后退到其父目录中去
    // 也即对于该进程本目录就如同是文件系统的根目录，因此我们需要将文件名修改为‘.’
    // 否则，如果该目录的i节点号等于ROOT_INO（1号）的话，说明确实是文件系统的 根i节点
    // 则取文件系统的超级块
    // 如果被安装到的i节点存在，则先放回原i节点，然后对被安装到的i节点进行处理
    // 于是我们让*dir指向该被安装到的i节点；并且该i节点的引用数加1.
    if (namelen == 2 && get_fs_byte(name) == '.' && get_fs_byte(name+1) == '.') {  // .. 目录
        if ((*dir) == current->root) {
            namelen = 1;
        } else if ((*dir)->i_num == ROOT_INO) {         // 该目录的i节点号等于ROOT_INO（1号）
            sb = get_super((*dir)->i_dev);
            if (sb->s_imount) {
                iput(*dir);
                (*dir) = sb->s_imount;
                (*dir)->i_count++;
            }
        }
    }

    if (!(block = (*dir)->i_zone[0]))           // 该目录竟然不含数据
        return NULL;
    if (!(bh = bread((*dir)->i_dev, block)))    // 目录项数据块
        return NULL;

    i = 0;
    de = (struct dir_entry *) bh->b_data;
    while (i < entries) {
        // 如果当前目录项数据块已经搜索完，还没有找到匹配的目录项，则释放当前目录项数据块。
        // 再读入目录的下一个逻辑块
        // 若这块为空，则只要还没有搜索完目录中的所有目录项，就跳过该块继续读目录的下一逻辑块
        // 若该块不空，就让 de 指向该数据块，然后在其中继续搜索
        if ((char *)de >= BLOCK_SIZE+bh->b_data) {
            brelse(bh);
            bh = NULL;
            if ( !(block = bmap(*dir, (int)((unsigned int)i/DIR_ENTRIES_PER_BLOCK)) ) ||       // 读入目录的下一个逻辑块
                 !(bh = bread((*dir)->i_dev, block)) ) {
                    i += (int)DIR_ENTRIES_PER_BLOCK;
                    continue;
            }
            de = (struct dir_entry *) bh->b_data;
        }

        // 找到匹配的目录项
        if (match(namelen, name, de)) {
			*res_dir = de;
			return bh;
		}
        de++;
        i++;
    }

    // 目录项中搜索完了没有找到，释放目录的数据块
    brelse(bh);
    return NULL;
}

//// 根据指定的目录和文件名添加目录项
// 参数：dir - 指定目录的i节点
//      name - 文件名
//      namelen - 文件名长度
// 返回：高速缓冲区指针；res_dir - 返回的目录项结构指针
static struct buffer_head * add_entry(struct m_inode * dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
    return NULL;
}

//// 搜寻指定路径的目录（或文件名）的i节点
// 参数：pathname - 路径名
// 返回：目录或文件的i节点指针
static struct m_inode * get_dir(const char * pathname) {
    char c;
    const char * thisname;
    struct m_inode * inode;
    struct buffer_head * bh;
    int namelen, inr, idev;
    struct dir_entry * de;

    // 搜索操作会从当前任务结构中设置的根（或伪根）i节点或当前工作目录i节点开始
    if (!current->root || !current->root->i_count)          // 没有根节点
        panic("No root indoe");
    if (!current->pwd || !current->pwd->i_count)            // 没有当前工作目录
        panic("No cwd inode");

    // 如果第1个字符是'/'，则说明是绝对路径名，则从根i节点开始操作
    // 否则第一个字符是其他字符，则表示给定的相对路径名
    if ((c = get_fs_byte(pathname)) == '/') {
        inode = current->root;
        pathname++;
    } else if (c) {
        inode = current->pwd;
    } else {
        return NULL;
    }

    // 此时 inode 是根目录 或者 当前路径 i 节点
    inode->i_count++;
    while (1) {
        thisname = pathname;
        if (!S_ISDIR(inode->i_mode) || !permission(inode, MAY_EXEC)) {      // 不是文件夹 或者 没有权限
            iput(inode);
            return NULL;
        }

        // 每次循环我们处理路径名中一个目录名（或文件名）部分
        // 注意！如果路径名中最后一个名称也是一个目录名，但其后面没有加上'/'字符，则函数不会返回该最后目录的i节点！
        // 例如：对于路径名/usr/src/linux，该函数将只返回src/目录名的i节点
        for (namelen = 0; (c = get_fs_byte(pathname++)) && (c != '/'); namelen++) {
            /* nothing */ ;
        }

        if (!c) {   // 到路径末尾，并已到达最后指定目录名或文件名，则返回该i节点指针退出。
            return inode;
        }

        if(!(bh = find_entry(&inode, thisname, namelen, &de))) {
            iput(inode);
            return NULL;
        }

        inr = de->inode;    // 当前目录名部分的 i 节点号， 如 /dev/tty 第一次返回 dev 的 i 节点
        idev = inode->i_dev;
        brelse(bh);
        iput(inode);
        if (!(inode = iget(idev, inr)))     // 取 i 节点内容，继续循环处理下一目录
            return NULL;
    }
}

// 参数：pathname - 目录路径名
//      namelen - 路径名长度
//      name - 返回的最顶层目录名
// 返回：指定目录名最顶层目录的i节点指针和最顶层目录名称及长度。出错时返回NULL。
// 注意！！这里"最顶层目录"是指路径名中最靠近末端的目录。
static struct m_inode * dir_namei(const char * pathname, int * namelen, const char ** name) {
    char c;
    const char * basename;
    struct m_inode * dir;

    if (!(dir = get_dir(pathname)))
        return NULL;
    basename = pathname;
    while ((c = get_fs_byte(pathname++))) {
        if (c == '/')
			basename=pathname;
    }
    *namelen = pathname-basename-1;
	*name = basename;
	return dir;
}

//// 文件打开namei函数
// 返回：成功返回0，否则返回出错码
int open_namei(const char * pathname, int flag, int mode, struct m_inode ** res_inode) {
    const char * basename;
    int inr, dev, namelen;
    struct m_inode * dir, * inode;
    struct buffer_head * bh;
    struct dir_entry * de;

    // 如果文件访问模式标志是只读(0)，但是文件截零标志 O_TRUNC 却置位了, 则在文件打开标志中添加只写 O_WRONLY
    // 这样做的原因是由于截零标志 O_TRUNC 必须在文件可写情况下才有效
    // 然后使用当前进程的文件访问许可屏蔽码，屏蔽掉给定模式中的相应位
    // 并添上对普通文件标志I_REGULAR
    // 该标志将用于打开的文件不存在而需要创建文件时，作为新文件的默认属性
    if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
        flag |= O_WRONLY;
    mode &= 0777 & ~current->umask;
    mode |= I_REGULAR;

    // 然后根据指定的路径名寻找对应的i节点，以及最顶端目录名及其长度, 例如 /dev/tty0, dir -> tty0
    if (!(dir = dir_namei(pathname, &namelen, &basename)))
        return -ENOENT;

    // 如果最顶端目录名长度为0, 例如 /dev/tty0/, 即路径后面加了 /
    if (!namelen) {
        // 若操作不是读写、创建和文件长度截0，则表示是在打开一个目录名文件操作
        // 于是直接返回该目录的i节点并返回 0 退出
        // 否则说明进程操作非法，于是放回该i节点，返回出错码
        if (!(flag & (O_ACCMODE | O_CREAT | O_TRUNC))) {
            *res_inode = dir;
            return 0;
        }
        iput(dir);
        return -EISDIR;
    }

    // 查找路径最后文件名对应的目录结构de，例如 /dev/tty0 , 根据 dev 的 i 节点 dir，查找 tty0 的目录项结构de
    // 并同时得到该目录项所在的高速缓冲区指针
    bh = find_entry(&dir, basename, namelen, &de);
    if (!bh) {                                  // 没有找到对应文件名的目录项, 因此只可能是创建文件操作
        if (!(flag & O_CREAT)) {                // 不是创建文件
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir, MAY_WRITE)) {      // 用户在该目录没有写的权力
			iput(dir);
			return -EACCES;
		}

        // 到这是创建操作并且有写操作许可
        // 申请一个新的i节点给指定的文件使用
        inode = new_inode(dir->i_dev);
        if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
        inode->i_uid = current->euid;
        inode->i_mode = (unsigned short)mode;
		inode->i_dirt = 1;
		bh = add_entry(dir, basename, namelen, &de);    // 在指定目录dir中添加一个新目录项
        if (!bh) {                                      // 添加失败
            inode->i_nlinks--;
            iput(inode);
            iput(dir);
            return -ENOSPC;
        }
        de->inode = inode->i_num;                       // 置i节点号为新申请的i节点的号码
        bh->b_dirt = 1;                                 // 置高速缓冲区已修改标志
        brelse(bh);                                     // 释放该高速缓冲区
        iput(dir);
        *res_inode = inode;                             // 返回新目录项的i节点指针
        return 0;
    }

    // 取文件名对应目录项结构的操作成功，则说明指定打开的文件已经存在
    // 取出该目录项的i节点号和其所在设备号
    // 并释放该高速缓冲区以及放回目录的i节点
    // 如果此时堵在操作标志O_EXCL置位，但现在文件已经存在，则返回文件已存在出错码退出
    inr = de->inode;
    dev = dir->i_dev;
    brelse(bh);
    iput(dir);
    if (flag & O_EXCL)
		return -EEXIST;
    // 然后我们读取该目录项的 i 节点内容
    // 若该i节点是一个 目录i节点 并且访问模式是只写 或 读写, 或者 没有访问的许可权限
	if (!(inode = iget(dev, inr)))
		return -EACCES;
    if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
	    !permission(inode, ACC_MODE(flag))) {
		iput(inode);
		return -EPERM;
	}
    return 0;
}