#ifndef _KERNEL_H
#define _KERNEL_H

void printk(char *fmt, ...);     // Simplest printk function to use
void video_putchar_at(char ch, int x, int y, char attr);
void video_putchar(char ch);
void video_clear();
void video_init();
int video_getx();
int video_gety();
void roll_screen();
void printnum(int num, int base, int sign);
int get_cursor();
void update_cursor(int row, int col);
void panic(const char *str);
void verify_area(void *addr,unsigned int size);

extern int video_x, video_y;

#define suser() (current->euid == 0)

#endif