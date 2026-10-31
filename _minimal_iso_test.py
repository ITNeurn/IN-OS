# -*- coding: utf-8 -*-
# 最小 El Torito ISO 测试 - 修正所有 PVD/结构问题
# 对齐 pycdlib 布局: PVD 先于 BRVD, 有 path table 和 root dir
import struct, os

ISO = r'C:\qemu_iso\test_min.iso'
S = 2048

# 引导扇区 (512字节) - 直接写 VGA 显存打印 'A'
bs = bytearray(512)
bs[0] = 0xB8; bs[1] = 0x00; bs[2] = 0xB8    # mov ax, 0xB800
bs[3] = 0x8E; bs[4] = 0xC0                  # mov es, ax
bs[5] = 0x31; bs[6] = 0xFF                  # xor di, di
bs[7] = 0xB8; bs[8] = 0x41; bs[9] = 0x07    # mov ax, 0x0741 ('A'+attr7)
bs[10] = 0xAB                               # stosw
bs[11] = 0xF4                               # hlt
bs[12] = 0xEB; bs[13] = 0xFE                # jmp -2
bs[510] = 0x55; bs[511] = 0xAA
boot_img = bytes(bs) + b'\x00' * (S - 512)

# ---- ISO 布局 ----
# LBA 0-15:  System Area
# LBA 16:    PVD
# LBA 17:    BRVD
# LBA 18:    Terminator
# LBA 19:    L Path Table
# LBA 20:    M Path Table
# LBA 21:    Root Directory
# LBA 22:    Boot Catalog
# LBA 23:    Boot Image
LBA_PVD = 16
LBA_BRVD = 17
LBA_TERM = 18
LBA_L_PT = 19
LBA_M_PT = 20
LBA_ROOT = 21
LBA_BOOTCAT = 22
LBA_BOOTIMG = 23
TOTAL = 24  # 至少 24 扇区

# ---- PVD ----
pvd = bytearray(S)
pvd[0] = 0x01                       # Type 1 PVD
pvd[1:6] = b'CD001'
pvd[6] = 0x01                       # Version
pvd[7] = 0x00                       # Unused
pvd[8:40] = b'MINIMAL'.ljust(32, b' ')   # System Identifier
pvd[40:72] = b'TEST'.ljust(32, b' ')    # Volume Identifier
# offset 72-79: Unused (Q = 8 bytes)
# offset 80-83: Volume Space Size LE
struct.pack_into('<I', pvd, 80, TOTAL)
# offset 84-87: Volume Space Size BE
struct.pack_into('>I', pvd, 84, TOTAL)
# offset 88-119: Escape Sequences (32 bytes, 0 for PVD)
# offset 120-121: Volume Set Size LE
struct.pack_into('<H', pvd, 120, 1)
# offset 122-123: Volume Set Size BE
struct.pack_into('>H', pvd, 122, 1)
# offset 124-125: Volume Sequence Number LE
struct.pack_into('<H', pvd, 124, 1)
# offset 126-127: Volume Sequence Number BE
struct.pack_into('>H', pvd, 126, 1)
# offset 128-129: Logical Block Size LE  ← 关键！之前为 0
struct.pack_into('<H', pvd, 128, 2048)
# offset 130-131: Logical Block Size BE
struct.pack_into('>H', pvd, 130, 2048)
# offset 132-135: Path Table Size LE
struct.pack_into('<I', pvd, 132, 10)
# offset 136-139: Path Table Size BE
struct.pack_into('>I', pvd, 136, 10)
# offset 140-143: L Path Table LBA
struct.pack_into('<I', pvd, 140, LBA_L_PT)
# offset 144-147: Optional L Path Table LBA
struct.pack_into('<I', pvd, 144, 0)
# offset 148-151: M Path Table LBA
struct.pack_into('>I', pvd, 148, LBA_M_PT)
# offset 152-155: Optional M Path Table LBA
struct.pack_into('>I', pvd, 152, 0)
# offset 156-189: Root Directory Record (34 bytes)
root = bytearray(34)
root[0] = 34                                   # Record length
root[1] = 0                                    # Ext attr length
struct.pack_into('<I', root, 2, LBA_ROOT)     # LBA LE
struct.pack_into('>I', root, 6, LBA_ROOT)     # LBA BE
struct.pack_into('<I', root, 10, S)           # Data length LE
struct.pack_into('>I', root, 14, S)           # Data length BE
root[18] = 0x02                                # Flags: directory
root[27] = 1                                   # File unit size
root[28] = 0                                   # Interleave gap
struct.pack_into('<H', root, 29, 1)           # Volume seq num LE
struct.pack_into('>H', root, 31, 1)           # Volume seq num BE
root[33] = 1                                   # File identifier length
# root[32] = 0x00  # File identifier (00 = root, already 0)
pvd[156:190] = root
# offset 190-317: Volume Set Identifier (128 bytes, space)
pvd[190:318] = b' ' * 128
# offset 318-445: Publisher Identifier (128 bytes)
pvd[318:446] = b' ' * 128
# offset 446-573: Data Preparer Identifier (128 bytes)
pvd[446:574] = b' ' * 128
# offset 574-701: Application Identifier (128 bytes)
pvd[574:702] = b' ' * 128
# offset 702-738: Copyright File Identifier (37 bytes)
pvd[702:739] = b' ' * 37
# offset 739-775: Abstract File Identifier (37 bytes)
pvd[739:776] = b' ' * 37
# offset 776-812: Bibliographic File Identifier (37 bytes)
pvd[776:813] = b' ' * 37
# offset 813-829: Volume Creation Date (17 bytes, '0'*17 for "now")
pvd[813:830] = b'0' * 16 + b'\x00'
# offset 830-846: Volume Modification Date
pvd[830:847] = b'0' * 16 + b'\x00'
# offset 847-863: Volume Expiration Date
pvd[847:864] = b'0' * 16 + b'\x00'
# offset 864-880: Volume Effective Date
pvd[864:881] = b'0' * 16 + b'\x00'
# offset 881: File Structure Version
pvd[881] = 0x01

# ---- BRVD (Boot Record Volume Descriptor) ----
brvd = bytearray(S)
brvd[0] = 0x00                       # Type 0 Boot Record
brvd[1:6] = b'CD001'
brvd[6] = 0x01                       # Version
brvd[7:39] = b'EL TORITO SPECIFICATION'.ljust(32, b'\x00')
struct.pack_into('<I', brvd, 71, LBA_BOOTCAT)  # Boot Catalog LBA

# ---- Terminator ----
term = bytearray(S)
term[0] = 0xFF
term[1:6] = b'CD001'
term[6] = 0x01

# ---- L Path Table ----
# 最小 path table: 1 个 entry (根目录)
l_pt = bytearray(S)
l_pt[0] = 1                                    # Directory identifier length
l_pt[1] = 0                                    # Extended attribute record length
struct.pack_into('<I', l_pt, 2, LBA_ROOT)     # Location of extent (LE)
struct.pack_into('<H', l_pt, 6, 1)            # Parent number (LE)
l_pt[8] = 0x00                                 # Directory identifier (root = 0x00)
# padding to even (9 bytes -> pad to 10)
l_pt[9] = 0x00

# ---- M Path Table ----
m_pt = bytearray(S)
m_pt[0] = 1
m_pt[1] = 0
struct.pack_into('>I', m_pt, 2, LBA_ROOT)     # Location (BE)
struct.pack_into('>H', m_pt, 6, 1)            # Parent (BE)
m_pt[8] = 0x00
m_pt[9] = 0x00

# ---- Root Directory ----
rootdir = bytearray(S)
# "." entry (self)
dot = bytearray(34)
dot[0] = 34
dot[1] = 0
struct.pack_into('<I', dot, 2, LBA_ROOT)
struct.pack_into('>I', dot, 6, LBA_ROOT)
struct.pack_into('<I', dot, 10, S)
struct.pack_into('>I', dot, 14, S)
dot[18] = 0x02                                # directory flag
struct.pack_into('<H', dot, 29, 1)
struct.pack_into('>H', dot, 31, 1)
dot[33] = 1
dot[32] = 0x00                                # file id "." = 0x00
rootdir[0:34] = dot
# ".." entry (parent)
dotdot = bytearray(34)
dotdot[0] = 34
dotdot[1] = 0
struct.pack_into('<I', dotdot, 2, LBA_ROOT)
struct.pack_into('>I', dotdot, 6, LBA_ROOT)
struct.pack_into('<I', dotdot, 10, S)
struct.pack_into('>I', dotdot, 14, S)
dotdot[18] = 0x02
struct.pack_into('<H', dotdot, 29, 1)
struct.pack_into('>H', dotdot, 31, 1)
dotdot[33] = 1
dotdot[32] = 0x01                             # file id ".." = 0x01
rootdir[34:68] = dotdot

# ---- Boot Catalog ----
valid = bytearray(32)
valid[0] = 0x01
valid[1] = 0x00
valid[4:28] = b'MINIMAL TEST BOOT'.ljust(24, b' ')
valid[30] = 0x55; valid[31] = 0xAA
valid[28] = 0; valid[29] = 0
chk = 0
for i in range(0, 32, 2):
    chk = (chk + (valid[i] | (valid[i+1] << 8))) & 0xFFFF
chk = (0x10000 - chk) & 0xFFFF
valid[28] = chk & 0xFF
valid[29] = (chk >> 8) & 0xFF

init = bytearray(32)
init[0] = 0x88                    # Bootable
init[1] = 0x00                    # No emulation
struct.pack_into('<H', init, 2, 0x07C0)  # Load segment
init[4] = 0x00
init[5] = 0x00
struct.pack_into('<H', init, 6, 4)      # Sector count (4 * 512 = 2048)
struct.pack_into('<I', init, 8, LBA_BOOTIMG)  # Load RBA

bootcat = bytes(valid) + bytes(init) + b'\x00' * (S - 64)

# ---- 拼装 ISO ----
iso = bytearray()
iso += b'\x00' * (16 * S)          # LBA 0-15: System Area
iso += pvd                        # LBA 16
iso += brvd                       # LBA 17
iso += term                       # LBA 18
iso += l_pt                       # LBA 19
iso += m_pt                       # LBA 20
iso += rootdir                    # LBA 21
iso += bootcat                    # LBA 22
iso += boot_img                   # LBA 23
while len(iso) < TOTAL * S:
    iso += b'\x00' * S

with open(ISO, 'wb') as f:
    f.write(iso)
print(f'ISO written: {ISO}')
print(f'size: {len(iso)} bytes = {len(iso)//S} sectors')
