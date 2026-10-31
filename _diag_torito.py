# -*- coding: utf-8 -*-
# _diag_torito.py 详细诊断 El Torito 引导结构。
import os, struct

ISO = r'C:\qemu_iso\system.iso'
SECTOR = 2048

with open(ISO, 'rb') as f:
    iso = f.read()

print('=== El Torito 引导结构详细诊断 ===\n')

# Boot Record VD (LBA 16)
brvd = iso[16*SECTOR:17*SECTOR]
print('[1] Boot Record VD (LBA 16):')
print(f'  Type:              0x{brvd[0]:02X} (expect 0x00)')
print(f'  Std ID:            {brvd[1:6].decode("ascii", "replace")} (expect CD001)')
print(f'  Version:           {brvd[6]} (expect 1)')
boot_sys = brvd[7:39].rstrip(b'\x00').decode('ascii', 'replace')
print(f'  Boot System ID:    "{boot_sys}"')
boot_id = brvd[39:71].rstrip(b'\x00').decode('ascii', 'replace')
print(f'  Boot ID:           "{boot_id}"')
cat_lba = struct.unpack('<I', brvd[71:75])[0]  # 注意偏移是 71 (不是 72)
print(f'  Boot Catalog LBA:  {cat_lba} (expect 19)')

# 等等，标准 El Torito 的 Boot Catalog LBA 是在偏移 0x47 = 71，长度 4 字节
# 让我重新检查。标准说从 0x47 开始 4 字节是 Boot Catalog LBA
# 0x47 = 71
# 我之前用了 brvd[72:76] = 偏移 72-75，这是错的！应该是 71-74
print(f'  字节 71-74: {brvd[71:75].hex()} = LBA {struct.unpack("<I", brvd[71:75])[0]}')
print(f'  字节 72-75: {brvd[72:76].hex()} = LBA {struct.unpack("<I", brvd[72:76])[0]} (这是我之前用的)')

# Boot Catalog (LBA 19)
bc = iso[19*SECTOR:20*SECTOR]
print(f'\n[2] Boot Catalog (LBA {cat_lba}):')
print(f'  字节 0-31 (Validation Entry):')
print(f'    Header ID:        0x{bc[0]:02X} (expect 0x01)')
print(f'    Platform ID:      0x{bc[1]:02X} (expect 0x00 = 80x86)')
print(f'    字节 2-3:          {bc[2]:02X} {bc[3]:02X} (reserved, expect 00 00)')
manuf = bc[4:28].rstrip(b' ').decode('ascii', 'replace')
print(f'    Manuf ID:         "{manuf}" (4-27)')
print(f'    Checksum:         0x{bc[28]:02X}{bc[29]:02X} (28-29)')
print(f'    Key:              0x{bc[30]:02X} 0x{bc[31]:02X} (expect 0x55 0xAA)')

# Validation checksum 计算
chksum = 0
for i in range(0, 32, 2):
    word = bc[i] | (bc[i+1] << 8)
    chksum = (chksum + word) & 0xFFFF
print(f'    Checksum 计算:    sum=0x{chksum:04X} (expect 0x0000 with checksum field filled)')

# Initial/Default Entry (字节 32-63)
print(f'\n  字节 32-63 (Initial/Default Entry):')
print(f'    Boot Indicator:   0x{bc[32]:02X} (expect 0x88 = bootable)')
print(f'    Boot Media Type:  0x{bc[33]:02X}')
print(f'      - 0x00 = no emulation')
print(f'      - 0x01 = 1.2MB floppy')
print(f'      - 0x02 = 1.44MB floppy')
print(f'      - 0x03 = 2.88MB floppy')
print(f'      - 0x04 = hard disk')
print(f'    字节 34-35:       0x{bc[34]:02X}{bc[35]:02X} (load segment, 0=default 0x07C0)')
load_seg = struct.unpack('<H', bc[34:36])[0]
print(f'    Load Segment:     0x{load_seg:04X} (0 = use 0x07C0, 7C0 means load to 0x7C00)')
sys_type = bc[36]
print(f'    System Type:      0x{sys_type:02X} (unused, must be 0)')
print(f'    字节 37:          0x{bc[37]:02X} (unused, must be 0)')
sec_count = struct.unpack('<H', bc[38:40])[0]
print(f'    Sector Count:     {sec_count} (16-bit, in 512-byte virtual sectors)')
load_rba = struct.unpack('<I', bc[40:44])[0]
print(f'    Load RBA:         {load_rba} (LBA in 2048-byte sectors)')
print(f'    字节 44-47:       {bc[44:48].hex()} (reserved)')
print(f'    字节 48-63:       {bc[48:64].hex()} (reserved)')

# 显示完整字节
print(f'\n  Validation Entry (0-31) hex:')
print(f'    {bc[0:16].hex()}')
print(f'    {bc[16:32].hex()}')
print(f'  Initial Entry (32-63) hex:')
print(f'    {bc[32:48].hex()}')
print(f'    {bc[48:64].hex()}')

# Boot Image (LBA 20)
print(f'\n[3] Boot Image (LBA {load_rba}):')
bi = iso[load_rba*SECTOR:load_rba*SECTOR+512]
print(f'  前 32 字节: {bi[:32].hex()}')
print(f'  510-511 签名: 0x{bi[510]:02X} 0x{bi[511]:02X} (expect 0x55 0xAA)')

# QEMU 报错分析
print(f'\n[4] QEMU SeaBIOS 错误分析:')
print(f'  错误: "Boot failed: Could not read from CDROM (code 0004)"')
print(f'  SeaBIOS 错误码 0004 通常表示读 CDROM 扇区失败')
print(f'  可能原因:')
print(f'    a) Boot Catalog Validation Entry checksum 不为 0')
print(f'       实际: sum=0x{chksum:04X}')
print(f'    b) Boot Image LBA 超出 ISO 范围')
print(f'       ISO 大小: {len(iso)} bytes = {len(iso)//SECTOR} sectors')
print(f'       Load RBA: {load_rba} (在 ISO 范围内? {load_rba < len(iso)//SECTOR})')
print(f'    c) Sector Count 与 Media Type 不匹配')
print(f'       Media=0x{bc[33]:02X}, Count={sec_count}')
print(f'       1.44MB 软盘应 = 2880 扇区 × 512 字节')
print(f'    d) Load Segment 不正确')
print(f'       实际: 0x{load_seg:04X}, 期望 0x0000 (= 默认 0x07C0)')

# 关键检查：Boot Catalog 字节是否正确
print(f'\n[5] Boot Catalog 字节级检查:')
print(f'  Validation Header ID: 0x{bc[0]:02X} ({"OK" if bc[0]==0x01 else "FAIL"})')
print(f'  Validation Platform:  0x{bc[1]:02X} ({"OK" if bc[1]==0x00 else "FAIL"})')
print(f'  Validation Key 55AA:  0x{bc[30]:02X}{bc[31]:02X} ({"OK" if bc[30]==0x55 and bc[31]==0xAA else "FAIL"})')
print(f'  Validation checksum:  0x{chksum:04X} ({"OK" if chksum==0 else "FAIL"})')
print(f'  Initial Entry Bootable: 0x{bc[32]:02X} ({"OK" if bc[32]==0x88 else "FAIL"})')
print(f'  Initial Entry Media:    0x{bc[33]:02X} ({"OK" if bc[33] in (0x00,0x01,0x02,0x03,0x04) else "FAIL"})')
print(f'  Initial Entry Load RBA: {load_rba} ({"OK" if 0 < load_rba < len(iso)//SECTOR else "FAIL"})')
print(f'  Initial Entry Sec Count: {sec_count} ({"OK" if sec_count > 0 else "FAIL"})')
