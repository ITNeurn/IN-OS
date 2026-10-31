; mbr.asm - isohybrid 风格 MBR (自重定位到 0x0600)
;
; DD 模式写入 USB 后, BIOS 从物理 LBA0 加载本 MBR 到 0x7C00。
; 因为稍后要把分区 VBR 读到同一个 0x7C00 (会覆盖本代码),
; 所以先把自身 512 字节复制到 0x0600 并在那里继续执行。
; 然后从分区表读取 FAT 分区起始 LBA, 读 VBR 到 0x7C00,
; 把分区起始 LBA 填入 VBR 的 BPB 隐藏扇区字段 (0x7C1C), 跳转。
; 分区表第一项 (扇区偏移446) 由构建脚本回填。
[BITS 16]
[ORG 0x0600]

; ---- 与 cdshim.asm 必须保持同步的常量 ----
SHIM_DST   equ 0xB000          ; shim 驻留段地址 (物理 0xB000)
SHIM_SRC   equ 0x7E00          ; BIOS no-emulation 载入的 shim 源(MBR之后)
SHIM_WORDS equ 768             ; 复制 1536 字节
SHIM_SEG   equ 0x0B00
CDL_OFF    equ 0x0200          ; shim 内: 光驱号
OLD_OFF    equ 0x0202          ; shim 内: 原 INT13 向量 far ptr

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [0x7084], dl            ; 保存启动盘号

    ; 重定位 0:0x7C00 -> 0:0x0600 (256 字 = 512 字节, 不重叠)
    mov si, 0x7C00
    mov di, 0x0600
    mov cx, 256
    cld
    rep movsw
    ; 远跳转到 0x0600 副本中的 main
    db 0xEA
    dw main
    dw 0x0000

main:
    ; 分区表已随副本搬到 0x0600+446 = ptable, 分区起始 LBA 在表项+8
    mov eax, [ptable + 8]
    test eax, eax
    jz .bad
    mov [0x7080], eax           ; 保存分区起始

    ; 若从 El Torito no-emulation 光驱启动 (DL>=0xE0), 安装 INT13 转换 shim:
    ; 它把 512 扇区/LBA 的扩展读透明转换成光驱 2048 物理块读取。
    mov dl, [0x7084]
    cmp dl, 0xE0
    jb .read_vbr
    mov si, SHIM_SRC
    mov di, SHIM_DST
    mov cx, SHIM_WORDS
    cld
    rep movsw
    mov byte [SHIM_DST + CDL_OFF], dl
    cli
    mov ax, [0x4C]
    mov [SHIM_DST + OLD_OFF], ax
    mov ax, [0x4E]
    mov [SHIM_DST + OLD_OFF + 2], ax
    mov word [0x4C], 0
    mov word [0x4E], SHIM_SEG
    sti

.read_vbr:
    ; 构造 DAP @ 0x7000, 读 1 个扇区到 0000:7C00
    mov eax, [0x7080]           ; 重新加载分区起始(shim 分支会动 ax)
    mov si, 0x7000
    mov word [si], 0x0010
    mov word [si+2], 1
    mov word [si+4], 0x7C00
    mov word [si+6], 0
    mov [si+8], eax
    mov dword [si+12], 0
    mov ah, 0x42
    mov dl, [0x7084]
    int 0x13
    jc .bad

    ; 本代码在 0x0600, 写 0x7C00 区不会覆盖自己:
    ; 将分区起始 LBA 写入 VBR BPB 隐藏扇区字段 (0x7C00+28)
    mov eax, [0x7080]
    mov [0x7C1C], eax
    mov dl, [0x7084]
    ; 远跳转 0000:7C00 执行 VBR
    db 0xEA
    dw 0x7C00
    dw 0x0000

.bad:
    push 0xB800
    pop es
    mov word [es:0], 0x044D     ; 红色 'M' = MBR 失败
    hlt
    jmp $

; ---- 扇区剩余填充到 446, 然后是分区表 ----
times 446-($-$$) db 0
ptable:
    times 64 db 0               ; 4 个分区表项, 第一项由构建脚本回填
dw 0xAA55
