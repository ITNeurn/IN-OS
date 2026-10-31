; cdshim.asm - El Torito no-emulation INT13 转换 shim
;
; 被 MBR 复制到 0x0B00:0000 并挂钩 INT 13h。作用: 引导链(VBR/loader)看到的
; 仍是 512 字节扇区世界, 但 SeaBIOS 光驱的 INT13 AH=42h 实际按 "2048 字节
; 物理块" 寻址 (LBA=物理块号, count=块数, 每块 2048B)。本 shim 拦截针对光驱
; 的 AH=42h, 把请求读到 bounce 缓冲 0x70000, 再按调用者要求的字节数拷到目标。
;
; 数据区固定在偏移 0x200, 与 mbr.asm 中的 CDL_OFF/OLD_OFF 常量保持同步。
[BITS 16]
[ORG 0]

CDL     equ 0x0200          ; 光驱驱动器号 (DL)
OLD13   equ 0x0202          ; 原 INT13 向量 (far ptr)
DAP     equ 0x208           ; 本地磁盘地址包 (16 字节)
REQCNT  equ 0x218           ; 请求的 512 扇区数
DSTOFF  equ 0x21A           ; 目标 offset
DSTSEG  equ 0x21C           ; 目标 segment
BLOCK   equ 0x21E           ; 起始 2048 物理块号
NBYTES  equ 0x222           ; 需拷贝的字节数
BOUNCE  equ 0x7000          ; bounce 缓冲段 (物理 0x70000)

handler:
    cmp ah, 0x42
    jne .pass
    ; 光驱引导时系统中只有这张可启动盘: 无论调用者用什么 DL(VBR/loader 可能
    ; 把盘号回退成 0x80), 所有扩展读一律经 CDL 指定的真实光驱完成。

    ; DS:SI 此刻指向调用者的 DAP, 先在改动 DS 前取出参数
    mov ax, [si+2]
    mov [cs:REQCNT], ax
    mov ax, [si+4]
    mov [cs:DSTOFF], ax
    mov ax, [si+6]
    mov [cs:DSTSEG], ax
    mov eax, [si+8]
    shr eax, 2                  ; 物理块 = 512-LBA / 4 (镜像布局保证 4 对齐)
    mov [cs:BLOCK], eax

    push es
    push ds
    push di
    push si
    push dx
    push cx
    push bx
    push ax

    ; 构造本地 DAP: 读 ceil(count/4) 个 2048 块到 BOUNCE:0000
    mov word [cs:DAP], 0x0010
    mov ax, [cs:REQCNT]
    mov cx, ax
    shl cx, 9                   ; 字节数 = count * 512
    mov [cs:NBYTES], cx
    add ax, 3
    shr ax, 1
    shr ax, 1                   ; 块数 = ceil(count / 4)
    mov [cs:DAP+2], ax
    mov word [cs:DAP+4], 0
    mov word [cs:DAP+6], BOUNCE
    mov eax, [cs:BLOCK]
    mov [cs:DAP+8], eax
    mov dword [cs:DAP+12], 0

    ; 以模拟中断方式调用真实 INT13: pushf; call far old13
    push ds
    mov ax, 0x0B00
    mov ds, ax
    mov si, DAP
    mov ah, 0x42
    mov dl, [cs:CDL]
    pushf
    call far [cs:OLD13]
    pop ds
    jc .fail

    ; BOUNCE:0000 -> 调用者目标, 只拷贝 NBYTES 字节(丢弃多读的对齐部分)
    mov ax, [cs:DSTSEG]
    mov es, ax
    mov di, [cs:DSTOFF]
    mov ax, BOUNCE
    mov ds, ax
    xor si, si
    mov cx, [cs:NBYTES]
    cld
    rep movsb

    ; 成功: 返回 AH=0, AL=已传 512 扇区数, CF=0
    mov ax, [cs:REQCNT]
    mov ah, 0
    mov bx, sp
    mov [ss:bx], ax
    and byte [ss:bx+20], 0xFE   ; 8 个 push(16B) + INT帧 FLAGS(IP/CS/FL = +16/+18/+20)
    jmp .done
.fail:
    mov al, 0                   ; AH 保留真实 INT13 的错误码
    mov bx, sp
    mov [ss:bx], ax
    or byte [ss:bx+20], 1
.done:
    pop ax
    pop bx
    pop cx
    pop dx
    pop si
    pop di
    pop ds
    pop es
    iret

.pass:
    jmp far [cs:OLD13]         ; 其它功能/其它驱动器: 原样转发

; ---- 固定布局数据区 (偏移必须与上面的 equ 一致) ----
times 0x200-($-$$) db 0
db 0                 ; 0x200 CDL
db 0                 ; 0x201 pad
dd 0                 ; 0x202 OLD13
db 0                 ; 0x207 pad
times 16 db 0        ; 0x208 DAP
dw 0                 ; 0x218 REQCNT
dw 0                 ; 0x21A DSTOFF
dw 0                 ; 0x21C DSTSEG
dd 0                 ; 0x21E BLOCK
dw 0                 ; 0x222 NBYTES
