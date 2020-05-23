
#include <linux/kernel.h>
#include <stdarg.h>

void main() {
    video_init(); 
    printk("Welcome to Linux0.11 Kernel Mode(NO)\n");
    printk(" Hello world \n"); 

    while(1);
}
