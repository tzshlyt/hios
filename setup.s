.code16

.text

.equ DEMOSEG, 0x1020
.equ LEN, 50 

show_text:
    mov $DEMOSEG, %ax
    mov %ax, %es
    mov $0x03, %ah
    xor %bh, %bh
    int $0x10

    mov $0x000a, %bx
    mov $0x1301, %ax
    mov $LEN, %cx
    mov $msg, %bp
    int $0x10

loop:
    jmp loop

msg:
    .byte 13, 10
    .ascii "You've successfully load the floppy into RAM"
    .byte 13, 10, 13, 10
    
