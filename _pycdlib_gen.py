# -*- coding: utf-8 -*-
# 用 pycdlib 生成一个正确的最小 El Torito 可引导 ISO，然后测试 QEMU 引导
import pycdlib
import os, struct

BOOT_IMG = r'C:\qemu_iso\boot_pycdlib.img'
ISO_OUT = r'C:\qemu_iso\test_pycdlib.iso'

# 1. 创建引导扇区 (512 字节) - 直接写 VGA 显存
bs = bytearray(512)
# mov ax, 0xB800; mov es, ax; xor di,di; mov ax,0x0741; stosw; hlt; jmp -2
bs[0] = 0xB8; bs[1] = 0x00; bs[2] = 0xB8    # mov ax, 0xB800
bs[3] = 0x8E; bs[4] = 0xC0                   # mov es, ax
bs[5] = 0x31; bs[6] = 0xFF                   # xor di, di
# 写 "PY" 两个字符: P=0x50 Y=0x59, attr=0x07
bs[7] = 0x66; bs[8] = 0xB8; bs[9] = 0x50; bs[10] = 0x07  # mov ax, 0x0750
bs[11] = 0xAB                                # stosw
bs[12] = 0x66; bs[13] = 0xB8; bs[14] = 0x59; bs[15] = 0x07  # mov ax, 0x0759
bs[16] = 0xAB                                # stosw
bs[17] = 0xF4                                # hlt
bs[18] = 0xEB; bs[19] = 0xFE                 # jmp -2
bs[510] = 0x55; bs[511] = 0xAA

with open(BOOT_IMG, 'wb') as f:
    f.write(bs)
print(f"引导扇区已写入: {BOOT_IMG}")

# 2. 用 pycdlib 生成 ISO
iso = pycdlib.PyCdlib()
iso.new(interchange_level=1, sys_ident='PYCDLIB', vol_ident='TEST')

# 先把 boot 文件添加到 ISO
iso.add_file(BOOT_IMG, '/BOOT.;1')
# 再添加 El Torito (media_name='noemul', 4 个 512 字节扇区)
iso.add_eltorito('/BOOT.;1', media_name='noemul', boot_load_seg=0x07c0, boot_load_size=4)
iso.write(ISO_OUT)
iso.close()
print(f"ISO 已生成: {ISO_OUT}")
print(f"ISO 大小: {os.path.getsize(ISO_OUT)} bytes")

# 3. 用 pycdlib 重新打开验证
iso2 = pycdlib.PyCdlib()
iso2.open(ISO_OUT)
print("\n=== pycdlib 验证 ===")
print(f"  Volume Space Size: {iso2.pvd.space_size}")
print(f"  Logical Block Size: {iso2.pvd.log_block_size}")
print(f"  Path Table Size: {iso2.pvd.path_tbl_size}")
print(f"  Volume Set Size: {iso2.pvd.set_size}")
print(f"  Sequence Number: {iso2.pvd.seqnum}")
print(f"  File Structure Version: {iso2.pvd.file_structure_version}")
print(f"  L Path Table LBA: {iso2.pvd.path_table_location_le}")
print(f"  M Path Table LBA: {iso2.pvd.path_table_location_be}")
rde = iso2.pvd.root_dir_record
print(f"  Root Dir: extent={rde.extent_location()}, size={rde.data_length}")
# El Torito 信息
if hasattr(iso2, 'eltorito_boot_info'):
    print(f"  El Torito: {iso2.eltorito_boot_info}")
iso2.close()
