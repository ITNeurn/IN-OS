# -*- coding: utf-8 -*-
# 检查 ISO 中 Rufus 关心的结构：MBR、PVD、BRVD、Boot Catalog
import struct

ISO = r'C:\qemu_iso\system.iso'

with open(ISO, 'rb') as f:
    data = f.read()

SECTOR = 2048

print(f'ISO 大小: {len(data)} 字节 = {len(data)//SECTOR} 扇区 (2048B)')
print(f'         = {len(data)//512} 扇区 (512B)')
print()

# === MBR (第一个 512 字节) ===
print('=== MBR (LBA 0, 前 512 字节) ===')
mbr = data[:512]
print(f'引导代码前 16 字节: {mbr[:16].hex()}')
print(f'MBR 签名 (510-511): {mbr[510]:02x} {mbr[511]:02x}  ', end='')
print('✓ 55AA' if mbr[510] == 0x55 and mbr[511] == 0xAA else '✗ 无效签名')

# 分区表 (446-509, 4 个 16 字节项)
print('分区表:')
for i in range(4):
    off = 446 + i * 16
    entry = mbr[off:off+16]
    status = entry[0]
    ptype = entry[4]
    lba_start = struct.unpack('<I', entry[8:12])[0]
    lba_size = struct.unpack('<I', entry[12:16])[0]
    if status == 0 and ptype == 0 and lba_start == 0 and lba_size == 0:
        print(f'  分区 {i+1}: 空')
    else:
        print(f'  分区 {i+1}: status=0x{status:02x} type=0x{ptype:02x} '
              f'LBA_start={lba_start} size={lba_size}')
print()

# === PVD (LBA 16) ===
print('=== PVD (LBA 16) ===')
pvd = data[16*SECTOR:(16+1)*SECTOR]
print(f'Type: {pvd[0]} (应为 1)')
print(f'Std ID: {pvd[1:6]} (应为 CD001)')
print(f'Version: {pvd[6]} (应为 1)')
print(f'Volume ID: {pvd[40:72].rstrip(b" ").decode("ascii", errors="replace")}')
vol_size = struct.unpack('<I', pvd[80:84])[0]
print(f'Volume Space Size: {vol_size} 扇区 (LE)')
log_block_size = struct.unpack('<H', pvd[128:130])[0]
print(f'Logical Block Size: {log_block_size} (应为 2048)')
pt_size = struct.unpack('<I', pvd[132:136])[0]
print(f'Path Table Size: {pt_size} 字节')
l_pt_lba = struct.unpack('<I', pvd[140:144])[0]
m_pt_lba = struct.unpack('>I', pvd[148:152])[0]
print(f'L Path Table LBA: {l_pt_lba}')
print(f'M Path Table LBA: {m_pt_lba}')
print(f'File Structure Version: {pvd[881]} (应为 1)')
print()

# === BRVD (LBA 17) ===
print('=== Boot Record VD (LBA 17) ===')
brvd = data[17*SECTOR:(17+1)*SECTOR]
print(f'Type: {brvd[0]} (应为 0)')
print(f'Std Id: {brvd[1:6]} (应为 CD001)')
print(f'Version: {brvd[6]} (应为 1)')
print(f'Boot System Id: {brvd[7:39].rstrip(b" ").decode("ascii", errors="replace")}')
boot_cat_lba = struct.unpack('<I', brvd[71:75])[0]
print(f'Boot Catalog LBA (offset 71-74): {boot_cat_lba}')
# 检查 72-75 是否也有值（错误位置）
boot_cat_lba_wrong = struct.unpack('<I', brvd[72:76])[0]
print(f'  (offset 72-75 的值: {boot_cat_lba_wrong} — 应为 0 或无意义)')
print()

# === VD Terminator (LBA 18) ===
print('=== VD Terminator (LBA 18) ===')
term = data[18*SECTOR:(18+1)*SECTOR]
print(f'Type: {term[0]} (应为 255)')
print(f'Std Id: {term[1:6]} (应为 CD001)')
print()

# === Boot Catalog (LBA 19) ===
print('=== Boot Catalog (LBA 19) ===')
if boot_cat_lba != 19:
    print(f'⚠ Boot Catalog LBA={boot_cat_lba}, 不是 19！实际检查 LBA {boot_cat_lba}')
    bc_off = boot_cat_lba * SECTOR
else:
    bc_off = 19 * SECTOR

bc = data[bc_off:bc_off+SECTOR]
# Validation Entry (32 bytes)
print('--- Validation Entry ---')
print(f'Header ID: {bc[0]} (应为 1)')
print(f'Platform ID: {bc[1]} (0=x86, 0xEF=EFI)')
id_str = bc[4:28].rstrip(b'\x00').decode('ascii', errors='replace')
print(f'ID String: "{id_str}"')
checksum = struct.unpack('<H', bc[28:30])[0]
print(f'Checksum: 0x{checksum:04x}')
# 验证 checksum
csum_calc = 0
for i in range(0, 32, 2):
    csum_calc += struct.unpack('<H', bc[i:i+2])[0]
csum_calc &= 0xFFFF
print(f'  验证: 全32字节求和={csum_calc} (应为 0)')
print(f'Key: {bc[30]:02x} {bc[31]:02x} (应为 55 AA)')

# Initial/Default Entry (32 bytes, offset 32)
print('--- Initial/Default Entry ---')
ie = bc[32:64]
print(f'Boot Indicator: 0x{ie[0]:02x} (0x88=bootable)')
print(f'Media Type: 0x{ie[1]:02x} (0x00=no-emulation, 0x02=1.44MB floppy)')
load_seg = struct.unpack('<H', ie[2:4])[0]
print(f'Load Segment: 0x{load_seg:04x} (0x0000=default 0x07C0)')
sector_count = struct.unpack('<H', ie[6:8])[0]
print(f'Sector Count (512B): {sector_count}')
boot_img_lba = struct.unpack('<I', ie[8:12])[0]
print(f'Boot Image LBA: {boot_img_lba}')

# 检查 boot image 位置
print()
print('=== Boot Image 检查 ===')
bi_off = boot_img_lba * SECTOR
if bi_off + 512 <= len(data):
    bi_first_sector = data[bi_off:bi_off+512]
    print(f'Boot Image 第一个 512 字节最后 2 字节: {bi_first_sector[510]:02x} {bi_first_sector[511]:02x}')
    print(f'  ', end='')
    print('✓ 55AA 引导扇区签名' if bi_first_sector[510]==0x55 and bi_first_sector[511]==0xAA else '✗ 无引导扇区签名')
    print(f'Boot Image 总大小: {len(data) - bi_off} 字节 (到 ISO 末尾)')
else:
    print(f'✗ Boot Image LBA {boot_img_lba} 超出 ISO 范围')

# === EFI 检查 ===
print()
print('=== EFI 引导路径检查 ===')
# 查找 EFI/BOOT/BOOTX64.EFI
# 先从根目录找 EFI 目录
root_lba = struct.unpack('<I', pvd[156:160])[0]
root_size = struct.unpack('<I', pvd[166:170])[0]
print(f'根目录 LBA: {root_lba}, 大小: {root_size}')
root_data = data[root_lba*SECTOR:(root_lba*SECTOR)+root_size]
# 遍历目录记录
pos = 0
while pos < len(root_data) and root_data[pos] != 0:
    rec_len = root_data[pos]
    if rec_len == 0:
        break
    name_len = root_data[pos+32]
    name = root_data[pos+33:pos+33+name_len].decode('ascii', errors='replace')
    if name.startswith('\x00'):
        name = '.'
    elif name.startswith('\x01'):
        name = '..'
    print(f'  根目录条目: "{name}" rec_len={rec_len}')
    pos += rec_len
