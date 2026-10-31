# -*- coding: utf-8 -*-
# verify_iso.py 验证 build_iso.py 产出的引导扇区/软盘镜像/ISO 镜像的关键结构。
import os, struct, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SECTOR = 2048

def hexlify(b, n=16):
    return b[:n].hex()

print('=== 产物存在性检查 ===')
for n in ['bootloader.bin', 'loader.bin', 'boot.img', 'system.iso']:
    p = os.path.join(ROOT, n)
    ok = os.path.exists(p)
    sz = os.path.getsize(p) if ok else 0
    print('  {:18s} exists={} size={}'.format(n, ok, sz))

print()
print('=== boot.img 引导扇区 ===')
with open(os.path.join(ROOT, 'boot.img'), 'rb') as f:
    img = f.read()
print('  size: {} bytes (= {:.2f} MB)'.format(len(img), len(img) / 1024 / 1024))
print('  boot sector first 16B: {}'.format(hexlify(img[:16])))
print('  boot sector signature: {:02X} {:02X}  (expect 55 AA)'.format(img[510], img[511]))
print('  loader first 16B (off=512): {}'.format(hexlify(img[512:512+16])))
print('  loader expected to start with 31 C0 8E D8 8E C0 (xor ax,ax; mov ds,ax; mov es,ax)')

print()
print('=== system.iso 结构 ===')
with open(os.path.join(ROOT, 'system.iso'), 'rb') as f:
    iso = f.read()
print('  total size: {} bytes = {} sectors (2048B each)'.format(len(iso), len(iso) // SECTOR))

# Sector 16 (offset 32768): Boot Record VD
brvd = iso[32768:32768 + SECTOR]
print('  Sector 16 Boot Record VD:')
print('    type byte: 0x{:02X} (expect 0x00)'.format(brvd[0]))
print('    std id:    {} (expect CD001)'.format(brvd[1:6].decode('ascii', 'replace')))
print('    version:   {} (expect 1)'.format(brvd[6]))
torito_id = brvd[7:39].rstrip(b'\x00').decode('ascii', 'replace')
print('    boot sys:  {} (expect EL TORITO SPECIFICATION)'.format(torito_id))
boot_cat_lba = struct.unpack('<I', brvd[72:76])[0]
print('    catalog LBA: {} (expect 19)'.format(boot_cat_lba))

# Sector 17 (offset 34816): Primary VD
pvd = iso[34816:34816 + SECTOR]
print('  Sector 17 Primary VD:')
print('    type byte: 0x{:02X} (expect 0x01)'.format(pvd[0]))
print('    std id:    {} (expect CD001)'.format(pvd[1:6].decode('ascii', 'replace')))
vol_id = pvd[40:72].rstrip(b' ').decode('ascii', 'replace')
print('    volume id: {}'.format(vol_id))
vol_size = struct.unpack('<I', pvd[80:84])[0]
print('    vol size:  {} sectors'.format(vol_size))
l_pt = struct.unpack('<I', pvd[140:144])[0]
m_pt = struct.unpack('>I', pvd[148:152])[0]
print('    L path table LBA: {} (expect 740)'.format(l_pt))
print('    M path table LBA: {} (expect 741)'.format(m_pt))
root_rec_len = pvd[156]
print('    root dir rec len: {} (expect 34)'.format(root_rec_len))
root_lba = struct.unpack('<I', pvd[156 + 2:156 + 6])[0]
print('    root dir extent LBA: {} (expect 742)'.format(root_lba))

# Sector 18 (offset 36864): VD Set Terminator
term = iso[36864:36864 + SECTOR]
print('  Sector 18 VD Terminator:')
print('    type byte: 0x{:02X} (expect 0xFF)'.format(term[0]))
print('    std id:    {} (expect CD001)'.format(term[1:6].decode('ascii', 'replace')))

# Sector 19 (offset 38912): Boot Catalog
bc = iso[38912:38912 + SECTOR]
print('  Sector 19 Boot Catalog:')
print('    Validation Header ID: 0x{:02X} (expect 0x01)'.format(bc[0]))
print('    Validation Platform:  0x{:02X} (expect 0x00 = x86)'.format(bc[1]))
id_str = bc[4:28].rstrip(b'\x00').decode('ascii', 'replace')
print('    Validation ID string: {}'.format(id_str))
print('    Validation Key:       0x{:02X} 0x{:02X} (expect 0x55 0xAA)'.format(bc[30], bc[31]))

# 验证 validation checksum
chksum = 0
for i in range(0, 32, 2):
    chksum += (bc[i + 1] << 8) | bc[i]
chksum &= 0xFFFF
print('    Validation checksum word sum: 0x{:04X} (expect 0x0000)'.format(chksum))

print('  Initial/Default Entry:')
print('    Boot Indicator: 0x{:02X} (expect 0x88 = bootable)'.format(bc[32]))
print('    Media Type:     0x{:02X} (expect 0x02 = 1.44MB floppy)'.format(bc[33]))
seg = bc[34] | (bc[35] << 8)
print('    Load Segment:   0x{:04X}'.format(seg))
sect_count = bc[38] | (bc[39] << 8)
print('    Sector Count:   {} (expect 2880)'.format(sect_count))
img_lba = struct.unpack('<I', bc[40:44])[0]
print('    Boot Image LBA: {} (expect 20)'.format(img_lba))

# Sector 20 起：boot.img 内容
boot_in_iso = iso[20 * SECTOR:20 * SECTOR + 512]
print('  Boot image in ISO first 16B: {}'.format(hexlify(boot_in_iso[:16])))
print('  Boot image in ISO signature: 0x{:02X} 0x{:02X} (expect 0x55 0xAA)'.format(
    boot_in_iso[510], boot_in_iso[511]))

# 根目录内容（根目录可能跨多个扇区，读取 16 扇区保险）
print()
print('=== Root Directory 文件列表 ===')
root_dir_off = 742 * SECTOR
root_dir = iso[root_dir_off:root_dir_off + SECTOR * 16]
off = 0
file_count = 0
dir_count = 0
while off + 33 < len(root_dir) and root_dir[off] != 0:
    rec_len = root_dir[off]
    if rec_len == 0:
        break
    if off + rec_len > len(root_dir):
        break
    flags = root_dir[off + 25]
    name_len = root_dir[off + 32]
    name_raw = root_dir[off + 33:off + 33 + name_len]
    if name_raw == b'\x00':
        name = '.'
    elif name_raw == b'\x01':
        name = '..'
    else:
        name = name_raw.decode('ascii', 'replace')
    fl = 'DIR ' if flags & 2 else 'FILE'
    print('    [{:3d}] len={:3d} {} {}'.format(file_count, rec_len, fl, name))
    if flags & 2:
        dir_count += 1
    else:
        file_count += 1
    off += rec_len

print()
print('  total entries: {} files, {} directories'.format(file_count, dir_count))

print()
print('=== 验证结论 ===')
ok = True
if img[510] != 0x55 or img[511] != 0xAA:
    print('  FAIL: boot.img signature wrong')
    ok = False
if brvd[0] != 0x00 or brvd[1:6] != b'CD001':
    print('  FAIL: BRVD magic wrong')
    ok = False
if pvd[0] != 0x01 or pvd[1:6] != b'CD001':
    print('  FAIL: PVD magic wrong')
    ok = False
if term[0] != 0xFF or term[1:6] != b'CD001':
    print('  FAIL: VD terminator magic wrong')
    ok = False
if bc[30] != 0x55 or bc[31] != 0xAA:
    print('  FAIL: boot catalog key wrong')
    ok = False
if chksum != 0:
    print('  FAIL: validation checksum not zero')
    ok = False
if bc[32] != 0x88:
    print('  FAIL: initial entry not bootable')
    ok = False
if bc[33] != 0x02:
    print('  FAIL: media type not 1.44MB floppy')
    ok = False
if img_lba != 20:
    print('  FAIL: boot image LBA wrong')
    ok = False
if ok:
    print('  PASS: 所有结构校验通过，ISO 可启动')
sys.exit(0 if ok else 1)
