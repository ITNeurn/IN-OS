; boot_fat32.asm - FAT32 引导扇区 (精简版 v2)
[BITS 16]
[ORG 0x7C00]

jmp short start
nop
db 'MSDOS5.0'          ; offset 3:  OEM (必须正好 8 字节)
dw 512                  ; offset 11: bytes per sector
db 1                    ; offset 13: sectors per cluster (必须 db; 1=每簇1扇区,保证簇数>=65525 通过 UEFI FAT32 校验)
dw 32                   ; offset 14: reserved sectors
db 2                    ; offset 16: num FATs
dw 0                    ; offset 17: root entries
dw 0                    ; offset 19: total sectors 16
db 0xF8                 ; offset 21: media
dw 0                    ; offset 22: sectors per FAT 16
dw 63                   ; offset 24: sectors per track
dw 255                  ; offset 26: heads
dd 0                    ; offset 28: hidden sectors
dd 0                    ; offset 32: total sectors 32
dd 0                    ; offset 36: sectors per FAT 32
dw 0                    ; offset 40: ext flags
dw 0                    ; offset 42: FS version
dd 2                    ; offset 44: root cluster
dw 1                    ; offset 48: FSInfo sector
dw 6                    ; offset 50: backup boot sector
times 12 db 0           ; offset 52: reserved
db 0x80                 ; offset 64: drive number
db 0                    ; offset 65: reserved
db 0x29                 ; offset 66: boot sig
dd 0x12345678           ; offset 67: volume ID
db 'BOOT       '        ; offset 71: label (11)
db 'FAT32   '           ; offset 82: FS type (8)

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [drive], dl
    mov bp, 0x7C00

    ; 'B'
    push 0xB800
    pop es
    mov word [es:0], 0x0742
    xor ax, ax
    mov es, ax

    ; 参数到 0x7000
    mov di, 0x7000
    mov ax, [bp+11]
    mov [di], ax          ; bps
    mov al, [bp+13]
    mov [di+2], al        ; spc
    mov eax, [bp+14]
    and eax, 0xFFFF
    mov [di+4], eax       ; fat_start
    ; data_start = fat_start + spf * 2
    mov eax, [bp+36]
    shl eax, 1
    add eax, [di+4]
    mov [di+8], eax

    ; 加载根目录到 0x9000
    mov eax, [bp+44]
    mov [cur], eax
    xor dx, dx
    mov es, dx
    mov bx, 0x9000
    call load
    ; load 会改动 ES (pushad 不保存段寄存器), 搜索前重置 ES=0 (DS 始终为 0)
    push 0
    pop es

    ; 搜索 LOADER.BIN
    mov si, 0x9000
search_loop:
    mov al, [si]
    test al, al
    jz not_found
    cmp al, 0xE5
    je search_next
    test byte [si+11], 0x18
    jnz search_next
    mov cx, 11
    mov di, ln
    push si
    repe cmpsb
    pop si
    jz found_loader
search_next:
    add si, 32
    cmp si, 0xA000
    jl search_loop
not_found:
    mov al, 'E'
    jmp die

found_loader:
    mov ax, [si+20]
    shl eax, 16
    mov ax, [si+26]
    mov [cur], eax
    mov ax, 0x0800
    mov es, ax
    xor bx, bx
    call load
    push 0xB800
    pop es
    mov word [es:2], 0x074C
    jmp 0x0800:0x0000

; load: 加载 cur 簇链到 ES:BX
load:
    pushad
.l:
    mov eax, [cur]
    cmp eax, 0x0FFFFFF8
    jae .d
    sub eax, 2
    xor ecx, ecx
    mov cl, [0x7002]
    mul ecx
    add eax, [0x7008]
    push es
    push bx
    push cx
    call rs
    pop cx
    pop bx
    pop es
    ; ES += spc*32
    mov ax, es
    mov dx, cx
    shl dx, 5
    add ax, dx
    mov es, ax
    mov eax, [cur]
    call gf
    mov [cur], eax
    jmp .l
.d:
    popad
    ret

; gf: get_fat_entry
gf:
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
    call rs
    pop bx
    pop es
    mov eax, [0x7100 + ebx]
    and eax, 0x0FFFFFFF
    pop ecx
    pop ebx
    ret

; rs: read_sectors eax=LBA es:bx=dst cl=count
rs:
    pushad
    mov esi, 0x7064
    mov byte [esi], 0x10
    mov byte [esi+1], 0
    mov [esi+2], cl
    mov byte [esi+3], 0
    mov [esi+4], bx
    add eax, [bp+28]          ; 加分区隐藏扇区偏移 (El Torito=0, MBR链加载=分区起点)
    mov [esi+8], eax          ; 存最终 LBA
    mov dword [esi+12], 0
    mov ax, es                ; 之后才能动 AX
    mov [esi+6], ax
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jc re
    popad
    ret
re:
    mov al, 'X'
    jmp die

; die: al=字符, 红色显示后停机
die:
    push 0xB800
    pop es
    mov ah, 0x04
    mov [es:2], ax
    hlt
    jmp $

ln:     db 'LOADER  BIN'
drive:  db 0x80
cur:    dd 0

times 510-($-$$) db 0
dw 0xAA55
