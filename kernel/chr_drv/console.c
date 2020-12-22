#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/system.h>
#include <serial_debug.h>

extern void keyboard_interrupt(void);

#define PAGE_SIZE 4096
#define VIDEO_MEM 0xB8000   // 屏幕映射内存地址, 固定地址

#define VIDEO_X_SZ 80       // 最大列
#define VIDEO_Y_SZ 25       // 最大行, 设备决定
#define TAB_LEN 8

#define CALC_MEM(x, y) (2*((x) + 80*(y))

#define WHITE_ON_BLACK 0x0f // 字符属性，背景色
#define RED_ON_WHITE 0xf4

/* 屏幕设备 I/O ports */
#define REG_SCREEN_CTRL 0x3D4
#define REG_SCREEN_DATA 0x3D5

struct video_info {
    unsigned int retval;        // Return value
    unsigned int colormode;     // Color bits
    unsigned int feature;       // Feature settings
};

int video_x, video_y;

char *video_buffer = (char *)VIDEO_MEM;

void video_init() {
    // struct video_info *info = 0x9000;   // setup.s 中保存了屏幕设备信息

    video_x = 0;
    video_y = 0;
    video_clear();
    update_cursor(video_y, video_x);
}

int video_getx() {
    return video_x;
}

int video_gety() {
    return video_y;
}

void update_cursor(int row, int col) {
    unsigned int pos = ((unsigned int)row * VIDEO_X_SZ) + (unsigned int)col;
    // LOW Cursor port to VGA Index Registe
    outb(0x0f, REG_SCREEN_CTRL);
    outb((unsigned char)(pos & 0xff), REG_SCREEN_DATA);
    // High Cursor port to VGA Index Registe
    outb(0x0e, REG_SCREEN_CTRL);
    outb((unsigned char)((pos >> 8) & 0xff), REG_SCREEN_DATA);
}

int get_cursor() {
    int offset;
    outb(0x0f, REG_SCREEN_CTRL);
    offset = inb(REG_SCREEN_DATA) << 8;
    outb(0x0e, REG_SCREEN_CTRL);
    offset += inb(REG_SCREEN_DATA);
    return offset;
}

void video_putchar(char ch) {
    if (ch == '\n') {
        video_x = 0;
        video_y++;
    }
    else if(ch == '\t') {
        while(video_x % TAB_LEN) video_x++;
    }
    else if (ch == '\b') {
        video_x--;
        if (video_x < 0) {
            video_x = VIDEO_X_SZ;
            video_y--;
            if (video_y < 0) {
                video_y = 0;
            }
        }
        video_putchar_at(' ', video_x, video_y, WHITE_ON_BLACK);
    }
    else {
        video_putchar_at(ch, video_x, video_y, WHITE_ON_BLACK);
        video_x++;
    }
    if (video_x >= VIDEO_X_SZ) {
        video_x = 0;
        video_y++;
    }
    if (video_y >= VIDEO_Y_SZ) {
        roll_screen();
        video_x = 0;
        video_y = VIDEO_Y_SZ - 1;
    }

    update_cursor(video_y, video_x);
    return;
}

void con_write(struct tty_struct *tty) {
    char ch;
    while (!EMPTY(tty->write_q)) {
        GETCH(tty->write_q, ch);
        video_putchar(ch);
    }
}

void video_clear() {
    int x,y;
    video_x = 0;
    video_y = 0;
    for (x = 0; x < VIDEO_X_SZ; x++) {
        for (y = 0; y < VIDEO_Y_SZ; y++) {
            video_putchar_at(' ', x, y, WHITE_ON_BLACK);
        }
    }
    return;
}

void video_putchar_at(char ch, int x, int y, char attr) {
    if (x >= 80) x = 80;
    if (y >= 25) y = 25;

    *(video_buffer + 2*(x+80*y)) = ch;
    *(video_buffer + 2*(x+80*y) + 1) = attr;
    return;
}

void roll_screen() {
    int i;
    for (i = 1; i < VIDEO_Y_SZ; i++) {
        memcpy(video_buffer + (i - 1) * 80 * 2, video_buffer + i * 80 * 2, VIDEO_X_SZ, 2*sizeof(char));
    }

    for (i = 0; i < VIDEO_X_SZ; i++) {
        video_putchar_at(' ', i, VIDEO_Y_SZ - 1, WHITE_ON_BLACK);
    }
    return;
}

void memcpy(char *dest, char *src, int count, int size) {
    int i;
    int j;
    for(i = 0; i < count; i++) {
        for(j = 0; j < size; j++) {
            *(dest + i*size + j) = *(src + i*size + j);
        }
    }
    return ;
}

// 控制台初始化程序。在init/main.c中被调用
// 该函数首先根据 setup.s 程序取得的系统硬件参数初始化设置几个本函数专用的静态
// 全局变量。然后根据显示卡模式(单色还是彩色显示)和显卡类型(EGA/VGA还是CGA)
// 分别设置显示内存起始位置以及显示索引寄存器和显示数值寄存器端口号。最后设置
// 键盘中断陷阱描述符并复位对键盘中断的屏蔽位，以允许键盘开始工作。
void con_init() {
    register unsigned char a;
    set_trap_gate(0x21, &keyboard_interrupt);
    outb_p(inb_p(0x21)&0xfd, 0x21);        // 取消对键盘中断的屏蔽，允许IRQ1。
    a = inb_p(0x61);                        // 读取键盘端口 0x61 (8255A端口PB)
	outb_p(a|0x80, 0x61);                   // 设置禁止键盘工作（位7置位）
	outb(a, 0x61);                          // 再允许键盘工作，用以复位键盘
}
