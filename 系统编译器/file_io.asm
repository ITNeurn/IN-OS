bits 64
org 0x400000
_start:
    cmp rdi,4
    jne .err
    mov rsi,[rsi+16]
    mov rax,2
    xor rdx,rdx
    syscall
    mov r8,rax
    mov rsi,[rsi+24]
    mov rax,2
    mov rdi,0x241
    mov rdx,0o644
    syscall
    mov r9,rax
.read:
    mov rax,0
    mov rdi,r8
    mov rsi,buf
    mov rdx,4096
    syscall
    cmp rax,0
    jle .end
    mov rdx,rax
    mov rax,1
    mov rdi,r9
    mov rsi,buf
    syscall
    jmp .read
.end:
    mov rax,3
    mov rdi,r8
    syscall
    mov rax,3
    mov rdi,r9
    syscall
    mov rsi,okmsg
    mov rax,1
    mov rdi,1
    xor rdx,rdx
.l:
    cmp byte [rsi+rdx],0
    je .o
    inc rdx
    jmp .l
.o:
    syscall
    xor rdi,rdi
    mov rax,60
    syscall
.err:
    mov rsi,errmsg
    mov rax,1
    mov rdi,1
    xor rdx,rdx
.le:
    cmp byte [rsi+rdx],0
    je .oe
    inc rdx
    jmp .le
.oe:
    syscall
    mov rax,60
    mov rdi,1
    syscall
section .bss
buf resb 4096
section .rodata
okmsg db "Copy success\n",0
errmsg db "Usage: ./读写器 copy src dst\n",0
