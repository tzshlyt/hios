
#include <linux/kernel.h>

int mmtest_main(void);

void main() {
    int ret;
    video_init(); 
    printk("Welcome to Linux0.11 Kernel Mode(NO)\n");


    // 初始化物理页内存, 将 1MB - 16MB 地址空间的内存进行初始
    mem_init(0x100000, 0x1000000);
    ret = mmtest_main();

    while(1);
}
