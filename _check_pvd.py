# -*- coding: utf-8 -*-
# 检查 PVD 和整体 ISO9660 结构是否合规。
import struct

ISO = r'C:\qemu_iso\system.iso'
S = 2048
with open(ISO, 'rb') as f:
    iso = f.read()

print('ISO size:', len(iso), '=', len(iso)//S, 'sectors')

# PVD 在 LBA 17
pvd = iso[17*S:18*S]
print('\n=== PVD (LBA 17) 关键字段 ===')
print(f'  Type:                0x{pvd[0]:02X} (expect 0x01)')
print(f'  Std ID:              {pvd[1:6]} (expect CD001)')
print(f'  Version:             {pvd[6]} (expect 1)')
vol_id = pvd[8:40].rstrip(b' ').decode('ascii', 'replace')
print(f'  Volume ID (8-39):    "{vol_id}"')
# Volume Space Size (LE+BE, 8 bytes at offset 80)
vol_size_le = struct.unpack('<I', pvd[80:84])[0]
vol_size_be = struct.unpack('>I', pvd[84:88])[0]
print(f'  Volume Space Size:   LE={vol_size_le}, BE={vol_size_be} (sectors)')
print(f'    ISO 实际大小:        {len(iso)//S} sectors (应该一致)')
# Path Table Size (offset 132-139, LE+BE)
pt_size_le = struct.unpack('<I', pvd[132:136])[0]
pt_size_be = struct.unpack('>I', pvd[136:140])[0]
print(f'  Path Table Size:    LE={pt_size_le}, BE={pt_size_be} bytes')
# Path Table Locations
l_pt_l = struct.unpack('<I', pvd[140:144])[0]  # L-type path table LBA
opt_l_pt_l = struct.unpack('<I', pvd[144:148])[0]  # optional L-type
m_pt_l = struct.unpack('>I', pvd[148:152])[0]  # M-type path table LBA
opt_m_pt_l = struct.unpack('>I', pvd[152:156])[0]  # optional M-type
print(f'  L Path Table LBA:    {l_pt_l}')
print(f'  Opt L Path Table:    {opt_l_pt_l}')
print(f'  M Path Table LBA:    {m_pt_l}')
print(f'  Opt M Path Table:    {opt_m_pt_l}')
# Root Directory Record (offset 156-189, 34 bytes)
root_rec = pvd[156:190]
print(f'  Root Dir Record:')
print(f'    Length:            {root_rec[0]} (expect 34)')
print(f'    ExtAttrLen:        {root_rec[1]}')
root_lba_le = struct.unpack('<I', root_rec[2:6])[0]
root_lba_be = struct.unpack('>I', root_rec[6:10])[0]
print(f'    Extent LBA:        LE={root_lba_le}, BE={root_lba_be}')
root_size_le = struct.unpack('<I', root_rec[10:14])[0]
root_size_be = struct.unpack('>I', root_rec[14:18])[0]
print(f'    Data Size:        LE={root_size_le}, BE={root_size_be} bytes')
# File Structure Version (offset 881)
fsv = pvd[881]
print(f'  File Structure Ver:  {fsv} (expect 1)')

# 检查 Volume Space Size 是否和 ISO 实际大小匹配
print('\n=== SeaBIOS CDROM 启动相关字段检查 ===')
# SeaBIOS 实际上检查 PVD 中 Volume Space Size 来确定 ISO 大小
# 如果 Volume Space Size 不等于 ISO 实际大小，SeaBIOS 可能在读超出 PVD 声明大小后报错
expected_vol_size = len(iso) // S
print(f'  PVD 声明的 Volume Space Size: {vol_size_le} sectors')
print(f'  ISO 文件实际扇区数:            {expected_vol_size} sectors')
print(f'  匹配: {vol_size_le == expected_vol_size}')

# SeaBIOS 读取 boot image 时需要的扇区数：
# Boot Image 在 LBA 20 起 720 扇区（1.44MB）
# 但 SeaBIOS 可能读取 2880 个 512 字节扇区 = 1474560 字节
# 在 2048 字节单位下 = ceil(1474560 / 2048) = 720 扇区
# Load RBA=20, 读 720 扇区, 即 LBA 20-739
# SeaBIOS 是否能读 LBA 20-739？如果 Volume Space Size 是 96455，那 LBA 739 << 96455，能读
print(f'  Boot Image: LBA 20-739 (共 720 扇区, 1.44MB)')
print(f'  最大读取 LBA: 739 << Volume Space Size ({vol_size_le})')
print(f'  能否读取: {739 < vol_size_le}')

# 路径表大小
print(f'\n  路径表实际检查:')
if l_pt_l > 0 and l_pt_l < len(iso)//S:
    pt_data = iso[l_pt_l*S:l_pt_l*S + pt_size_le]
    print(f'    L Path Table 实际读取: {len(pt_data)} bytes (期望 {pt_size_le})')
else:
    print(f'    [FAIL] L Path Table LBA {l_pt_l} 超出 ISO 范围')

# 检查 Boot Image 完整范围
print(f'\n=== Boot Image 完整范围检查 ===')
boot_img_start = 20
boot_img_sectors = 720  # 1474560 / 2048
boot_img_end = boot_img_start + boot_img_sectors - 1
print(f'  Boot Image LBA: {boot_img_start} - {boot_img_end} ({boot_img_sectors} sectors)')
print(f'  ISO 总扇区数:   {vol_size_le}')
print(f'  在 ISO 范围内:  {boot_img_end < vol_size_le}')

# 看 Boot Image 最后一个扇区是否有签名或数据
last_sec = iso[boot_img_end*S:boot_img_end*S+512]
print(f'  Boot Image 最后扇区前 16 字节: {last_sec[:16].hex()}')
print(f'  Boot Image 最后扇区是否有非零数据: {any(b != 0 for b in last_sec[:512])}')

# 检查 Boot Catalog 位置
print(f'\n=== Boot Catalog 位置检查 ===')
print(f'  PVD 在 LBA 17, Volume Space Size 字段 = {vol_size_le}')
print(f'  Boot Catalog 在 LBA 19')
print(f'  19 < Volume Space Size ({vol_size_le}): {19 < vol_size_le}')

# 是否有 path table 和 root dir 的位置
print(f'\n=== 其他结构位置 ===')
print(f'  L Path Table: LBA {l_pt_l}')
print(f'  M Path Table: LBA {m_pt_l}')
print(f'  Root Dir: LBA {root_lba_le}, size {root_size_le} bytes')

# 看是否所有结构都不重叠且在 ISO 范围内
print(f'\n=== ISO 总体结构 ===')
print(f'  LBA 16: Boot Record VD')
print(f'  LBA 17: PVD')
print(f'  LBA 18: VD Terminator')
print(f'  LBA 19: Boot Catalog')
print(f'  LBA 20-739: Boot Image (1.44MB)')
print(f'  LBA {l_pt_l}: L Path Table')
print(f'  LBA {m_pt_l}: M Path Table')
print(f'  LBA {root_lba_le}: Root Directory')
