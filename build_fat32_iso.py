# build_fat32_iso.py - 构建 FAT32 可启动镜像 + ISO (支持 DD 模式与 ISO 模式)
#
# 结构:
#   FAT32 镜像 (32MB):
#     引导扇区 (boot_fat32.bin) -> 加载 LOADER.BIN
#     LOADER.BIN -> 加载 KERNEL.BIN/FRAMES.BIN, 播放动画, 进内核
#     KERNEL.BIN, FRAMES.BIN, EFI/BOOT/BOOTX64.EFI
#   ISO:
#     ISO9660 文件树 (含上述文件, Rufus ISO 模式提取用)
#     El Torito 硬盘模拟引导 -> FAT32 镜像 (DD 模式/BIOS 启动)
import os
import struct
import subprocess
import shutil
import sys

ROOT = r'C:\Users\PC\Desktop\根目录'
NASM = r'C:\Program Files\NASM\nasm.exe'
TCC = r'C:\tcc\tcc.exe'
IMG_SIZE = 64 * 1024 * 1024
SECTOR = 512

# FAT32 参数
# SPC=1 + 64MB 卷 -> 数据区约 12.9 万簇 (>65525), 被 EDK2/真机 UEFI 承认为 FAT32;
# (32MB/SPC8 仅约 8 千簇, UEFI FAT 驱动会误判为 FAT16 而拒绝挂载 -> 无 FS0)
SPC = 1                  # 每簇 1 扇区 (512B)
RESERVED = 32
NUM_FATS = 2
ROOT_CLUSTER = 2
SPT, HEADS = 63, 255

total_sectors = IMG_SIZE // SECTOR

def calc_fat_size():
    # 迭代计算每 FAT 扇区数
    spf = 1
    for _ in range(10):
        data_start = RESERVED + NUM_FATS * spf
        clusters = (total_sectors - data_start) // SPC
        need = (clusters * 4 + SECTOR - 1) // SECTOR
        if need == spf:
            break
        spf = need
    data_start = RESERVED + NUM_FATS * spf
    clusters = (total_sectors - data_start) // SPC
    return spf, data_start, clusters

SPF, DATA_START, CLUSTERS = calc_fat_size()

def cluster_sector(c):
    return DATA_START + (c - 2) * SPC

print('=== FAT32 + 动画 ISO 构建 ===')
print(f'总扇区={total_sectors}, 每FAT扇区={SPF}, 数据区={DATA_START}, 簇数={CLUSTERS}')

# ===== 1. 编译引导扇区与 loader =====
def nasm(src, out):
    r = subprocess.run([NASM, '-f', 'bin', '-o', out, src],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout); print(r.stderr)
        raise RuntimeError(f'NASM 失败: {src}')
    print(f'  {os.path.basename(out)}: {os.path.getsize(out)} 字节')

print('[1/6] 汇编引导扇区与 loader')
nasm(os.path.join(ROOT, 'boot_fat32.asm'), os.path.join(ROOT, 'boot_fat32.bin'))
nasm(os.path.join(ROOT, 'loader.asm'), os.path.join(ROOT, 'loader.bin'))
nasm(os.path.join(ROOT, 'mbr.asm'), os.path.join(ROOT, 'mbr.bin'))
nasm(os.path.join(ROOT, 'cdshim.asm'), os.path.join(ROOT, 'cdshim.bin'))
# 真正的 UEFI x64 引导程序 (PE32+): 读 KERNEL.BIN + GOP 图形 + ExitBootServices
nasm(os.path.join(ROOT, 'efi64.asm'), os.path.join(ROOT, 'BOOTX64.EFI'))

# ===== 2. 编译内核 (WSL TCC, 输出带 0x200 padding, kernel_main 在偏移 0x200) =====
print('[2/6] 生成中文字库 + 编译内核 (WSL TCC)')
QEMU_ISO = r'C:\qemu_iso'
os.makedirs(QEMU_ISO, exist_ok=True)
# 2a. 扫描 kernel_full.c 实际用到的字符, 用 Noto Sans SC 光栅化生成六档字库
#     (12/16/22 全字符集 + 104/128/160 数字集, 按 GOP 缩放比运行时选最近档)
for _px, _mode in ((12, 'full'), (16, 'full'), (22, 'full'),
                   (104, 'digits'), (128, 'digits'), (160, 'digits')):
    gr = subprocess.run([sys.executable, '-u', os.path.join(ROOT, 'gen_font.py'),
                         str(_px), _mode],
                        capture_output=True, text=True, timeout=120)
    print(gr.stdout.strip())
    if gr.returncode != 0:
        print(gr.stdout); print(gr.stderr); raise RuntimeError('gen_font.py 失败')
shutil.copy2(os.path.join(ROOT, 'kernel_full.c'),
             os.path.join(QEMU_ISO, 'kernel_full.c'))
shutil.copy2(os.path.join(ROOT, 'font_fb.h'),
             os.path.join(QEMU_ISO, 'font_fb.h'))
shutil.copy2(os.path.join(ROOT, 'usb_xhci.h'),
             os.path.join(QEMU_ISO, 'usb_xhci.h'))
shutil.copy2(os.path.join(ROOT, 'gui_cn.h'),
             os.path.join(QEMU_ISO, 'gui_cn.h'))
shutil.copy2(os.path.join(ROOT, 'ui_shell.h'),
             os.path.join(QEMU_ISO, 'ui_shell.h'))
shutil.copy2(os.path.join(ROOT, 'i2c_hid.h'),
             os.path.join(QEMU_ISO, 'i2c_hid.h'))
shutil.copy2(os.path.join(ROOT, 'hwprobe.h'),
             os.path.join(QEMU_ISO, 'hwprobe.h'))
shutil.copy2(os.path.join(ROOT, 'cterm.h'),
             os.path.join(QEMU_ISO, 'cterm.h'))
shutil.copy2(os.path.join(ROOT, 'bootanim.h'),
             os.path.join(QEMU_ISO, 'bootanim.h'))
for _px in (12, 16, 22, 104, 128, 160):
    shutil.copy2(os.path.join(ROOT, 'font_cn%d.h' % _px),
                 os.path.join(QEMU_ISO, 'font_cn%d.h' % _px))
kernel_out = os.path.join(ROOT, 'kernel.bin')
cmd = (
    'wsl -u root -e bash -c '
    '"cd /mnt/c/qemu_iso && '
    'tcc -m32 -nostdlib -static -ffreestanding '
    '-Wl,-Ttext,0x300200 -Wl,--oformat,binary '
    '-o kernel_full.bin kernel_full.c"'
)
r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=60)
if r.returncode != 0:
    print(r.stderr); raise RuntimeError('TCC 编译失败')
shutil.copy2(os.path.join(QEMU_ISO, 'kernel_full.bin'), kernel_out)
ksz = os.path.getsize(kernel_out)
if ksz > 2 * 1024 * 1024:
    raise RuntimeError(
        f'kernel.bin {ksz} 字节超过 2MB 闸门 (基址 0x300000+2MB 越过 0x500000), '
        '需砍 12 档字库或精简驱动')
print(f'  kernel.bin: {ksz} 字节 (闸门 2MB 通过)')

# ===== 3. 读取所有文件数据 =====
print('[3/6] 读取文件数据')
with open(os.path.join(ROOT, 'boot_fat32.bin'), 'rb') as f:
    boot_sec = f.read()
with open(os.path.join(ROOT, 'mbr.bin'), 'rb') as f:
    mbr_sec = f.read()
with open(os.path.join(ROOT, 'cdshim.bin'), 'rb') as f:
    shim_sec = f.read()
with open(os.path.join(ROOT, 'loader.bin'), 'rb') as f:
    loader_data = f.read()
with open(kernel_out, 'rb') as f:
    kernel_data = f.read()
with open(os.path.join(ROOT, 'frames.bin'), 'rb') as f:
    frames_data = f.read()
# EFI 引导程序 (沿用之前生成的)
efi_src = os.path.join(ROOT, 'BOOTX64.EFI')
if os.path.exists(efi_src):
    with open(efi_src, 'rb') as f:
        efi_data = f.read()
else:
    efi_data = b''
print(f'  loader={len(loader_data)}, kernel={len(kernel_data)}, '
      f'frames={len(frames_data)}, efi={len(efi_data)}')

# ===== 4. 构建 FAT32 镜像 =====
print('[4/6] 构建 FAT32 镜像')
img = bytearray(IMG_SIZE)

# --- 4a. 引导扇区: 用 boot_fat32.bin 但需要回填 BPB 的动态字段 ---
bs = bytearray(boot_sec)
struct.pack_into('<I', bs, 32, total_sectors)   # 总扇区 32
struct.pack_into('<I', bs, 36, SPF)             # 每 FAT 扇区
img[0:512] = bs

# --- FSInfo 扇区 (保留扇区 1) ---
fsinfo = bytearray(512)
fsinfo[0:4] = b'RRaA'
fsinfo[484:488] = b'rrAa'
struct.pack_into('<I', fsinfo, 488, 0xFFFFFFFF)  # free count unknown
struct.pack_into('<I', fsinfo, 492, 0xFFFFFFFF)  # next free unknown
fsinfo[510:512] = b'\x55\xAA'
img[SECTOR:2*SECTOR] = fsinfo

# --- 备份引导扇区 (扇区 6, 按 BPB) ---
img[6*SECTOR:7*SECTOR] = bs
img[7*SECTOR:8*SECTOR] = fsinfo

# --- 4b. 文件簇分配 ---
# cluster 2 = 根目录
# 文件按顺序分配: loader(3), kernel(4), frames(...), efi(...)
alloc = []  # (name, ext, attr, start_cluster, data)
def add_file(name, ext, data, attr=0x20):
    clusters_needed = (len(data) + SPC * SECTOR - 1) // (SPC * SECTOR)
    start = 3 + sum((len(d) + SPC*SECTOR - 1)//(SPC*SECTOR) for _,_,_,_,d in alloc)
    alloc.append((name, ext, attr, start, data))
    return start, clusters_needed

loader_start, _ = add_file('LOADER', 'BIN', loader_data)
kernel_start, _ = add_file('KERNEL', 'BIN', kernel_data)
frames_start, _ = add_file('FRAMES', 'BIN', frames_data)
# EFI 需要子目录 EFI/BOOT, 简化: 直接放根目录 BOOTX64.EFI 不行, UEFI 要求 /EFI/BOOT/
# 为简化 FAT32 实现 (只支持根目录), 我们创建两个目录簇: EFI 和 BOOT
efi_start, _ = add_file('BOOTX64', 'EFI', efi_data)

# 根目录项
def dir_entry(name, ext, attr, cluster, size):
    e = bytearray(32)
    e[0:8] = name.encode().upper().ljust(8, b' ')[:8]
    e[8:11] = ext.encode().upper().ljust(3, b' ')[:3]
    e[11] = attr
    struct.pack_into('<H', e, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into('<H', e, 26, cluster & 0xFFFF)
    struct.pack_into('<I', e, 28, size)
    return bytes(e)

# EFI 目录簇 (在文件之后追加)
efi_dir_cluster = 3 + sum((len(d) + SPC*SECTOR - 1)//(SPC*SECTOR) for _,_,_,_,d in alloc)
boot_dir_cluster = efi_dir_cluster + 1
# 重新分配 EFI 文件簇 (在目录之后)
# 重做分配, 按簇号顺序: loader, kernel, frames, EFI 目录, BOOT 目录, BOOTX64.EFI
alloc = []
loader_start = 3
loader_n = (len(loader_data) + SPC*512 - 1)//(SPC*512)
kernel_start = loader_start + loader_n
kernel_n = (len(kernel_data) + SPC*512 - 1)//(SPC*512)
frames_start = kernel_start + kernel_n
frames_n = (len(frames_data) + SPC*512 - 1)//(SPC*512)
efi_dir_c = frames_start + frames_n          # \EFI 目录
boot_dir_c = efi_dir_c + 1                   # \EFI\BOOT 目录
efi_file_c = boot_dir_c + 1                  # BOOTX64.EFI
efi_n = max(1, (len(efi_data) + SPC*512 - 1)//(SPC*512)) if efi_data else 1

# 根目录 (cluster 2)
root_dir = bytearray(SPC * SECTOR)
root_dir[0:32] = dir_entry('LOADER', 'BIN', 0x20, loader_start, len(loader_data))
root_dir[32:64] = dir_entry('KERNEL', 'BIN', 0x20, kernel_start, len(kernel_data))
root_dir[64:96] = dir_entry('FRAMES', 'BIN', 0x20, frames_start, len(frames_data))
root_dir[96:128] = dir_entry('EFI', '', 0x10, efi_dir_c, 0)  # 目录

# \EFI 目录
efi_dir = bytearray(SPC * SECTOR)
efi_dir[0:32] = b'.          ' + bytes([0x10]) + b'\x00'*20
efi_dir[0:32] = dir_entry('.', '', 0x10, efi_dir_c, 0)
efi_dir[32:64] = dir_entry('..', '', 0x10, ROOT_CLUSTER, 0)
efi_dir[64:96] = dir_entry('BOOT', '', 0x10, boot_dir_c, 0)

# \EFI\BOOT 目录
boot_dir = bytearray(SPC * SECTOR)
boot_dir[0:32] = dir_entry('.', '', 0x10, boot_dir_c, 0)
boot_dir[32:64] = dir_entry('..', '', 0x10, efi_dir_c, 0)
boot_dir[64:96] = dir_entry('BOOTX64', 'EFI', 0x20, efi_file_c, len(efi_data))

def write_cluster(cluster, data):
    sec = cluster_sector(cluster)
    if len(data) < SPC * SECTOR:
        data = data + b'\x00' * (SPC * SECTOR - len(data))
    img[sec*SECTOR:sec*SECTOR + SPC*SECTOR] = data

write_cluster(ROOT_CLUSTER, bytes(root_dir))
write_cluster(efi_dir_c, bytes(efi_dir))
write_cluster(boot_dir_c, bytes(boot_dir))

# --- 写文件数据 ---
def write_file_clusters(start, data):
    off = 0
    c = start
    while off < len(data):
        chunk = data[off:off + SPC*SECTOR]
        write_cluster(c, chunk)
        off += SPC*SECTOR
        c += 1

write_file_clusters(loader_start, loader_data)
write_file_clusters(kernel_start, kernel_data)
write_file_clusters(frames_start, frames_data)
if efi_data:
    write_file_clusters(efi_file_c, efi_data)

# --- 4c. FAT 表 ---
def build_fat():
    fat = bytearray(SPF * SECTOR)
    struct.pack_into('<I', fat, 0, 0x0FFFFFF8)
    struct.pack_into('<I', fat, 4, 0x0FFFFFFF)
    struct.pack_into('<I', fat, 8, 0x0FFFFFFF)  # 根目录 EOC
    def chain(start, n):
        for i in range(n):
            c = start + i
            v = 0x0FFFFFFF if i == n - 1 else c + 1
            struct.pack_into('<I', fat, c*4, v)
    chain(loader_start, loader_n)
    chain(kernel_start, kernel_n)
    chain(frames_start, frames_n)
    struct.pack_into('<I', fat, efi_dir_c*4, 0x0FFFFFFF)
    struct.pack_into('<I', fat, boot_dir_c*4, 0x0FFFFFFF)
    if efi_data:
        chain(efi_file_c, efi_n)
    else:
        struct.pack_into('<I', fat, efi_file_c*4, 0x0FFFFFFF)
    return bytes(fat)

fat = build_fat()
fat0 = RESERVED * SECTOR
img[fat0:fat0 + len(fat)] = fat
img[fat0 + SPF*SECTOR:fat0 + 2*SPF*SECTOR] = fat

fat_img_path = os.path.join(ROOT, 'fat32.img')
with open(fat_img_path, 'wb') as f:
    f.write(bytes(img))
print(f'  fat32.img: {len(img)} 字节')
print(f'  LOADER@簇{loader_start}, KERNEL@簇{kernel_start}, '
      f'FRAMES@簇{frames_start}, EFI@簇{efi_file_c}')

# ===== 5. 构建 ISO =====
print('[5/6] 构建 ISO (ISO9660 + El Torito 硬盘模拟)')
sys.path.insert(0, ROOT)
from build_iso import (
    build_primary_volume_descriptor,
    build_boot_catalog, build_boot_record_volume_descriptor,
    build_volume_descriptor_set_terminator,
    build_path_table_record, build_dir_record,
)

ISO_SECTOR = 2048
fat_sectors = (len(img) + ISO_SECTOR - 1) // ISO_SECTOR

# ISO 布局 (2048 字节扇区)
LBA_PVD, LBA_BRVD, LBA_TERM, LBA_CAT = 16, 17, 18, 19
LBA_PTL, LBA_PTM = 20, 21
LBA_ROOT = 22
LBA_EFI_DIR = 23
LBA_BOOT_DIR = 24
LBA_FILE_START = 25

# 文件分配 (ISO9660)
iso_files = [
    ('LOADER.BIN', loader_data),
    ('KERNEL.BIN', kernel_data),
    ('FRAMES.BIN', frames_data),
]
file_lba = {}
cur = LBA_FILE_START
for name, data in iso_files:
    file_lba[name] = cur
    cur += (len(data) + ISO_SECTOR - 1) // ISO_SECTOR
LBA_EFI_FILE = cur
cur += (len(efi_data) + ISO_SECTOR - 1) // ISO_SECTOR if efi_data else 1
# El Torito 硬盘模拟引导映像: 1 个 2048 扇区(内含 MBR) + fat32.img
LBA_ET_MBR = cur                 # El Torito 引导映像起点 (其前 512B 是 MBR)
LBA_FAT_IMG = cur + 1            # FAT 卷 (跳过含 MBR 的 2048 扇区)
TOTAL_ISO_SECTORS = LBA_FAT_IMG + fat_sectors + 1

iso = bytearray()

# 系统区 16 扇区: LBA0 放 isohybrid MBR (DD 写入后 BIOS 从 MBR 链加载 FAT 分区)
# MBR 分区表项1 指向 fat32.img (在 ISO 中的 512 字节扇区偏移)
part_sectors = len(img) // SECTOR

def make_mbr(part_start_512):
    """用 mbr.bin 生成一份回填了分区表项1 的 MBR (512 字节)。"""
    m = bytearray(mbr_sec)
    m[446] = 0x80                            # 可引导标志
    m[447:450] = bytes([0xFE, 0xFF, 0xFF])  # 起始 CHS (LBA 忽略)
    m[450] = 0x0C                            # 分区类型: FAT32 LBA
    m[451:454] = bytes([0xFE, 0xFF, 0xFF])  # 结束 CHS
    struct.pack_into('<I', m, 454, part_start_512)
    struct.pack_into('<I', m, 458, part_sectors)
    return bytes(m)

# DD MBR(ISO LBA0): FAT 卷相对 USB 物理头的绝对 512 扇区偏移
dd_mbr = make_mbr(LBA_FAT_IMG * 4)
# El Torito no-emulation MBR(引导映像首扇区): shim 会把 512-LBA 透明转换成
# 2048 物理块, 故分区起点与 DD 相同 = FAT 卷的绝对 512 虚拟扇区偏移。
et_mbr = make_mbr(LBA_FAT_IMG * 4)

sys_area = bytearray(16 * ISO_SECTOR)
sys_area[0:512] = dd_mbr
iso += bytes(sys_area)

# PVD
iso += build_primary_volume_descriptor(
    volume_size_sectors=TOTAL_ISO_SECTORS,
    root_dir_lba=LBA_ROOT, root_dir_size=2048,
    l_path_table_lba=LBA_PTL, m_path_table_lba=LBA_PTM,
    path_table_size=2048,
)
# BRVD
iso += build_boot_record_volume_descriptor(boot_catalog_lba=LBA_CAT)
# Terminator
iso += build_volume_descriptor_set_terminator()
# Boot Catalog: media_type=0x00 无模拟(no-emulation)。加载引导映像前 4 个
# 512 虚拟扇区(=1 个 2048 物理块)到 0x7C00: 前 512B 是 MBR, 随后是 cdshim。
iso += build_boot_catalog(
    boot_img_lba=LBA_ET_MBR, sector_count_512=4,
    media_type=0x00, load_segment=0x0000,
)

# 路径表 (小端): root(1), EFI(2), BOOT(3)
ptl = bytearray(2048)
r1 = build_path_table_record(b'\x00', LBA_ROOT, 1)
r2 = build_path_table_record(b'EFI', LBA_EFI_DIR, 1)
r3 = build_path_table_record(b'BOOT', LBA_BOOT_DIR, 2)
off = 0
for r in (r1, r2, r3):
    ptl[off:off+len(r)] = r
    off += len(r)
iso += bytes(ptl)
# 路径表 (大端)
import struct as _struct
ptm = bytearray(2048)
r1b = build_path_table_record(b'\x00', LBA_ROOT, 1, big_endian=True)
r2b = build_path_table_record(b'EFI', LBA_EFI_DIR, 1, big_endian=True)
r3b = build_path_table_record(b'BOOT', LBA_BOOT_DIR, 2, big_endian=True)
off = 0
for r in (r1b, r2b, r3b):
    ptm[off:off+len(r)] = r
    off += len(r)
iso += bytes(ptm)

# 根目录记录扇区
rootd = bytearray()
rootd += build_dir_record(LBA_ROOT, 2048, 0x02, b'\x00')
rootd += build_dir_record(LBA_ROOT, 2048, 0x02, b'\x01')
for name, _ in iso_files:
    rootd += build_dir_record(file_lba[name],
        len(dict(iso_files)[name]), 0x00, (name + ';1').encode())
rootd += build_dir_record(LBA_EFI_DIR, 2048, 0x02, b'EFI')
rootd += b'\x00' * (2048 - len(rootd))
iso += bytes(rootd)

# EFI 目录
efid = bytearray()
efid += build_dir_record(LBA_EFI_DIR, 2048, 0x02, b'\x00')
efid += build_dir_record(LBA_ROOT, 2048, 0x02, b'\x01')
efid += build_dir_record(LBA_BOOT_DIR, 2048, 0x02, b'BOOT')
efid += b'\x00' * (2048 - len(efid))
iso += bytes(efid)

# EFI/BOOT 目录
bootd = bytearray()
bootd += build_dir_record(LBA_BOOT_DIR, 2048, 0x02, b'\x00')
bootd += build_dir_record(LBA_EFI_DIR, 2048, 0x02, b'\x01')
if efi_data:
    bootd += build_dir_record(LBA_EFI_FILE, len(efi_data), 0x00, b'BOOTX64.EFI;1')
bootd += b'\x00' * (2048 - len(bootd))
iso += bytes(bootd)

# 普通文件
file_map = dict(iso_files)
for name, _ in iso_files:
    data = file_map[name]
    iso += data + b'\x00' * ((ISO_SECTOR - len(data) % ISO_SECTOR) % ISO_SECTOR)
# EFI 文件
if efi_data:
    iso += efi_data + b'\x00' * ((ISO_SECTOR - len(efi_data) % ISO_SECTOR) % ISO_SECTOR)
else:
    iso += b'\x00' * ISO_SECTOR

# El Torito no-emulation 引导映像首块(2048B): [MBR 512][cdshim 512..2047]。
# BIOS 把整块载入 0x7C00, MBR 检测到光驱后将 cdshim 复制到 0xB000 挂钩 INT13,
# 之后整个 FAT 引导链的 512 扇区读盘都被透明转换为光驱 2048 物理块读。
et_sector = bytearray(ISO_SECTOR)
et_sector[0:512] = et_mbr
assert len(shim_sec) <= 1536, len(shim_sec)
et_sector[512:512 + len(shim_sec)] = shim_sec
assert len(iso) == LBA_ET_MBR * ISO_SECTOR, (len(iso), LBA_ET_MBR * ISO_SECTOR)
iso += bytes(et_sector)
assert len(iso) == LBA_FAT_IMG * ISO_SECTOR
iso += bytes(img)
if len(iso) % ISO_SECTOR:
    iso += b'\x00' * (ISO_SECTOR - len(iso) % ISO_SECTOR)

iso_path = os.path.join(ROOT, 'system.iso')
with open(iso_path, 'wb') as f:
    f.write(bytes(iso))
print(f'[6/6] system.iso: {len(iso)} 字节 ({len(iso)//ISO_SECTOR} 扇区)')
print(f'  FAT 映像 @ ISO LBA {LBA_FAT_IMG}')
print('\n完成!')
print('DD 模式: BIOS -> MBR(LBA0) -> FAT 分区 VBR -> loader -> 动画 -> 内核')
print('CD/El Torito: no-emulation -> MBR(INT13 读光驱) -> FAT VBR -> loader')
print('ISO 模式(Rufus): 提取 ISO9660 文件, UEFI 走 /EFI/BOOT/BOOTX64.EFI')
print(f'MBR 分区1: 起始 {LBA_FAT_IMG*4} (512B扇区), 共 {part_sectors} 扇区, 类型 0x0C')
print(f'El Torito 引导映像 @ LBA {LBA_ET_MBR} (MBR) + FAT @ LBA {LBA_FAT_IMG}')

# ===== 7. UEFI/Legacy 双兼容裸盘镜像 (dd 到 U 盘; 纯 UEFI 真机用这个) =====
# 布局: LBA0 MBR(分区1: type=0xEF ESP, start=2048), LBA2048 起完整 FAT32 卷
#   UEFI:   固件识别 MBR 上的 0xEF ESP -> 挂载 FAT32 -> \EFI\BOOT\BOOTX64.EFI
#   Legacy: MBR -> 读 VBR@LBA2048 (mbr.asm 运行时回填隐藏扇区) -> loader 原链
print('[7] 构建 UEFI/Legacy 双兼容磁盘镜像 uefi_boot.img')
ESP_PAD = 2048                              # 前 1MiB (2048 扇区) 对齐保留
esp_sectors = len(img) // SECTOR            # FAT32 卷 = 32MiB
um = bytearray(mbr_sec)
um[446] = 0x80                              # 可引导标志
um[447:450] = bytes([0x00, 0x01, 0x00])    # 起始 CHS 0/0/1
um[450] = 0xEF                              # 分区类型: EFI System Partition
um[451:454] = bytes([0xFE, 0xFF, 0xFF])    # 结束 CHS (LBA 忽略)
struct.pack_into('<I', um, 454, ESP_PAD)    # 分区起始 LBA
struct.pack_into('<I', um, 458, esp_sectors)
um[462:510] = b'\x00' * 48                  # 清分区项 2-4
disk = bytearray(ESP_PAD * SECTOR + len(img))
disk[0:512] = um
_base = ESP_PAD * SECTOR
disk[_base:_base + len(img)] = bytes(img)
uefi_path = os.path.join(ROOT, 'uefi_boot.img')
with open(uefi_path, 'wb') as f:
    f.write(bytes(disk))
print(f'  uefi_boot.img: {len(disk)} 字节 (ESP @ LBA {ESP_PAD}, {esp_sectors} 扇区)')
