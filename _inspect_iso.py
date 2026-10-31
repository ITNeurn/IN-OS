# -*- coding: utf-8 -*-
# 检查 test_min.iso 的关键扇区字节内容
import struct

ISO = r'C:\qemu_iso\test_min.iso'
S = 2048

with open(ISO, 'rb') as f:
    data = f.read()

def dump(label, offset, n):
    print(f'\n=== {label} (offset {offset} = LBA {offset//S}) ===')
    chunk = data[offset:offset+n]
    for i in range(0, len(chunk), 16):
        hex_part = ' '.join(f'{b:02x}' for b in chunk[i:i+16])
        ascii_part = ''.join(chr(b) if 0x20<=b<0x7f else '.' for b in chunk[i:i+16])
        print(f'{offset+i:06x}: {hex_part:<48} {ascii_part}')

# LBA 16: BRVD
dump('Boot Record VD (LBA 16)', 16*S, 80)
# LBA 17: PVD (前 40 字节 + 关键字段)
dump('PVD header (LBA 17)', 17*S, 40)
print(f'\nPVD Volume Space Size (offset 80): {struct.unpack_from("<I", data, 17*S+80)[0]} LE / {struct.unpack_from(">I", data, 17*S+84)[0]} BE')
# LBA 19: Boot Catalog (Validation + Initial Entry)
dump('Boot Catalog Validation + Initial Entry (LBA 19)', 19*S, 64)
# LBA 20: Boot Image (前 32 字节)
dump('Boot Image (LBA 20, first 32 bytes)', 20*S, 32)

# 解析 Validation Entry
v = 19*S
print(f'\n=== Validation Entry 解析 ===')
print(f'Header ID: 0x{data[v]:02x} (expect 0x01)')
print(f'Platform ID: 0x{data[v+1]:02x} (expect 0x00)')
print(f'Manufacturer: {data[v+4:v+28]!r}')
print(f'Checksum: 0x{data[v+29]:02x}{data[v+28]:02x} (LE)')
print(f'Key: 0x{data[v+30]:02x} 0x{data[v+31]:02x} (expect 0x55 0xAA)')
# verify checksum
s = 0
for i in range(0, 32, 2):
    s = (s + (data[v+i] | (data[v+i+1]<<8))) & 0xFFFF
print(f'Checksum verify: 0x{s:04x} (expect 0x0000)')

# 解析 Initial Entry
ie = 19*S + 32
print(f'\n=== Initial Entry 解析 ===')
print(f'Boot Indicator: 0x{data[ie]:02x} (expect 0x88)')
print(f'Boot Media Type: 0x{data[ie+1]:02x} (0=no-emul)')
print(f'Load Segment: 0x{struct.unpack_from("<H", data, ie+2)[0]:04x} (0=default 0x07C0)')
print(f'System Type: 0x{data[ie+4]:02x}')
print(f'Sector Count: {struct.unpack_from("<H", data, ie+6)[0]} (512-byte units)')
print(f'Load RBA: {struct.unpack_from("<I", data, ie+8)[0]} (2048-byte sectors)')

# 解析 BRVD Boot Catalog LBA
brvd = 16*S
print(f'\n=== BRVD 解析 ===')
print(f'Type: 0x{data[brvd]:02x} (expect 0x00)')
print(f'Std ID: {data[brvd+1:brvd+6]!r} (expect b CD001)')
print(f'Version: {data[brvd+6]} (expect 1)')
print(f'Boot Sys ID: {data[brvd+7:brvd+39]!r}')
print(f'Boot Catalog LBA (offset 71): {struct.unpack_from("<I", data, brvd+71)[0]} (expect 19)')
print(f'Boot Catalog LBA (offset 72, wrong): {struct.unpack_from("<I", data, brvd+72)[0]} (would be 0 if misaligned)')

# 文件总大小
print(f'\nISO size: {len(data)} bytes = {len(data)//S} sectors')
print(f'LBA 20 offset: {20*S} (within file: {20*S < len(data)})')
print(f'Boot image content at LBA 20: {data[20*S:20*S+18].hex()}')
