.code32

.global _start
.global begtext, begdata, begbss

_start:
	# Just have a try to move one char in the memory
	#mov 'O', %al
	#movb %al, 0xb8000
	#movb $0b00000111, %al
	#movb %al, 0xb8001
	#mov $0x0c63, %ax		# Don't mess up the order
	#mov %ax, 0xb8000

put_red_string:
	xor %ecx, %ecx			# Clear the counter
loop:
	mov $red_str, %ebx
	add %cx, %bx
	movb (%ebx), %al
	movb $0x0c, %ah
	mov $0xb8A00, %ebx
	shl %ecx
	add %ecx, %ebx
	shr %ecx
	movw %ax, (%ebx)
	inc %ecx
	cmp $62, %ecx
	jne loop

halt:
	jmp halt

red_str:
	.ascii "You've entered protected mode!                           "
	# len = 0x1e

