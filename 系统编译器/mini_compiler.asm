bits 64
org 0x400000
_start:
    cmp rdi,3
    jne .usage
    mov rsi,[rsi+8]
    mov rax,2
    xor rdx,rdx
    syscall
    mov r8,rax
    mov rsi,[rsi+16]
    mov rax,2
    mov rdi,0x241
    mov rdx,0o644
    syscall
    mov r9,rax
    mov rsi,asm_data
    mov rax,1
    mov rdi,r9
    mov rdx,asm_len
    syscall
    mov rax,3
    mov rdi,r8
    syscall
    mov rax,3
    mov rdi,r9
    syscall
    xor rdi,rdi
    mov rax,60
    syscall
.usage:
    mov rsi,msg
    mov rax,1
    mov rdi,1
    xor rdx,rdx
.len:
    cmp byte [rsi+rdx],0
    je .out
    inc rdx
    jmp .len
.out:
    syscall
    mov rax,60
    mov rdi,1
    syscall
section .rodata
msg db "Usage: ./编译器 input.c out\n",0
asm_data db "section .text
global _start
_start:mov rax,60;xor rdi,0;syscall"
asm_len equ $-asm_data
