; loader.asm - 第二阶段加载器 (修正版)
; 加载 KERNEL.BIN 到 0x10000, FRAMES.BIN 到扩展内存
; 播放开机动画, 切换保护模式, 跳转内核
[BITS 16]
[ORG 0x8000]

start:
    ; 健壮处理驱动器号: 引导扇区跳转时 DL 可能被清 0, 硬盘应为 0x80+
    test dl, 0x80
    jnz .have_drive
    mov dl, 0x80
.have_drive:
    mov [drive], dl

    ; 显示 'M'
    push 0xB800
    pop es
    mov word [es:4], 0x074D
    xor ax, ax
    mov es, ax

    ; FAT32 参数到 0x7000
    mov bp, 0x7C00
    mov di, 0x7000
    mov ax, [bp+11]
    mov [di], ax
    mov al, [bp+13]
    mov [di+2], al
    mov eax, [bp+14]
    and eax, 0xFFFF
    mov [di+4], eax
    mov eax, [bp+36]
    shl eax, 1
    add eax, [di+4]
    mov [di+8], eax

    ; ===== 找并加载 KERNEL.BIN 到 0x10000 =====
    mov si, kernel_name
    call find_file
    jc err_kernel
    mov [cur], eax
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    call load_file

    ; ===== 找并加载 FRAMES.BIN 到扩展内存 0x100000 =====
    mov si, frames_name
    call find_file
    jc err_frames
    mov [cur], eax

    ; 进入 big-real (unreal): 段 base0/4GB, 随后回普通实模式段;
    ; 每次 4GB 串拷贝前再调用 unreal_on
    call unreal_on
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; 逐簇加载 frames.bin 到 0x100000
    mov dword [frame_offset], 0
load_frames_loop:
    mov eax, [cur]
    cmp eax, 0x0FFFFFF8
    jae frames_done
    ; 先设好目标 ES:BX (用 DX, 绝不触碰将作为 LBA 的 EAX)
    push es
    mov dx, 0x7000
    mov es, dx
    xor bx, bx
    sub eax, 2
    xor ecx, ecx
    mov cl, [0x7002]
    mul ecx
    add eax, [0x7008]      ; EAX = 簇首 LBA
    call read_sectors
    pop es

    ; 从 0x70000 复制到扩展内存 [0x100000 + frame_offset]
    call unreal_on              ; DS=ES = base0 / 4GB
    mov esi, 0x70000
    mov edi, 0x100000
    add edi, [frame_offset]
    xor ecx, ecx
    mov cl, [0x7002]
    shl ecx, 7          ; 扇区数 * 128 = dword 数
    a32 rep movsd
    xor ax, ax
    mov ds, ax
    mov es, ax          ; 回普通实模式段(base0), 供下一轮 INT13

    ; 更新偏移
    xor eax, eax
    mov al, [0x7002]
    shl eax, 9
    add [frame_offset], eax
    ; 下一簇
    mov eax, [cur]
    call get_fat_entry
    mov [cur], eax
    jmp load_frames_loop
frames_done:

    ; ===== 播放动画 =====
    call play_animation

    ; ===== VBE 图形模式 (实模式, 必须在进保护模式前完成) =====
    ; 成功: 物理 0x7000 处存 magic 'VBE1' + LFB 参数供内核读取
    ; 失败: 回 VGA 文本模式 03, magic=0, 内核走文本 CLI 兜底
    call vbe_init

    ; ===== 切换保护模式跳转内核 =====
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

[BITS 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000
    jmp 0x10200

[BITS 16]

; ===== play_animation =====
play_animation:
    ; VGA mode 13h
    mov ax, 0x13
    int 0x10

    ; 复制调色板 (frames.bin 前 768 字节) 到 0x7000
    ; 源在扩展内存, 需 base0/4GB 的 unreal 段
    call unreal_on
    mov esi, 0x100000
    mov edi, 0x7000
    mov ecx, 192
    a32 rep movsd

    ; 设置 VGA 调色板 (BIOS 要标准实模式 ES:DX)
    xor ax, ax
    mov es, ax
    mov ax, 0x1012
    xor bx, bx
    mov cx, 256
    mov dx, 0x7000
    int 0x10

    ; 读帧数量 + 计算帧数据区 (DS 回到 unreal base0/4GB)
    call unreal_on
    mov esi, 0x100000 + 768
    mov eax, [esi]
    mov [num_frames], eax
    mov eax, [num_frames]
    shl eax, 2
    add eax, 772
    add eax, 0x100000
    mov [frames_base], eax
    mov dword [frame_idx], 0

.anim_loop:
    mov eax, [frame_idx]
    cmp eax, [num_frames]
    jae .anim_done

    ; 用 unreal DS 从扩展内存读帧偏移 + 帧数据到 0x7E00
    call unreal_on              ; DS = base0 / 4GB
    mov esi, 0x100000 + 772
    mov eax, [frame_idx]
    shl eax, 2
    add esi, eax
    mov eax, [esi]
    mov [cur_frame_off], eax

    ; 帧数据地址 = frames_base + cur_frame_off
    mov esi, [frames_base]
    add esi, [cur_frame_off]

    ; 复制帧到 0x20000 (源跨 1MB 用 DS 4GB; 目标 ES=0x2000 段, 避开 loader 0x8000)
    mov ax, 0x2000
    mov es, ax                  ; ES:0 = 物理 0x20000
    xor edi, edi
    mov ecx, 16000
    a32 rep movsd

    ; RLE 解码 0x20000 -> 0x70000: DS=0x2000 读, ES=0x7000 写 (均在 64K 段内)
    mov ax, 0x2000
    mov ds, ax
    mov ax, 0x7000
    mov es, ax
    xor si, si
    xor di, di
    call rle_decode

    ; 复制解码帧 0x70000 -> VGA 0xA0000 (纯实模式段)
    mov ax, 0x7000
    mov ds, ax                  ; 源 0x7000:0
    mov ax, 0xA000
    mov es, ax                  ; 目标 0xA000:0
    xor si, si
    xor di, di
    mov cx, 32000
    rep movsw
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; 延迟 ~83ms (12fps)
    mov cx, 1
    mov dx, 0x4585              ; 83333 us
    mov ah, 0x86
    int 0x15

    inc dword [frame_idx]
    jmp .anim_loop

.anim_done:
    ; 不再回文本模式 03: 保留图形态, 由 vbe_init 切换到 VBE 高分辨率真彩
    xor ax, ax
    mov ds, ax
    mov es, ax
    ret

; ===== vbe_init: 设置 32bpp LFB 模式, 参数块写物理 0x7000 =====
; 模式信息块(256B)放物理 0x6000; 参数块:
;   [0x7000]='VBE1' [04]=LFB物理地址 [08]=pitch [10]=宽 [12]=高 [14]=bpp [16]=模式号
; 降级链: 1024x768(0x118) -> 800x600(0x115) -> 640x480(0x112)
vbe_init:
    pushad
    xor ax, ax
    mov es, ax
    mov si, vbe_modes
.try:
    mov cx, [si]
    cmp cx, 0
    je .fail

    ; 查询模式信息 (ES:DI=0x6000, 预置 'VBE2' 要求 BIOS 填 VBE2 扩展字段)
    mov di, 0x6000
    mov dword [es:di], 0x32454256      ; 'VBE2'
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .next
    ; VESA 标准模式号 0x112/0x115/0x118 报 24bpp (SeaBIOS 硬件实际 4 字节/像素,
    ; pitch=宽*4); 32bpp 直色同样接受。由 pitch 判定实际像素步长。
    cmp byte [es:di+25], 24
    je .bpp_ok
    cmp byte [es:di+25], 32
    jne .next
.bpp_ok:
    cmp byte [es:di+27], 6             ; MemoryModel == 6 (direct color)
    jne .next

    ; 设置模式: BX = 模式号 | bit14(LFB)
    ; 注意: 必须从模式表重读, 4F01 不保证保留 CX
    mov bx, [si]
    or  bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .next

    ; 设置后再查一次, 取可靠 PhysBase (CX 同样从表重读)
    mov cx, [si]
    mov di, 0x6000
    mov dword [es:di], 0x32454256
    mov ax, 0x4F01
    int 0x10
    mov eax, [es:di+40]
    test eax, eax
    jz .next

    ; 写参数块 0x7000
    mov di, 0x7000
    mov dword [es:di], 0x31454256      ; 'VBE1'
    mov [es:di+4], eax                 ; LFB
    mov ax, [es:0x6000+16]
    mov [es:di+8], ax                  ; BytesPerScanLine
    mov ax, [es:0x6000+18]
    mov [es:di+10], ax                 ; XResolution
    mov ax, [es:0x6000+20]
    mov [es:di+12], ax                 ; YResolution
    mov al, [es:0x6000+25]
    mov [es:di+14], al                 ; bpp
    mov ax, [si]
    mov [es:di+16], ax                 ; mode
    mov al, [es:0x6000+32]             ; RedFieldPosition
    mov [es:di+18], al                 ; 0=RGB序(需交换R/B), 16=BGR序(直写)
    popad
    ret
.next:
    add si, 2
    jmp .try
.fail:
    mov ax, 0x0003
    int 0x10
    xor ax, ax
    mov es, ax
    mov dword [es:0x7000], 0
    popad
    ret

; ===== rle_decode: DS:SI=压缩源, ES:DI=目标 (输出 64000 字节) =====
rle_decode:
    pushad
    xor dx, dx              ; DX = 输出字节计数 (64000 < 65536)
.loop:
    cmp dx, 64000
    jae .end
    lodsb                   ; DS:SI -> al, SI++
    mov bl, al
    test bl, 0x80
    jnz .run
    ; 原始: bl+1 字节
    movzx cx, bl
    inc cx
.lit:
    movsb                   ; DS:SI -> ES:DI
    inc dx
    dec cx
    jnz .lit
    jmp .loop
.run:
    ; 重复次数 = neg(bl)+1
    mov al, bl
    neg al
    inc al
    movzx cx, al
    lodsb                   ; al = 重复字节
.rl:
    mov [es:di], al
    inc di
    inc dx
    dec cx
    jnz .rl
    jmp .loop
.end:
    popad
    ret

; ===== find_file: si=11字节文件名, eax=起始簇, ecx=大小, CF=错误 =====
find_file:
    ; 加载根目录到 0x9000
    mov eax, [bp+44]
    mov [cur], eax
    push es
    xor ax, ax
    mov es, ax
    mov bx, 0x9000
    push si
    call load_file
    pop si
    pop es

    ; 搜索 (确保 ES=0, DS=0)
    push es
    push ds
    xor ax, ax
    mov es, ax
    mov ds, ax
    push si                  ; 保存文件名指针
    mov di, si
    mov si, 0x9000
.search:
    mov al, [si]
    test al, al
    jz .nf
    cmp al, 0xE5
    je .next
    test byte [si+11], 0x18
    jnz .next
    push si
    push di
    mov cx, 11
    rep cmpsb
    pop di
    pop si
    jz .found
.next:
    add si, 32
    cmp si, 0xA000
    jl .search
.nf:
    pop si
    pop ds
    pop es
    stc
    ret
.found:
    ; si = 目录项地址
    mov ax, [si+20]
    shl eax, 16
    mov ax, [si+26]
    mov ecx, [si+28]
    pop si
    pop ds
    pop es
    clc
    ret

; ===== load_file: 加载 cur 簇链到 ES:BX =====
load_file:
    pushad
.loop:
    mov eax, [cur]
    cmp eax, 0x0FFFFFF8
    jae .done
    sub eax, 2
    xor ecx, ecx
    mov cl, [0x7002]
    mul ecx
    add eax, [0x7008]
    push es
    push bx
    push cx
    call read_sectors
    pop cx
    pop bx
    pop es
    mov ax, es
    mov dx, cx
    shl dx, 5
    add ax, dx
    mov es, ax
    mov eax, [cur]
    call get_fat_entry
    mov [cur], eax
    jmp .loop
.done:
    popad
    ret

; ===== get_fat_entry =====
get_fat_entry:
    push ebx
    push ecx
    mov ebx, eax
    shl ebx, 2
    mov eax, ebx
    shr eax, 9
    add eax, [0x7004]
    and ebx, 0x1FF
    push es
    push bx
    xor cx, cx
    mov es, cx
    mov bx, 0x7100
    mov cx, 1
    call read_sectors
    pop bx
    pop es
    mov eax, [0x7100 + ebx]
    and eax, 0x0FFFFFFF
    pop ecx
    pop ebx
    ret

; ===== read_sectors: eax=LBA es:bx=dst cl=count =====
read_sectors:
    pushad
    mov esi, 0x7064
    mov byte [esi], 0x10
    mov byte [esi+1], 0
    mov [esi+2], cl
    mov byte [esi+3], 0
    mov [esi+4], bx
    add eax, [bp+28]          ; 加分区隐藏扇区偏移
    mov [esi+8], eax          ; 先存 LBA
    mov dword [esi+12], 0
    mov ax, es                ; 再动 AX
    mov [esi+6], ax
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jc .err
    popad
    ret
.err:
    push 0xB800
    pop es
    mov word [es:4], 0x0458
    hlt
    jmp $

; ===== unreal_on: 置 DS=ES 为 base0/4GB 的 big-real 段 =====
; 返回时 DS/ES 隐藏缓存为 4GB(可见选择子 0x10); 之后切勿再用实模式值
; 重载 DS/ES, 否则缓存退回 64KB。可重复调用。
unreal_on:
    cli
    xor ax, ax
    mov ds, ax              ; gdt 以 ORG 0x8000 绝对地址编址, 必须 base0
                            ; (BIOS 调用后不保证 DS 仍为 0)
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp .flush
.flush:
    mov bx, 0x10
    mov ds, bx
    mov es, bx
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp .flush2
.flush2:
    sti
    ret

; ===== GDT =====
gdt:
    dq 0
    dw 0xFFFF, 0, 0x9A00, 0x00CF
    dw 0xFFFF, 0, 0x9200, 0x00CF
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

; ===== 数据 =====
kernel_name:    db 'KERNEL  BIN'
frames_name:    db 'FRAMES  BIN'
drive:          db 0x80
cur:            dd 0
frame_offset:   dd 0
num_frames:     dd 0
frame_idx:      dd 0
cur_frame_off:  dd 0
frames_base:    dd 0

; VBE 降级模式表 (0 结尾): 1024x768x32, 800x600x32, 640x480x32
vbe_modes:      dw 0x0118, 0x0115, 0x0112, 0x0000

err_kernel:
    push 0xB800
    pop es
    mov word [es:4], 0x044B
    hlt
    jmp $
err_frames:
    push 0xB800
    pop es
    mov word [es:4], 0x0446
    hlt
    jmp $

times 8192-($-$$) db 0
