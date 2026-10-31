# -*- coding: utf-8 -*-
import struct

ISO = r'C:\qemu_iso\system.iso'
S = 2048
with open(ISO, 'rb') as f:
    iso = f.read()

print('ISO size:', len(iso), '=', len(iso)//S, 'sectors')

# 扫描 Volume Descriptor 区域 (LBA 16 起，直到 terminator)
print('\n=== Volume Descriptor 扫描 ===')
for lba in range(16, 20):
    vd = iso[lba*S:(lba+1)*S]
    t = vd[0]
    stdid = vd[1:6]
    ver = vd[6]
    if t == 0xFF:
        print(f'  LBA {lba}: VD Terminator (type 0xFF)')
        break
    elif t == 0x00:
        cats_lba = struct.unpack('<I', vd[71:75])[0]
        bootsys = vd[7:39].rstrip(b'\x00').decode('ascii', 'replace')
        print(f'  LBA {lba}: Boot Record (type 0x00), stdid={stdid}, BootSys="{bootsys}", BootCatLBA={cats_lba}')
    elif t == 0x01:
        volid = vd[40:72].rstrip(b' ').decode('ascii', 'replace')
        print(f'  LBA {lba}: PVD (type 0x01), stdid={stdid}, VolID="{volid}"')
    else:
        print(f'  LBA {lba}: Unknown type 0x{t:02X}')

# 看 LBA 16 的 Boot Record VD 完整内容
print('\n=== LBA 16 Boot Record VD 前 80 字节 ===')
brvd = iso[16*S:16*S+80]
print('hex:', brvd.hex())
print('  Type:', hex(brvd[0]))
print('  StdId:', brvd[1:6])
print('  Ver:', brvd[6])
print('  BootSysId:', brvd[7:39].rstrip(b'\x00'))
print('  BootId:', brvd[39:71])
print('  BootCatLBA bytes 71-74:', brvd[71:75].hex(), '=', struct.unpack('<I', brvd[71:75])[0])

# Boot Catalog (LBA 19)
print('\n=== Boot Catalog LBA 19 ===')
bc = iso[19*S:20*S]
print('Validation Entry hex:', bc[:32].hex())
print('  Header ID:', hex(bc[0]), '(expect 0x01)')
print('  Platform ID:', hex(bc[1]), '(expect 0x00)')
print('  Key:', hex(bc[30]), hex(bc[31]), '(expect 0x55 0xAA)')

# Initial Entry
print('Initial Entry hex:', bc[32:64].hex())
print('  Boot Indicator:', hex(bc[32]), '(expect 0x88)')
print('  Media Type:', hex(bc[33]), '(expect 0x02)')
print('  Load Segment:', hex(struct.unpack('<H', bc[34:36])[0]))
print('  Sector Count:', struct.unpack('<H', bc[38:40])[0])
print('  Load RBA:', struct.unpack('<I', bc[40:44])[0])

# Boot Image LBA 20 前 32 字节
print('\n=== Boot Image LBA 20 ===')
bi = iso[20*S:20*S+32]
print('first 32 bytes:', bi.hex())
print('boot sector sig (510-511):', hex(iso[20*S+510]), hex(iso[20*S+511]))

# 也检查 boot.img 整体大小
boot_img = iso[20*S:20*S+1474560]
print(f'Boot Image total: {len(boot_img)} bytes (= {len(boot_img)/1024}KB = {len(boot_img)/1024/1024}MB)')
# 检查每个 512 字节扇区的最后两字节是否都是 55AA (软盘镜像的特点)
print('Boot Image signature check (every 512 bytes):')
sig_ok = 0
sig_fail = 0
for i in range(0, 1474560, 512):
    if boot_img[i+510] == 0x55 and boot_img[i+511] == 0xAA:
        sig_ok += 1
    else:
        sig_fail += 1
print(f'  55 AA 签名 OK: {sig_ok}, FAIL: {sig_fail}')
# 软盘镜像只有引导扇区(第0扇区)有签名，其他扇区不应该有签名
# 但实际数据可能某些扇区也有 55AA 巧合
print(f'  期望：只有第 0 个扇区有签名，其他 2879 扇区无')
