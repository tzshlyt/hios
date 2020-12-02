#include <linux/head.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <serial_debug.h>

int tty_isempty_q(const struct tty_queue *q) {
    if (q->tail == q->head) {
        return 1;
    }
    return 0;
}

int tty_isfull_q(const struct tty_queue *q) {
    if ((q->head + 1) % TTY_BUF_SIZE == q->tail) {
        return 1;
    }
    return 0;
}

char tty_pop_q(struct tty_queue *q) {
    char ch;
    ch = q->buf[q->tail];
    q->tail = (q->tail + 1) % TTY_BUF_SIZE;
    return ch;
}

int tty_push_q(struct tty_queue *q, char ch) {
    if (tty_isfull_q(q)) {
        return -1;
    }
    q->buf[q->head] = ch;
    q->head = (q->head + 1) % TTY_BUF_SIZE;
    return 0;
}

char tty_queue_head(const struct tty_queue *q) {
    return q->buf[q->head];
}

char tty_queue_tail(const struct tty_queue *q) {
    return q->buf[q->tail];
}

void tty_queue_stat(const struct tty_queue *q) {
    unsigned int i;
    s_printk("tty queue tail = %d, head = %d\n", q->tail, q->head);
    s_printk("tty queue: [ ");
    for (i = q->tail; i != q->head; i = (i + 1) % TTY_BUF_SIZE) {
        s_printk("%c, ", q->buf[i]);
    }
    s_printk(" ]\n");
}