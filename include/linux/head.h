#ifndef _HEAD_H
#define _HEAD_H

typedef struct desc_struct {
    unsigned long a, b;     // 符号由8个字节构成
} desc_table[256];

extern unsigned long pg_dir[1024];
extern desc_table idt, gdt;


#endif
