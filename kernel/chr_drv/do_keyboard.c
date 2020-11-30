#include <linux/kernel.h>
#include <linux/head.h>
#include <asm/system.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <serial_debug.h>

#define RELEASE_CHAR(a) ((a) & 0x80)
#define CAPS 0x3A
#define LCTRL 0x1D
#define LALT 0x38
#define LSHIFT 0x2A
#define RCTRL
#define RALT
#define RSHIFT


// 点代表非可见字符, 或者是"."本身
const char scancode_table[] =       "..1234567890-=\b.qwertyuiop[]\n.asdfghjkl;\'`.\\zxcvbnm,./.*. .............7894561230......";
const char shift_scancode_table[] = "..!@#$%^&*()_+\b.QWERTYUIOP{}\n.ASDFGHJKL:\"~.|ZXCVBNM<>?.*. .............7894561230......";
// const char caps_scancode_table[] =  "..1234567890-=\b.QWERTYUIOP[]\n.ASDFGHJKL;\'`.\\ZXCVBNM,./.*. .............7894561230......";

char toupper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    return ch;
}

void do_keyboard_interrupt(short scancode) {
    static char caps = 0;
    static char lshift = 0;
    static char rshift = 0;
    static char lctrl = 0;
    static char rctrl = 0;
    static char lalt = 0;
    static char ralt = 0;
    static char cap_out = 0;
    char ch = ' ';

    if (RELEASE_CHAR(scancode)) {
        // s_printk("Keyboard Release 0x%x\n", scancode);

        // Check if Shift / Alt / Ctrl / Released
        if (scancode == (LCTRL | 0x80)) {
            lctrl = 0;
        }
        if (scancode == (LALT | 0x80)) {
            lalt = 0;
        }
        if (scancode == (LSHIFT | 0x80)) {
            lshift = 0;
            s_printk("LSHIFT[%d]\n", lshift);
        }
        return ;
    }
    if (!RELEASE_CHAR(scancode)) {
        if (scancode == LCTRL) {
            lctrl = 1;
            return ;
        }
        if (scancode == LALT) {
            lalt = 1;
            return ;
        }
        if (scancode == LSHIFT) {
            lshift = 1;
            return ;
        }
        if (scancode == CAPS) {
            caps = !caps;
            return ;
        }
        cap_out = caps?!(lshift || rshift):(lshift || rshift);
        if (lshift || rshift) {
            ch = shift_scancode_table[scancode];
            if (cap_out) {
                ch = toupper(ch);
            }
        }
        else if (lctrl || rctrl) {

        } else if (lalt || ralt) {

        }
        else {
            ch = scancode_table[scancode];
            if(cap_out) {
                ch = toupper(ch);
            }
        }
        // s_printk("Keyboard Press 0x%x[ %d ]\n", scancode, ch)
        // TODO: 使得 tty 和进程对应, 当前都是指向 tty_table[0]
        PUTCH(ch, tty_table[0].read_q);

        copy_to_buffer(&tty_table[0]);
    }
    return ;
}