# build_kernel_iso.py — 组装保护模式 C 内核 ISO
# 引导流程：BIOS → 引导扇区(0x7C00, 内嵌 PM Loader) → INT 13h CHS 读 C Kernel(0x10000)
#          → 跳转内嵌 PM Loader → 切换 32 位保护模式 → 跳转 C Kernel 入口 0x10200
#
# 关键布局：
#   - TCC 输出 binary 开头有 0x200 字节 0 padding，kernel_main 在 binary[0x200] 处
#   - 保留 padding 写入 boot.img 扇区1开头
#   - 读 boot.img 扇区1+ 到 0x10000，则 kernel_main 落在 0x10000 + 0x200 = 0x10200 ✓
#   - PM Loader 跳转 0x10200 (kernel_main) ✓
#
# 1.44MB 软盘 CHS 几何：80 cyl × 2 head × 18 sector = 2880 sectors
# 从 LBA=1 读 32 扇区 (16KB, 足够覆盖 15261 字节 kernel)：
#   第一次：磁头 0 柱面 0 扇区 2-18 (17 扇区) → 0x10000
#   第二次：磁头 1 柱面 0 扇区 1-15 (15 扇区) → 0x12200

import os, struct, subprocess, shutil, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
QEMU_ISO = r'C:\qemu_iso'
SECTOR = 2048
KERNEL_BASE = 0x10200          # C Kernel 入口 = 0x10000 (载入地址) + 0x200 (TCC binary padding)
ISO_LBA_BOOT_IMG = 20          # boot.img 在 ISO 中的起始 LBA
KERNEL_SECTORS_512 = 32        # CHS 读取 32 个 512 字节扇区 = 16384 字节 (17+15)

# ===== 1. 编译 C 内核 =====
def compile_kernel():
    """用 WSL 中的 TCC 编译 C 内核为 32 位 flat binary"""
    src = os.path.join(ROOT, 'kernel_full.c')
    dst = os.path.join(QEMU_ISO, 'kernel_full.bin')

    # 复制源码到英文路径
    shutil.copy2(src, os.path.join(QEMU_ISO, 'kernel_full.c'))

    # 编译基址 = KERNEL_BASE = 0x10200
    # 因为引导扇区加载 boot.img 头部到 0x10000，C Kernel 在偏移 0x200 处
    cmd = (
        'wsl -u root -e bash -c '
        '"cd /mnt/c/qemu_iso && '
        'tcc -m32 -nostdlib -static -ffreestanding '
        f'-Wl,-Ttext,{KERNEL_BASE:#x} -Wl,--oformat,binary '
        '-o kernel_full.bin kernel_full.c"'
    )
    print(f'[1/5] 编译 C 内核 (基址 {KERNEL_BASE:#x})...')
    print(f'  {cmd}')
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        print(f'  编译失败: {result.stderr}')
        sys.exit(1)
    if not os.path.exists(dst):
        print('  输出文件不存在!')
        sys.exit(1)
    size = os.path.getsize(dst)
    print(f'  kernel_test.bin: {size} 字节')
    return dst

# ===== 2. 生成内嵌 PM Loader 的引导扇区 =====
def build_boot_sector():
    """
    生成 512 字节引导扇区，包含：
    1. 设置段寄存器
    2. INT 13h AH=0x42 读 ISO LBA 20, count=12 (12 个 512 扇区 = 6144 字节) 加载到 0x1000:0x0000 (物理 0x10000)
       → 0x10000 = 引导扇区副本 (无用)
       → 0x10200 = C Kernel 入口 (kernel_main)
    3. 跳转到内嵌 PM Loader 入口 (在引导扇区内偏移 PM_OFFSET，物理地址 0x7C00+PM_OFFSET)
    PM Loader 切换 32 位 PM 后 jmp 0x10200 (kernel_main)
    """
    sys.path.insert(0, ROOT)
    from boot_pm import build_pm_loader as _build

    code = bytearray()

    # --- 调试标记：写 'B' 到 VGA 显存 (0xB8000) 第一个字符 ---
    # mov ax, 0xB800
    code += b'\xB8\x00\xB8'          # mov ax, 0xB800
    code += b'\x8E\xC0'              # mov es, ax
    # xor di, di
    code += b'\x31\xFF'              # xor di, di
    # mov byte [es:di], 'B' = 0x42
    code += b'\x26\xC6\x05\x42'      # mov byte [es:di], 'B'
    # mov byte [es:di+1], 0x07 (属性: 灰底黑字)
    code += b'\x26\xC6\x45\x01\x07'  # mov byte [es:di+1], 0x07
    # add di, 2
    code += b'\x83\xC7\x02'          # add di, 2

    # --- 设置段寄存器 ---
    code += b'\x31\xC0'          # xor ax, ax
    code += b'\x8E\xD8'          # mov ds, ax
    code += b'\x8E\xD0'          # mov ss, ax
    code += b'\xBC\x00\x7C'      # mov sp, 0x7C00
    # 不修改 DL，保留 BIOS 给的软盘驱动器号 (floppy emulation 模式下 DL=0x00)

    # --- 重置软盘控制器 (INT 13h AH=0x00, DL=驱动器号) ---
    # 必须先重置软盘才能读取 (BIOS 规范要求)
    code += b'\xB4\x00'          # mov ah, 0x00 (reset disk)
    code += b'\xCD\x13'          # int 0x13
    # 不检查 CF, 继续读取

    # --- INT 13h AH=0x02 CHS 读 (第一次：磁头 0 扇区 2-18 共 17 扇区) ---
    # ES:BX = 0x1000:0x0000 = 物理地址 0x10000
    code += b'\xB8\x00\x10'          # mov ax, 0x1000
    code += b'\x8E\xC0'              # mov es, ax
    code += b'\x31\xDB'              # xor bx, bx
    code += b'\xB4\x02'              # mov ah, 0x02 (CHS 读)
    code += b'\xB0\x11'              # mov al, 17 (扇区数)
    code += b'\xB5\x00'              # mov ch, 0 (柱面低 8 位)
    code += b'\xB1\x02'              # mov cl, 2 (起始扇区 = 2)
    code += b'\xB6\x00'              # mov dh, 0 (磁头 0)
    code += b'\xCD\x13'              # int 0x13
    jc1_pos = len(code)
    code += b'\x72\x00'              # jc fail (位移待回填)

    # --- INT 13h AH=0x02 CHS 读 (第二次：磁头 1 扇区 1-15 共 15 扇区) ---
    # ES:BX = 0x1000:0x2200 = 物理地址 0x12200 (= 0x10000 + 17*512)
    code += b'\xBB\x00\x22'          # mov bx, 0x2200
    code += b'\xB4\x02'              # mov ah, 0x02
    code += b'\xB0\x0F'              # mov al, 15 (扇区数)
    code += b'\xB5\x00'              # mov ch, 0
    code += b'\xB1\x01'              # mov cl, 1 (起始扇区 = 1)
    code += b'\xB6\x01'              # mov dh, 1 (磁头 1)
    code += b'\xCD\x13'              # int 0x13
    jc2_pos = len(code)
    code += b'\x72\x00'              # jc fail (位移待回填)

    # --- 调试标记：写 'L' (读取成功) ---
    code += b'\xB8\x00\xB8'          # mov ax, 0xB800
    code += b'\x8E\xC0'              # mov es, ax
    code += b'\xBF\x02\x00'          # mov di, 2
    code += b'\x26\xC6\x05\x4C'      # mov byte [es:di], 'L'
    code += b'\x26\xC6\x45\x01\x07'  # mov byte [es:di+1], 0x07
    jmp_skip_pos = len(code)
    code += b'\xEB\x00'              # jmp short skip_fail (位移待回填)

    # --- fail: 写 'X' + 错误码 + hlt 死循环 ---
    fail_label = len(code)
    # AH 包含错误码，保存到 BL (避免被 mov es 覆盖)
    code += b'\x88\xE3'              # mov bl, ah (保存错误码到 BL)
    code += b'\xB8\x00\xB8'          # mov ax, 0xB800
    code += b'\x8E\xC0'              # mov es, ax
    code += b'\xBF\x02\x00'          # mov di, 2
    code += b'\x26\xC6\x05\x58'      # mov byte [es:di], 'X'
    code += b'\x26\xC6\x45\x01\x04'  # mov byte [es:di+1], 0x04 (红色)
    code += b'\x83\xC7\x02'          # add di, 2
    # 显示错误码 AH 的高 4 位 (hex 字符)
    code += b'\x88\xD8'              # mov al, bl
    code += b'\xC0\xE8\x04'          # shr al, 4
    code += b'\x3C\x0A'              # cmp al, 10
    code += b'\x7C\x02'              # jl +2 (跳过 add al,7 到 add al,'0')
    code += b'\x04\x07'              # add al, 7 (A-F)
    code += b'\x04\x30'              # add al, '0'
    code += b'\x26\x88\x05'          # mov [es:di], al
    code += b'\x26\xC6\x45\x01\x0E'  # mov byte [es:di+1], 0x0E (黄色)
    code += b'\x83\xC7\x02'          # add di, 2
    # 显示错误码 AH 的低 4 位
    code += b'\x88\xD8'              # mov al, bl
    code += b'\x24\x0F'              # and al, 0x0F
    code += b'\x3C\x0A'              # cmp al, 10
    code += b'\x7C\x02'              # jl +2
    code += b'\x04\x07'              # add al, 7
    code += b'\x04\x30'              # add al, '0'
    code += b'\x26\x88\x05'          # mov [es:di], al
    code += b'\x26\xC6\x45\x01\x0E'  # mov byte [es:di+1], 0x0E
    code += b'\xF4'                  # hlt
    code += b'\xEB\xFD'              # jmp -3 (死循环)

    # --- skip_fail: 恢复 ES=0 给 PM Loader ---
    skip_label = len(code)
    # 回填跳转位移
    code[jc1_pos + 1] = (fail_label - (jc1_pos + 2)) & 0xFF
    code[jc2_pos + 1] = (fail_label - (jc2_pos + 2)) & 0xFF
    code[jmp_skip_pos + 1] = (skip_label - (jmp_skip_pos + 2)) & 0xFF

    code += b'\x31\xC0'              # xor ax, ax
    code += b'\x8E\xC0'              # mov es, ax

    # --- 记录 PM Loader 偏移位置 ---
    pm_offset = len(code)

    # --- 生成 PM Loader 代码，base_addr = 0x7C00 + pm_offset (PM Loader 实际加载地址) ---
    # 因为 PM Loader 紧跟在引导扇区代码后面，被加载到 0x7C00 + pm_offset 处
    # PM Loader 内部地址 (lgdt, 远跳转目标) 都基于 base_addr
    pm_loader = _build(base_addr=0x7C00 + pm_offset, kernel_load_addr=KERNEL_BASE)
    code += pm_loader

    # --- 跳转到 PM Loader 入口 (0x0000:0x7C00+pm_offset) ---
    # 用相对短跳转 or 直接跳转
    # 因为 PM Loader 紧跟在跳转指令后面，用 fall-through 即可，不需要显式跳转

    # --- 填充到 510 字节 + 签名 ---
    while len(code) < 510:
        code += b'\x00'
    code += b'\x55\xAA'

    print(f'[2/5] 引导扇区 (含 PM Loader): {len(code)} 字节')
    print(f'  PM Loader 偏移: {pm_offset:#x} (在引导扇区内)')
    print(f'  PM Loader 大小: {len(pm_loader)} 字节')
    return bytes(code), pm_loader

# ===== 3. 组装 boot.img =====
def build_boot_img(boot_sector, kernel_bin):
    """组装 1.44MB 软盘镜像"""
    img = bytearray(1474560)  # 1.44MB

    # 扇区 0 (LBA 0): 引导扇区 (512B, 已内嵌 PM Loader)
    img[0:512] = boot_sector

    # 扇区 1+ (LBA 1+): C Kernel (保留 0x200 padding)
    kernel_sectors = (len(kernel_bin) + 511) // 512
    kernel_padded = kernel_bin + b'\x00' * (512 * kernel_sectors - len(kernel_bin))
    img[512:512 + len(kernel_padded)] = kernel_padded

    # 保存 boot.img 到桌面根目录 (供 QEMU 软盘测试)
    boot_img_path = os.path.join(ROOT, 'boot.img')
    with open(boot_img_path, 'wb') as f:
        f.write(bytes(img))

    print(f'[3/5] boot.img: {len(img)} 字节 (1.44MB)')
    print(f'  C Kernel: {len(kernel_bin)} 字节 = {kernel_sectors} 扇区 (boot.img LBA 1-{kernel_sectors})')
    print(f'  保存: {boot_img_path}')
    return bytes(img)

# ===== 4. 组装 ISO =====
def build_iso(boot_img):
    """组装 El Torito 可启动 ISO9660 镜像"""
    from build_iso import (
        build_boot_catalog, build_primary_volume_descriptor,
        build_boot_record_volume_descriptor, build_volume_descriptor_set_terminator,
        build_path_table_record, build_dir_record, pad_field,
        both_endian_16, both_endian_32, iso_datetime_now,
        SECTOR_SIZE
    )

    SECTOR_SYSTEM_AREA_END = 16
    SECTOR_PVD = SECTOR_SYSTEM_AREA_END       # 16
    SECTOR_BRVD = SECTOR_SYSTEM_AREA_END + 1  # 17
    SECTOR_VD_TERM = SECTOR_SYSTEM_AREA_END + 2  # 18
    SECTOR_BOOT_CAT = SECTOR_SYSTEM_AREA_END + 3  # 19

    BOOT_IMG_SECTORS = (len(boot_img) + SECTOR_SIZE - 1) // SECTOR_SIZE  # 720
    SECTOR_BOOT_IMG_START = SECTOR_BOOT_CAT + 1  # 20
    SECTOR_BOOT_IMG_END = SECTOR_BOOT_IMG_START + BOOT_IMG_SECTORS - 1

    SECTOR_L_PATH_TABLE = SECTOR_BOOT_IMG_END + 1
    SECTOR_M_PATH_TABLE = SECTOR_L_PATH_TABLE + 1
    SECTOR_ROOT_DIR = SECTOR_M_PATH_TABLE + 1

    # Path Table
    l_path = build_path_table_record(b'\x01', SECTOR_ROOT_DIR, 1, big_endian=False)
    l_path_padded = l_path + b'\x00' * (SECTOR_SIZE - len(l_path))
    m_path = build_path_table_record(b'\x01', SECTOR_ROOT_DIR, 1, big_endian=True)
    m_path_padded = m_path + b'\x00' * (SECTOR_SIZE - len(m_path))
    pt_size = len(l_path)

    # Root Directory (只含 . 和 .. 和 boot.img 文件)
    root_dir = bytearray()
    root_dir += build_dir_record(extent_lba=SECTOR_ROOT_DIR, data_length=SECTOR_SIZE,
                                 flags=0x02, name_bytes=b'\x00')
    root_dir += build_dir_record(extent_lba=SECTOR_ROOT_DIR, data_length=SECTOR_SIZE,
                                 flags=0x02, name_bytes=b'\x01')
    root_dir += build_dir_record(extent_lba=SECTOR_BOOT_IMG_START,
                                 data_length=len(boot_img), flags=0x00,
                                 name_bytes=b'BOOT.IMG;1')
    root_dir_padded = bytes(root_dir) + b'\x00' * (SECTOR_SIZE - len(root_dir))

    # Boot Catalog (floppy emulation 模式 1.44MB, sector_count=1)
    # BIOS 把 boot.img 当作 1.44MB 软盘，加载引导扇区到 0x0000:0x7C00
    # DL=0x00 (软盘 A)，引导扇区用 INT 13h LBA=1 读 kernel
    boot_catalog = build_boot_catalog(
        boot_img_lba=SECTOR_BOOT_IMG_START,
        sector_count_512=1,
        media_type=0x02,   # 1.44MB floppy emulation
        load_segment=0x0000,
    )

    # Volume Descriptors
    total_sectors = SECTOR_ROOT_DIR + 1
    pvd = build_primary_volume_descriptor(
        volume_size_sectors=total_sectors,
        root_dir_lba=SECTOR_ROOT_DIR, root_dir_size=SECTOR_SIZE,
        l_path_table_lba=SECTOR_L_PATH_TABLE,
        m_path_table_lba=SECTOR_M_PATH_TABLE,
        path_table_size=pt_size,
    )
    brvd = build_boot_record_volume_descriptor(boot_catalog_lba=SECTOR_BOOT_CAT)
    term = build_volume_descriptor_set_terminator()

    # MBR (ISOHybrid)
    # 分区类型用 0x83 (Linux filesystem) - 所有 BIOS 都接受
    # 之前 0x17 (Hidden FAT12) 导致部分 BIOS 把分区当"隐藏"忽略, U 盘无法识别
    mbr = bytearray(b'\x00' * 512)
    mbr[0] = 0xEB; mbr[1] = 0x3C; mbr[2] = 0x90
    iso_total_512 = (total_sectors * SECTOR_SIZE) // 512
    pte = bytearray(16)
    pte[0] = 0x80; pte[4] = 0x83  # 0x83 = Linux filesystem (通用, BIOS 兼容性最好)
    pte[5] = 0xFE; pte[6] = 0xFF; pte[7] = 0xFF
    struct.pack_into('<I', pte, 8, 0)
    struct.pack_into('<I', pte, 12, iso_total_512)
    mbr[446:462] = pte
    mbr[510] = 0x55; mbr[511] = 0xAA

    # 拼接 ISO
    iso = bytearray()
    iso += bytes(mbr) + b'\x00' * (SECTOR_SIZE - 512)  # LBA 0
    iso += b'\x00' * ((SECTOR_SYSTEM_AREA_END - 1) * SECTOR_SIZE)  # LBA 1-15
    iso += pvd       # LBA 16
    iso += brvd      # LBA 17
    iso += term      # LBA 18
    iso += boot_catalog  # LBA 19
    # boot.img (720 扇区)
    img_padded = boot_img + b'\x00' * (BOOT_IMG_SECTORS * SECTOR_SIZE - len(boot_img))
    iso += img_padded
    iso += l_path_padded
    iso += m_path_padded
    iso += root_dir_padded

    # 先写到桌面根目录（沙箱允许），再复制到 C:\qemu_iso 给 QEMU 用
    desktop_path = os.path.join(ROOT, 'system.iso')
    with open(desktop_path, 'wb') as f:
        f.write(bytes(iso))

    # 复制到 C:\qemu_iso 给 QEMU 测试用
    out_path = os.path.join(QEMU_ISO, 'system.iso')
    try:
        shutil.copy2(desktop_path, out_path)
        copied = True
    except Exception as e:
        copied = False
        print(f'  警告: 无法复制到 {out_path}: {e}')

    print(f'[4/5] system.iso: {len(iso)} 字节 ({len(iso)//SECTOR_SIZE} 扇区)')
    print(f'  输出: {desktop_path}')
    if copied:
        print(f'  复制: {out_path}')
    return desktop_path

# ===== 主流程 =====
if __name__ == '__main__':
    print('=== 保护模式 C 内核 ISO 构建 ===\n')

    # 确保输出目录存在
    os.makedirs(QEMU_ISO, exist_ok=True)

    # 1. 编译 C 内核
    kernel_bin = compile_kernel()

    # 读取编译后的二进制
    with open(kernel_bin, 'rb') as f:
        kernel_data_raw = f.read()

    # 保留 TCC 输出 binary 开头的 0x200 字节 0 padding
    # 这样写到 boot.img 扇区1开头后，读 LBA=1 到 0x10000，
    # kernel_main 落在 0x10000 + 0x200 = 0x10200 (PM Loader 跳转目标)
    kernel_data = kernel_data_raw
    # 验证 padding 存在
    if len(kernel_data) >= 0x200 and all(b == 0 for b in kernel_data[:0x200]):
        print(f'  保留 0x200 字节 binary padding (kernel_main 在偏移 0x200)')
        print(f'  kernel_full.bin: {len(kernel_data)} 字节')
    else:
        print(f'  警告: binary 开头无 0x200 padding，kernel_main 位置可能不对')
        print(f'  前 16 字节: {kernel_data[:16].hex()}')

    # 2. 生成内嵌 PM Loader 的引导扇区
    boot_sector, pm_loader = build_boot_sector()

    # 3. 组装 boot.img
    boot_img = build_boot_img(boot_sector, kernel_data)

    # 4. 组装 ISO
    iso_path = build_iso(boot_img)

    print('\n=== 构建完成 ===')
    print(f'引导流程:')
    print(f'  BIOS → 加载引导扇区到 0x7C00 (含 PM Loader)')
    print(f'  引导扇区 → INT 13h CHS 读 boot.img 扇区 1-32 加载到 0x10000')
    print(f'         → 0x10000-0x10200 = 0 padding (TCC binary 对齐)')
    print(f'         → 0x10200 = C Kernel 入口 (kernel_main)')
    print(f'  PM Loader (内嵌引导扇区) → 切换 32 位保护模式 → jmp 0x10200')
    print(f'  C Kernel → 清屏 → 打印 banner → CLI 交互终端')
