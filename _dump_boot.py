# -*- coding: utf-8 -*-
# dump system.iso 中 boot image 的前 512 字节（引导扇区机器码）
import struct

ISO = r'C:\qemu_iso\system.iso'
# LBA 20 是 boot image 起始（按 build_iso.py 布局：boot catalog 在 19，boot image 在 20）
LBA_BOOT_IMG = 20

with open(ISO, 'rb') as f:
    f.seek(LBA_BOOT_IMG * 2048)
    data = f.read(512)

print('=== system.iso 引导扇区 (LBA 20, offset {}) ==='.format(LBA_BOOT_IMG * 2048))
print('前 64 字节 hex:')
for i in range(0, 64, 16):
    hexs = ' '.join('{:02x}'.format(b) for b in data[i:i+16])
    asci = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in data[i:i+16])
    print(f'{i:04x}: {hexs}  {asci}')

print('\n签名检查:')
print(f'  offset 510: {data[510]:#x} (期望 0x55)')
print(f'  offset 511: {data[511]:#x} (期望 0xaa)')

# 也 dump boot catalog 验证
LBA_BOOT_CAT = 19
with open(ISO, 'rb') as f:
    f.seek(LBA_BOOT_CAT * 2048)
    cat = f.read(64)

print('\n=== Boot Catalog (LBA 19) ===')
print('Validation Entry (32B):')
for i in range(0, 32, 16):
    hexs = ' '.join('{:02x}'.format(b) for b in cat[i:i+16])
    print(f'  {i:04x}: {hexs}')
print('Initial/Default Entry (32B):')
for i in range(32, 64, 16):
    hexs = ' '.join('{:02x}'.format(b) for b in cat[i:i+16])
    print(f'  {i:04x}: {hexs}')

# 解析 initial entry 关键字段
print('\nInitial Entry 关键字段:')
print(f'  Boot indicator: {cat[32]:#x} (0x88=bootable)')
print(f'  Media type:     {cat[33]:#x} (0x02=1.44MB floppy, 0x00=no emulation)')
seg = struct.unpack('<H', cat[34:36])[0]
print(f'  Load segment:   {seg:#x} (0x07c0=default)')
sectors = struct.unpack('<H', cat[38:40])[0]
rba = struct.unpack('<I', cat[40:44])[0]
print(f'  Sector count:   {sectors} (5280=2880*512B)')
print(f'  Load RBA:       {rba} (期望 20=boot image start)')
