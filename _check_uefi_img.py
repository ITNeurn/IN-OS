import struct

img = open(r'c:\Users\PC\Desktop\根目录\uefi_boot.img', 'rb').read()
print('image size', len(img))

# --- LBA0 MBR / 分区表 ---
mbr = img[0:512]
assert mbr[510:512] == b'\x55\xAA', 'MBR sig'
boot, sh, sst, ptype = mbr[446], mbr[447], mbr[448], mbr[450]
lba_start = struct.unpack_from('<I', mbr, 454)[0]
lba_count = struct.unpack_from('<I', mbr, 458)[0]
print(f'part1: boot=0x{boot:02X} type=0x{ptype:02X} start={lba_start} sectors={lba_count}')
assert boot == 0x80 and ptype == 0xEF
assert lba_start == 2048 and lba_count == 131072
assert mbr[462:510] == b'\x00' * 48, 'part2-4 must be zero'

# --- LBA2048 FAT32 VBR ---
v = lba_start * 512
vbr = img[v:v + 512]
assert vbr[510:512] == b'\x55\xAA', 'VBR sig'
print('VBR OEM:', vbr[3:11], 'bytesPerSec:', struct.unpack_from('<H', vbr, 11)[0])
assert vbr[3:11].startswith(b'MSDOS') or vbr[11:13] == b'\x00\x02'

# --- 卷内定位文件: 根目录簇(2) -> 找 EFI 目录 -> BOOT 目录 -> BOOTX64.EFI ---
spc = vbr[13]
reserved = struct.unpack_from('<H', vbr, 14)[0]
num_fats = vbr[16]
spf = struct.unpack_from('<I', vbr, 36)[0]
data_start = (reserved + num_fats * spf) * 512
root_dir = img[v + data_start:v + data_start + spc * 512]

def find_entry(dirtab, name11):
    for i in range(0, len(dirtab), 32):
        e = dirtab[i:i + 32]
        if e[0] == 0:
            break
        if e[0:11] == name11:
            cl = struct.unpack_from('<H', e, 20)[0] << 16 | struct.unpack_from('<H', e, 26)[0]
            sz = struct.unpack_from('<I', e, 28)[0]
            return cl, sz, e[11]
    return None

def s11(name, ext):
    return (name.ljust(8) + ext.ljust(3)).encode()

r = find_entry(root_dir, s11('KERNEL', 'BIN'))
print('KERNEL.BIN:', r)
assert r and r[1] == 32153
r = find_entry(root_dir, s11('EFI', ''))
print('EFI dir:', r)
efi_cl = r[0]
efi_dir = img[v + data_start + (efi_cl - 2) * spc * 512:][:spc * 512]
r = find_entry(efi_dir, s11('BOOT', ''))
print('BOOT dir:', r)
boot_cl = r[0]
boot_dir = img[v + data_start + (boot_cl - 2) * spc * 512:][:spc * 512]
r = find_entry(boot_dir, s11('BOOTX64', 'EFI'))
print('BOOTX64.EFI:', r)
efi_file_cl, efi_size = r[0], r[1]
off = v + data_start + (efi_file_cl - 2) * spc * 512
efi_on_disk = img[off:off + efi_size]
local = open(r'c:\Users\PC\Desktop\根目录\BOOTX64.EFI', 'rb').read()
assert efi_on_disk == local, 'EFI content mismatch'
print('BOOTX64.EFI on volume matches local file, size', efi_size)

# KERNEL.BIN 头 512 应为 0 (-Ttext 0x10200 padding)
k_cl, k_sz, _ = find_entry(root_dir, s11('KERNEL', 'BIN'))
k_off = v + data_start + (k_cl - 2) * spc * 512
assert img[k_off:k_off + 512] == b'\x00' * 512, 'kernel first 512 must be zero'
print('KERNEL.BIN head 512 zero-padded OK, entry will be at phys 0x10200')
print('ALL CHECKS PASSED')
