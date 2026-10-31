; boot_x86.S X86实模式引导
[org 0x7c00]
[bits 16]
mov ax, 0x0000
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7c00

; 加载微内核二进制到内存0x1000
mov ah, 0x02
mov al, 32
mov ch, 0
mov cl, 2
mov dh, 0
mov bx, 0x1000
int 0x13

jmp 0x1000

times 510 - ($ - $$) db 0
dw 0xaa55
