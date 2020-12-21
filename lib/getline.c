#define __LIBRARY__
#define EOF -1
#include <unistd.h>
#include <linux/lib.h>

extern int user_tty_read(int channel, char *buf, int nr);
_syscall3(int ,user_tty_read, int, channel, char *, buf, int, nr)

static inline _syscall1(int, sys_debug, char *, str)

char getchar() {
    char _buf[2048];
    user_tty_read(0, _buf, 1);
    return _buf[0];
}

int getline(char *str) {
    char _buf[2048];
    char ch;
    char *p = str;
    int len = 0;
    while(ch != '\n' && ch != EOF) {
        user_tty_read(0, &_buf[0], 1);
        ch = _buf[0];
        *p++ = ch;
        len++;
    }
    *(p - 1) = '\0';
    return len - 1; // remove the \n
}
