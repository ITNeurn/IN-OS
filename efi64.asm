; ============================================================================
; efi64.asm - 真正的 UEFI x86_64 引导程序 -> 加载现有 32 位裸机内核
;
; 链路: UEFI 固件(long mode) -> 本程序(PE32+) -> 读 FAT 卷根目录 KERNEL.BIN
;       -> GOP 设图形模式 -> 写 0x7000 VBE1 参数块 -> ExitBootServices
;       -> 关长模式回 32 位保护模式 -> jmp 0x10200 (现有内核, 一行不改)
;
; 编译: nasm -f bin efi64.asm -o BOOTX64.EFI
; 放置: FAT 卷 \EFI\BOOT\BOOTX64.EFI  (MBR 分区类型 0xEF = ESP)
;
; MS x64 / UEFI 调用约定: RCX RDX R8 R9 + 栈, 32 字节 shadow space,
; 调用前 RSP 16 字节对齐。efi_main 入口 RSP%16==8; 8 个 push 后仍为 8;
; 每次 call 前 sub rsp,0x28(40) 使对齐到 0, 完事 add rsp,0x28。
; ============================================================================

%define EFI_LOADER_DATA       4
%define ALLOCATE_ADDRESS      2
%define ALLOCATE_ANY_PAGES    0

; ---- EFI_BOOT_SERVICES 函数偏移 (x64) ----
%define BS_ALLOC_PAGES        40
%define BS_ALLOC_POOL         64
%define BS_HANDLE_PROTOCOL    152
%define BS_EXIT_BOOT_SERVICES 232
%define BS_STALL              248
%define BS_WATCHDOG           256
%define BS_CONNECT_CONTROLLER 264
%define BS_OPEN_PROTOCOL      280
%define BS_PROTOCOLS_PER_HANDLE 304
%define BS_LOCATE_HANDLE_BUFFER 312
%define BS_LOCATE_PROTOCOL    320

; ---- EFI_SYSTEM_TABLE 偏移 (x64, 结构体无 pack -> 按 ABI 8 字节自然对齐) ----
; Hdr24 + FirmwareVendor@24 + FirmwareRevision@32(u32) + 4字节pad
; + ConsoleInHandle@40 + ConIn@48 + ConsoleOutHandle@56 + ConOut@64
; + StdErrHandle@72 + StdErr@80 + RuntimeServices@88 + BootServices@96
; + NumberOfTableEntries@104 + ConfigurationTable@112
%define ST_CONOUT             64
%define ST_BOOTSERVICES       96
%define ST_NUM_TABLES         104
%define ST_CONFIG_TABLE       112

; ---- EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL 偏移 ----
%define TXT_OUTPUT_STRING     8
%define TXT_CLEAR_SCREEN      48

; ---- EFI_FILE_PROTOCOL 偏移 ----
%define FILE_OPEN             8
%define FILE_CLOSE            16
%define FILE_READ             32
%define FILE_GETINFO          64

; ---- EFI_GRAPHICS_OUTPUT_PROTOCOL 偏移 ----
%define GOP_QUERYMODE         0
%define GOP_SETMODE           8
%define GOP_MODE_PTR          24

; ============================================================================
; PE32+ 映像
; ============================================================================
section hdr start=0x000 vstart=0x000

    dw  'M' + ('Z' << 8)          ; MZ
    times 58 db 0
    dd  pe_sig - $$               ; e_lfanew = 0x40

pe_sig:
    db  'PE', 0, 0
    ; ---- COFF 头 ----
    dw  0x8664                    ; Machine = AMD64
    dw  1                         ; NumberOfSections
    dd  0                         ; TimeDateStamp
    dd  0                         ; PointerToSymbolTable
    dd  0                         ; NumberOfSymbols
    dw  opt_end - opt_hdr         ; SizeOfOptionalHeader = 240
    dw  0x0022                    ; Characteristics

opt_hdr:
    dw  0x20B                     ; Magic = PE32+
    db  14, 0                     ; LinkerVersion
    dd  0x4000                    ; SizeOfCode
    dd  0                         ; SizeOfInitializedData
    dd  0                         ; SizeOfUninitializedData
    dd  0x1000                    ; AddressOfEntryPoint (RVA)
    dd  0x1000                    ; BaseOfCode
    dq  0x200000                  ; ImageBase (避开内核目标地址 0x10000,
                                  ;  否则拷贝内核会覆盖正在执行的降模式代码)
    dd  0x1000                    ; SectionAlignment
    dd  0x200                     ; FileAlignment
    dw  0, 0                      ; OS 版本
    dw  0, 0                      ; 镜像版本
    dw  0, 0                      ; Subsystem 版本
    dd  0                         ; Win32VersionValue
    dd  0x5000                    ; SizeOfImage (hdr 1 页 + .text 4 页)
    dd  0x200                     ; SizeOfHeaders
    dd  0                         ; CheckSum
    dw  10                        ; Subsystem = EFI_APPLICATION
    dw  0x0060                    ; DllCharacteristics
    dq  0x100000                  ; StackReserve
    dq  0x1000                    ; StackCommit
    dq  0x100000                  ; HeapReserve
    dq  0x1000                    ; HeapCommit
    dd  0                         ; LoaderFlags
    dd  16                        ; NumberOfRvaAndSizes
    times 16 dq 0                 ; DataDirectory: 16 条目 x 8 字节 = 128
opt_end:

    ; ---- .text 节表 ----
    db  '.text', 0, 0, 0
    dd  0x4000                    ; VirtualSize
    dd  0x1000                    ; VirtualAddress
    dd  0x4000                    ; SizeOfRawData
    dd  0x200                     ; PointerToRawData
    dd  0, 0                      ; PointerRelocations / LineNumbers
    dw  0, 0                      ; NumberOfRelocations / LineNumbers
    dd  0x60000020                ; CODE | EXECUTE | READ

    times 0x200-($-$$) db 0

; ============================================================================
; .text  (文件偏移 0x200, 装载虚拟地址 0x201000)
; ============================================================================
section txt start=0x200 vstart=0x201000

[BITS 64]
DEFAULT REL

; UCS-2 字符串宏 (NASM 无 du): ucs2 "ABC" -> dw 65,66,67
%macro ucs2 1
%assign %%i 1
%rep %strlen(%1)
%substr %%c %1 %%i
dw %%c
%assign %%i %%i+1
%endrep
%endmacro

; 调用 EFI BootServices 函数: g_bs 是保存 BootServices 指针的变量,
; 必须先取出指针再间接调用 (不能直接 call [g_bs+off], 那会读到全局数据区)
%macro bscall 1
    mov  rax, [g_bs]
    call qword [rax + %1]
%endmacro

efi_main:
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub  rsp, 0x28
    mov  [g_image_handle], rcx
    mov  [g_system_table], rdx
    mov  rax, [rdx + ST_BOOTSERVICES]
    mov  [g_bs], rax

    ; 关看门狗
    xor  ecx, ecx
    xor  edx, edx
    xor  r8d, r8d
    xor  r9d, r9d
    bscall BS_WATCHDOG

    lea  rcx, [s_banner]
    call puts

    ; ---- 枚举所有发布 SimpleFileSystem 的卷, 找含 KERNEL.BIN 的 ----
    mov  ecx, 2                        ; ByProtocol
    lea  rdx, [guid_sfs]
    xor  r8d, r8d
    lea  r9, [g_sfs_count]
    lea  rax, [g_sfs_buf]
    mov  [rsp + 32], rax
    bscall BS_LOCATE_HANDLE_BUFFER
    test eax, eax
    jnz  fail_fs
    xor  r15, r15                      ; 卷序号
.vol_loop:
    mov  r14, [g_sfs_count]
    cmp  r15, r14
    jae  fail_open
    mov  rsi, [g_sfs_buf]
    mov  rcx, [rsi + r15*8]            ; 第 n 个句柄
    lea  rdx, [guid_sfs]
    lea  r8, [g_sfs]
    bscall BS_HANDLE_PROTOCOL
    test eax, eax
    jnz  .vol_next
    mov  rcx, [g_sfs]
    lea  rdx, [g_root]
    call qword [rcx + 8]              ; OpenVolume
    test eax, eax
    jnz  .vol_next

    ; ---- Open \KERNEL.BIN ----
    mov  rcx, [g_root]
    lea  rdx, [g_kfile]
    lea  r8, [s_kernel_name]
    mov  r9d, 1                       ; EFI_FILE_MODE_READ
    mov  qword [rsp + 32], 0          ; Attributes
    call qword [rcx + FILE_OPEN]
    test eax, eax
    jz   .have_file
.vol_next:
    inc  r15
    jmp  .vol_loop
.have_file:

    ; ---- GetInfo 取文件大小 (EFI_FILE_INFO 为 pack(1): Size@0, FileSize@8) ----
    mov  qword [g_info_size], 512
    mov  ecx, EFI_LOADER_DATA
    mov  edx, 512
    lea  r8, [g_info_buf]
    bscall BS_ALLOC_POOL
    test eax, eax
    jnz  fail_mem
    mov  rcx, [g_kfile]
    lea  rdx, [guid_file_info]
    lea  r8, [g_info_size]
    mov  r9, [g_info_buf]
    call qword [rcx + FILE_GETINFO]
    test eax, eax
    jnz  fail_info
    mov  rax, [g_info_buf]
    mov  r12, [rax + 8]               ; r12 = 内核文件字节数 (FileSize@+8)
    test r12, r12
    jz   fail_info

    ; ---- 申请物理内存, 目标 0x300000 (内核将位于 0x300200) ----
    lea  rax, [r12 + 0x200 + 0x0FFF]
    shr  rax, 12
    mov  [g_pages], rax
    mov  qword [g_phys], 0x300000
    mov  rcx, ALLOCATE_ADDRESS
    mov  rdx, EFI_LOADER_DATA
    mov  r8, rax
    lea  r9, [g_phys]
    bscall BS_ALLOC_PAGES
    test eax, eax
    jz   .have_phys
    ; 固定地址失败 -> 任意地址, ExitBootServices 后用 64 位拷贝
    mov  rcx, ALLOCATE_ANY_PAGES
    mov  rdx, EFI_LOADER_DATA
    mov  r8, [g_pages]
    lea  r9, [g_phys]
    bscall BS_ALLOC_PAGES
    test eax, eax
    jnz  fail_mem
.have_phys:
    mov  r13, [g_phys]                ; r13 = 实际缓冲区物理地址

    ; ---- Read ----
    mov  [g_read_size], r12
    mov  rcx, [g_kfile]
    lea  rdx, [g_read_size]
    mov  r8, r13
    call qword [rcx + FILE_READ]
    test eax, eax
    jnz  fail_read
    mov  rcx, [g_kfile]
    call qword [rcx + FILE_CLOSE]

    ; ---- 加载 \FRAMES.BIN (开机动画) 到物理 0x2000000; 任何失败静默跳过 ----
    mov  rcx, [g_root]
    lea  rdx, [g_ffile]
    lea  r8, [s_frames_name]
    mov  r9d, 1
    mov  qword [rsp + 32], 0
    call qword [rcx + FILE_OPEN]
    test eax, eax
    jnz  .no_frames
    mov  qword [g_info_size], 512
    mov  ecx, EFI_LOADER_DATA
    mov  edx, 512
    lea  r8, [g_info_buf]
    bscall BS_ALLOC_POOL
    test eax, eax
    jnz  .fclose
    mov  rcx, [g_ffile]
    lea  rdx, [guid_file_info]
    lea  r8, [g_info_size]
    mov  r9, [g_info_buf]
    call qword [rcx + FILE_GETINFO]
    test eax, eax
    jnz  .fclose
    mov  rax, [g_info_buf]
    mov  rbx, [rax + 8]               ; rbx = frames 文件字节数
    test rbx, rbx
    jz   .fclose
    cmp  rbx, 0x4000000               ; 防御: >64MB 不认
    ja   .fclose
    lea  rax, [rbx + 0xFFF]
    shr  rax, 12
    mov  [g_pages], rax
    mov  qword [g_phys], 0x2000000
    mov  rcx, ALLOCATE_ADDRESS
    mov  rdx, EFI_LOADER_DATA
    mov  r8, rax
    lea  r9, [g_phys]
    bscall BS_ALLOC_PAGES
    test eax, eax
    jz   .fhave_phys
    mov  rcx, ALLOCATE_ANY_PAGES
    mov  rdx, EFI_LOADER_DATA
    mov  r8, [g_pages]
    lea  r9, [g_phys]
    bscall BS_ALLOC_PAGES
    test eax, eax
    jnz  .fclose
.fhave_phys:
    mov  [g_read_size], rbx
    mov  rcx, [g_ffile]
    lea  rdx, [g_read_size]
    mov  r8, [g_phys]
    call qword [rcx + FILE_READ]
    test eax, eax
    jnz  .fclose
    ; 传参给内核: 0x514=魔数'FRAM' 0x518=物理地址 0x51C=字节长度
    mov  edi, 0x514
    mov  dword [rdi], 0x4D415246
    mov  rax, [g_phys]
    mov  [rdi + 4], eax
    mov  eax, [g_read_size]
    mov  [rdi + 8], eax
.fclose:
    mov  rcx, [g_ffile]
    call qword [rcx + FILE_CLOSE]
.no_frames:

    ; ---- GOP 图形模式 ----
    lea  rcx, [guid_gop]
    xor  edx, edx
    lea  r8, [g_gop]
    bscall BS_LOCATE_PROTOCOL
    test eax, eax
    jnz  fail_gop
    mov  rbx, [g_gop]                 ; GOP*

    ; 枚举全部 GOP 模式, 选总像素最高者; 一个都查不到则保持当前模式
    ; (真机 2160x1440 原生面板由此选中, 不再被 1024x768 压低)
    mov  rsi, [rbx + GOP_MODE_PTR]    ; MODE*
    mov  r14d, [rsi + 4]              ; 兜底: 当前模式号
    xor  r15d, r15d                   ; 目前最优像素数
    xor  ebp, ebp                     ; index (此处 rbp 空闲, 稍后重新用作 red_pos)
.mode_loop:
    cmp  ebp, [rsi + 0]
    jae  .mode_pick
    mov  qword [g_qm_size], 256
    mov  rcx, rbx
    mov  edx, ebp
    lea  r8, [g_qm_size]
    lea  r9, [g_qm_info]
    call qword [rcx + GOP_QUERYMODE]
    test eax, eax
    jnz  .mode_next
    mov  rdi, [g_qm_info]
    mov  eax, [rdi + 4]               ; HorizontalResolution
    mul  dword [rdi + 8]              ; x VerticalResolution -> edx:eax
    test edx, edx
    jnz  .mode_next                   ; 像素 >4G 不可能, 防御
    cmp  eax, r15d
    jbe  .mode_next
    mov  r15d, eax
    mov  r14d, ebp
.mode_next:
    inc  ebp
    jmp  .mode_loop
.mode_pick:
    mov  rdx, r14
    mov  rcx, rbx
    call qword [rcx + GOP_SETMODE]
    test eax, eax
    jz   .mode_ok
    ; SetMode 失败 (怪异固件/虚拟设备) -> 回退当前模式, 保证有 scanout
    mov  rdx, [rsi + 4]
    mov  rcx, rbx
    call qword [rcx + GOP_SETMODE]
.mode_ok:
    ; 不论成败继续 (当前模式仍然有效)

    mov  rsi, [rbx + GOP_MODE_PTR]
    mov  rdi, [rsi + 8]               ; 最新 INFO*
    mov  eax, [rdi + 4]
    mov  [g_w], eax
    mov  eax, [rdi + 8]
    mov  [g_h], eax
    mov  eax, [rdi + 32]              ; PixelsPerScanLine
    shl  eax, 2
    mov  [g_pitch], eax               ; pitch 字节
    mov  rax, [rsi + 24]              ; FrameBufferBase
    mov  [g_fb], rax
    mov  rdx, rax
    shr  rdx, 32
    jnz  fail_fbhigh                  ; >= 4GB: 内核 32 位无法访问
    mov  eax, [rdi + 12]              ; PixelFormat
    mov  [g_pixfmt], eax

    ; RedFieldPosition: fmt0=RGBX(0) fmt1=BGRX(16) fmt2=BitMask(取RedMask位序)
    xor  ebp, ebp
    cmp  dword [g_pixfmt], 0
    je   .rpos_done                   ; RGBX: red @0
    mov  ebp, 16
    cmp  dword [g_pixfmt], 1
    je   .rpos_done                   ; BGRX: red @16
    cmp  dword [g_pixfmt], 2
    jne  .rpos_done                   ; 未知: 按 BGRX 兜底
    mov  ebp, [rdi + 16]              ; RedChannelMask
    bsf  ebp, ebp                     ; 最低置位 = 位移
.rpos_done:

    ; ---- 写 0x7000 VBE1 参数块 (与内核 vbe_block_t 完全一致) ----
    ; 注意: 文件顶部 DEFAULT REL, 直接写 [0x7000] 会被编成 RIP 相对!
    ;       必须用寄存器间接, 才能命中真正的物理地址 0x7000。
    mov  edi, 0x7000                   ; 物理 0x7000 (edi 零扩展至 rdi)
    mov  dword [rdi], 0x31454256       ; [0] 'VBE1'
    mov  eax, [g_fb]
    mov  [rdi + 4], eax                ; [4] LFB 低 32 位 (<4GB 已校验)
    mov  ax, [g_pitch]
    mov  [rdi + 8], ax                 ; [8] pitch
    mov  ax, [g_w]
    mov  [rdi + 0xA], ax               ; [A] width
    mov  ax, [g_h]
    mov  [rdi + 0xC], ax               ; [C] height
    mov  byte [rdi + 0xE], 32          ; [E] bpp
    mov  byte [rdi + 0xF], 0           ; [F] rsvd
    mov  word [rdi + 0x10], 0xFFFF     ; [10] mode = UEFI/GOP 标记
    mov  [rdi + 0x12], bpl             ; [12] red_pos (ebp 低 8 位)
    mov  byte [rdi + 0x13], 0          ; [13] pad2
    mov  word [rdi + 0x14], 0          ; [14] pad3

    ; ---- 找 ACPI 2.0 RSDP: SystemTable->ConfigurationTable, 写物理 0x510 ----
    ; GUID 8868e871-e4f1-11d3-bc22-0080c73e8889 的 LE dword 序列
    mov  rax, [g_system_table]
    mov  rcx, [rax + ST_NUM_TABLES]
    mov  rsi, [rax + ST_CONFIG_TABLE]
.rsdp_scan:
    test rcx, rcx
    jz   .rsdp_done
    cmp  dword [rsi],      0x8868E871
    jne  .rsdp_next
    cmp  dword [rsi + 4],  0x11D3E4F1
    jne  .rsdp_next
    cmp  dword [rsi + 8],  0x8000BC22
    jne  .rsdp_next
    cmp  dword [rsi + 12], 0x8889C73E
    je   .rsdp_hit
.rsdp_next:
    add  rsi, 24                      ; EFI_CONFIGURATION_TABLE = GUID16 + VendorTable8
    dec  rcx
    jmp  .rsdp_scan
.rsdp_hit:
    mov  eax, [rsi + 16]              ; VendorTable = RSDP (取低 32 位够用, <4GB)
    mov  edi, 0x510                   ; 寄存器间接, 避免 RIP 相对
    mov  [rdi], eax
.rsdp_done:

    ; ---- GetMemoryMap + ExitBootServices ----
    mov  qword [g_mm_size], 0
    lea  rcx, [g_mm_size]             ; arg1 *MemoryMapSize
    xor  edx, edx                     ; arg2 MemoryMap = NULL
    xor  r8d, r8d                     ; arg3 *MapKey = NULL
    xor  r9d, r9d                     ; arg4 *DescriptorSize = NULL
    mov  qword [rsp + 32], 0          ; arg5 *DescriptorVersion = NULL
    mov  rax, [g_bs]
    call qword [rax + 56]             ; GetMemoryMap (期望 BUFFER_TOO_SMALL)
    mov  rax, [g_mm_size]
    add  rax, 0x4000
    mov  [g_mm_size], rax
    mov  rcx, EFI_LOADER_DATA
    mov  rdx, rax
    lea  r8, [g_mm_buf]
    bscall BS_ALLOC_POOL
    test eax, eax
    jnz  fail_mem
    mov  r14, 10
.ebs_retry:
    mov  rax, [g_mm_size]
    mov  [g_mm_alloc], rax            ; 恢复缓冲容量
    lea  rcx, [g_mm_alloc]            ; arg1 *MemoryMapSize
    mov  rdx, [g_mm_buf]              ; arg2 MemoryMap
    lea  r8, [g_mm_key]               ; arg3 *MapKey
    lea  r9, [g_mm_dsz]               ; arg4 *DescriptorSize
    lea  rax, [g_mm_dver]
    mov  [rsp + 32], rax              ; arg5 *DescriptorVersion
    mov  rax, [g_bs]
    call qword [rax + 56]
    test eax, eax
    jnz  .fail_mm
    mov  rcx, [g_image_handle]
    mov  rdx, [g_mm_key]
    bscall BS_EXIT_BOOT_SERVICES
    test eax, eax
    jz   .boot_kernel
    dec  r14
    jnz  .ebs_retry
.fail_mm:
    lea  rcx, [s_mm_fail]
    jmp  die

    ; ================= ExitBootServices 之后: 无任何 UEFI 服务 =================
.boot_kernel:
    ; 若内核读在高位缓冲, 趁还在 64 位长模式整体拷到物理 0x300000
    ; (UEFI 固件恒等映射 RAM, 低地址可直接访问)
    mov  rsi, r13
    cmp  rsi, 0x300000
    je   .no_copy
    mov  rdi, 0x300000
    mov  rcx, r12
    rep  movsb
.no_copy:
    cli
    lea  rax, [g_gdt]
    mov  [g_gdt64_ptr + 2], rax       ; 运行时实际地址 (支持任意加载基址)
    lea  rax, [g_gdt64_ptr]
    lgdt [rax]
    ; 远跳入 32 位兼容段 (L=0 D=1) -> compatibility mode
    push 0x08
    lea  rax, [.compat32]
    push rax
    retfq

[BITS 32]
.compat32:
    ; 先切到自建 32 位数据段 (UEFI 段描述符仍可暂用, 但尽早替换)
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  eax, cr0
    and  eax, 0x7FFFFFFF               ; CR0.PG = 0 (恒等映射, 地址不变)
    mov  cr0, eax
    jmp  .flush
.flush:
    mov  eax, cr4
    and  eax, ~(1 << 5)               ; CR4.PAE = 0
    mov  cr4, eax
    mov  ecx, 0xC0000080              ; EFER
    rdmsr
    and  eax, ~(1 << 8)               ; EFER.LME = 0
    wrmsr
    mov  esp, 0x3f0000
    db   0xEA
    dd   0x300200
    dw   0x08

[BITS 64]
; ---------------------------------------------------------------------------
; puts(rcx = UCS-2 串)
; ---------------------------------------------------------------------------
puts:
    push rsi
    push rbx
    sub  rsp, 0x28
    mov  rsi, rcx
    mov  rbx, [g_system_table]
    mov  rcx, [rbx + ST_CONOUT]
    mov  rdx, rsi
    call qword [rcx + TXT_OUTPUT_STRING]
    add  rsp, 0x28
    pop  rbx
    pop  rsi
    ret

; ---------------------------------------------------------------------------
; puthex(rcx = u64) 调试用: 打印 16 位十六进制 + CRLF
; ---------------------------------------------------------------------------
puthex:
    push rbx
    push rbp
    push rsi
    sub  rsp, 0x28
    mov  rsi, rcx                    ; 待打印值
    lea  rbx, [hex_digits]
    lea  rbp, [hex_buf + 30]         ; 末尾 (16 CHAR16)
    mov  ecx, 16
.ph_loop:
    mov  rax, rsi
    and  rax, 0xF
    movzx rax, byte [rbx + rax]
    mov  [rbp], ax                   ; CHAR16
    shr  rsi, 4
    sub  rbp, 2
    dec  ecx
    jnz  .ph_loop
    lea  rcx, [hex_buf]
    call puts
    add  rsp, 0x28
    pop  rsi
    pop  rbp
    pop  rbx
    ret

die:
    call puts
    mov  rcx, 5000000
    bscall BS_STALL
    cli
    hlt
    jmp  $

fail_li:   lea rcx, [s_fail_li]
    jmp die
fail_fs:   lea rcx, [s_fail_fs]
    jmp die
fail_open: lea rcx, [s_fail_open]
    jmp die
fail_mem:  lea rcx, [s_fail_mem]
    jmp die
fail_info: lea rcx, [s_fail_info]
    jmp die
fail_read: lea rcx, [s_fail_read]
    jmp die
fail_gop:  lea rcx, [s_fail_gop]
    jmp die
fail_fbhigh:
    lea  rcx, [s_fail_fb]
    jmp die

; ============================================================================
; 数据
; ============================================================================
align 8
g_image_handle:   dq 0
g_system_table:   dq 0
g_bs:             dq 0
g_loaded_image:   dq 0
g_dev:            dq 0
g_sfs:            dq 0
g_sfs_iface:      dq 0
g_sfs_count:      dq 0
g_sfs_buf:        dq 0
g_all_count:      dq 0
g_all_buf:        dq 0
g_bio_count:      dq 0
g_bio_buf:        dq 0
g_pp_count:       dq 0
g_pp_buf:         dq 0
g_fake_count:     dq 0
g_fake_buf:       dq 0
g_sfs_direct:     dq 0
g_root:           dq 0
g_kfile:          dq 0
g_ffile:          dq 0
g_info_size:      dq 0
g_info_buf:       dq 0
g_pages:          dq 0
g_phys:           dq 0
g_read_size:      dq 0
g_gop:            dq 0
g_qm_size:        dq 0
g_qm_info:        dq 0
g_w:              dd 0
g_h:              dd 0
g_pitch:          dd 0
g_pixfmt:         dd 0
g_fb:             dq 0
g_mm_size:        dq 0
g_mm_buf:         dq 0
g_mm_alloc:       dq 0
g_mm_key:         dq 0
g_mm_dsz:          dq 0
g_mm_dver:        dq 0

hex_digits:       db '0123456789ABCDEF'
align 2
hex_buf:          times 16 dw 0
                  dw 13, 10, 0

align 16
g_gdt:
    dq 0
    dq 0x00CF9A000000FFFF            ; 0x08: 32 位代码 flat
    dq 0x00CF92000000FFFF            ; 0x10: 32 位数据 flat
g_gdt64_ptr:
    dw g_gdt64_ptr - g_gdt - 1
    dq g_gdt

align 4
guid_loaded_image:
    dd 0x5B1B31A1
    dw 0x9562, 0x11D2
    db 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
guid_sfs:
    dd 0x964E5B22
    dw 0x6459, 0x11D2
    db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
guid_blockio:
    dd 0x964E5B21
    dw 0x6459, 0x11D2
    db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
guid_dpath:
    dd 0x09576E91
    dw 0x6D3F, 0x11D2
    db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
guid_fake:
    times 16 db 0xFF
guid_file_info:
    dd 0x09576E92
    dw 0x6D3F, 0x11D2
    db 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
guid_gop:
    dd 0x9042A9DE
    dw 0x23DC, 0x4A38
    db 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A

; ---- UCS-2 字符串 ----
s_banner:    dw 13, 10
             ucs2 "IN OS UEFI Bootloader"
             dw 13, 10, 0
s_kernel_name:
             ucs2 "\KERNEL.BIN"
             dw 0
s_frames_name:
             ucs2 "\FRAMES.BIN"
             dw 0
s_fail_li:   ucs2 "FAIL: LoadedImage protocol"
             dw 13, 10, 0
s_fail_fs:   ucs2 "FAIL: filesystem on ESP"
             dw 13, 10, 0
s_fail_open: ucs2 "FAIL: KERNEL.BIN not found"
             dw 13, 10, 0
s_fail_mem:  ucs2 "FAIL: memory allocation"
             dw 13, 10, 0
s_fail_info: ucs2 "FAIL: file info"
             dw 13, 10, 0
s_fail_read: ucs2 "FAIL: kernel read"
             dw 13, 10, 0
s_fail_gop:  ucs2 "FAIL: GOP not available"
             dw 13, 10, 0
s_fail_fb:   ucs2 "FAIL: framebuffer above 4GB"
             dw 13, 10, 0
s_mm_fail:   ucs2 "FAIL: ExitBootServices"
             dw 13, 10, 0

    times 0x4000-($-$$) db 0
