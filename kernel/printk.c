#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <stdarg.h>


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

char *video_buffer = VIDEO_MEM;

void set_cursor_offset(int offset);

void video_init() {
    struct video_info *info = 0x9000;   // setup.s 中保存了屏幕设备信息

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
    unsigned int pos = (row * VIDEO_X_SZ) + col;
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

void printk(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    char c, *s;

    while(*fmt) {
        c = *fmt++;
        if(c != '%') {
            video_putchar(c);
            continue;
        }
        c = *fmt++;
        if(c == '\0')
            break;
        switch(c) {
            case 'd':
                printnum(va_arg(ap, int), 10, 1);
                break;
            case 'u':
                printnum(va_arg(ap, int), 10, 0);
                break;
            case 'x':
                printnum(va_arg(ap, int), 16, 0);
                break;
            case 's':
                s = va_arg(ap, char*);
                while(*s)
                    video_putchar(*s++);
                break;
            case '%':
                video_putchar('%');
        }
    }
    return;
}

void printnum(int num, int base, int sign) {
    char digits[] = "0123456789ABCDEF";
    char buf[50] = "";
    int cnt = 0;
    int i;
    if (sign && num < 0) {
        video_putchar('-');
        num = -num;
    }
    if (num == 0) {
        video_putchar('0');
        return;
    }
    while(num) {
        buf[cnt++] = digits[num % base];
        num = num / base;
    }

    for (i = cnt - 1; i >= 0; i--) {
        video_putchar(buf[i]);
    }
    return;
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

void video_putchar(char ch) {
    if (ch == '\n') {
        video_x = 0;
        video_y++;
    }
    else if(ch == '\t') {
        while(video_x % TAB_LEN) video_x++;
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
