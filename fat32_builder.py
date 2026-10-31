# fat32_builder.py - 构建可启动 FAT32 镜像 + ISO 模式支持
# 生成一个 FAT32 镜像，内含自定义引导扇区、内核、动画帧、EFI
# 同时生成支持 DD 模式和 ISO 模式的 ISO
import struct
import os
import shutil

ROOT = r'C:\Users\PC\Desktop\根目录'
IMG_SIZE = 32 * 1024 * 1024  # 32MB FAT32 镜像
SECTOR = 512
SPT = 63          # 每磁道扇区
HEADS = 255       # 磁头数

# ===== FAT32 BPB 参数 =====
BYTES_PER_SECTOR = 512
SECTORS_PER_CLUSTER = 8   # 4KB 簇
RESERVED_SECTORS = 32
NUM_FATS = 2
MEDIA = 0xF8

total_sectors = IMG_SIZE // SECTOR
# FAT 大小计算: 簇数 * 4 字节 / 512
data_sectors = total_sectors - RESERVED_SECTORS
clusters = data_sectors // (SECTORS_PER_CLUSTER + NUM_FATS // SECTORS_PER_CLUSTER)
# 简化: 直接计算
sectors_per_fat = (clusters * 4 + 511) // 512
# 重新精确计算
fat_sectors_total = NUM_FATS * sectors_per_fat
data_start = RESERVED_SECTORS + fat_sectors_total
data_sectors = total_sectors - data_start
clusters = data_sectors // SECTORS_PER_CLUSTER
sectors_per_fat = (clusters * 4 + 511) // 512
fat_sectors_total = NUM_FATS * sectors_per_fat
data_start = RESERVED_SECTORS + fat_sectors_total
data_sectors = total_sectors - data_start
clusters = data_sectors // SECTORS_PER_CLUSTER

print(f'FAT32 参数:')
print(f'  总扇区: {total_sectors}')
print(f'  保留扇区: {RESERVED_SECTORS}')
print(f'  FAT 数: {NUM_FATS}, 每 FAT 扇区: {sectors_per_fat}')
print(f'  数据区起始扇区: {data_start}')
print(f'  簇数: {clusters}')

def build_bpb():
    """构建 FAT32 BPB + 引导扇区前 90 字节"""
    bpb = bytearray(512)
    # 跳转指令
    bpb[0:3] = b'\xEB\x58\x90'  # jmp 0x5A; nop
    # OEM 名
    bpb[3:11] = b'MKFSFAT32'
    # BPB
    struct.pack_into('<H', bpb, 11, BYTES_PER_SECTOR)   # bytes per sector
    bpb[13] = SECTORS_PER_CLUSTER                         # sectors per cluster
    struct.pack_into('<H', bpb, 14, RESERVED_SECTORS)     # reserved sectors
    bpb[16] = NUM_FATS                                    # num FATs
    struct.pack_into('<H', bpb, 17, 0)                    # root entries (FAT32=0)
    struct.pack_into('<H', bpb, 19, 0)                    # total sectors 16 (FAT32=0)
    bpb[21] = MEDIA                                       # media
    struct.pack_into('<H', bpb, 22, 0)                    # sectors per FAT 16
    struct.pack_into('<H', bpb, 24, SPT)                  # sectors per track
    struct.pack_into('<H', bpb, 26, HEADS)                # heads
    struct.pack_into('<I', bpb, 28, 0)                    # hidden sectors
    struct.pack_into('<I', bpb, 32, total_sectors)        # total sectors 32
    struct.pack_into('<I', bpb, 36, sectors_per_fat)      # sectors per FAT 32
    struct.pack_into('<H', bpb, 40, 0)                    # ext flags
    struct.pack_into('<H', bpb, 42, 0)                    # FS version
    struct.pack_into('<I', bpb, 44, 2)                    # root cluster
    struct.pack_into('<H', bpb, 48, 1)                    # FSInfo sector
    struct.pack_into('<H', bpb, 50, 6)                    # backup boot sector
    bpb[52:64] = b'\x00' * 12                             # reserved
    bpb[64] = 0x80                                        # drive number
    bpb[65] = 0                                           # reserved
    bpb[66] = 0x29                                        # boot sig
    struct.pack_into('<I', bpb, 67, 0x12345678)           # volume ID
    bpb[71:82] = b'BOOT       '                          # volume label
    bpb[82:90] = b'FAT32   '                            # FS type
    return bytes(bpb)

def cluster_to_sector(cluster):
    return data_start + (cluster - 2) * SECTORS_PER_CLUSTER

def build_fat(fat_chain, num_clusters):
    """构建 FAT 表"""
    fat = bytearray(sectors_per_fat * SECTOR)
    # FAT[0] = media byte, FAT[1] = 0x0FFFFFFF
    struct.pack_into('<I', fat, 0, 0x0FFFFFF8)  # FAT[0]
    struct.pack_into('<I', fat, 4, 0x0FFFFFFF)  # FAT[1]
    # 根目录簇 (cluster 2) 设为 EOC
    struct.pack_into('<I', fat, 8, 0x0FFFFFFF)
    # 写入文件簇链
    for i, next_cluster in enumerate(fat_chain):
        if next_cluster == 0xFFFFFFFF:
            struct.pack_into('<I', fat, (i + 3) * 4, 0x0FFFFFFF)
        else:
            struct.pack_into('<I', fat, (i + 3) * 4, next_cluster)
    return bytes(fat)

def make_dir_entry(name, ext, attr, first_cluster, size):
    """创建 8.3 目录项 (32 字节)"""
    entry = bytearray(32)
    # 文件名 (8 字节, 空格填充)
    name_bytes = name.encode('ascii').upper()
    name_bytes = name_bytes[:8].ljust(8, b' ')
    entry[0:8] = name_bytes
    # 扩展名 (3 字节)
    ext_bytes = ext.encode('ascii').upper()
    ext_bytes = ext_bytes[:3].ljust(3, b' ')
    entry[8:11] = ext_bytes
    entry[11] = attr  # 属性
    entry[12] = 0     # 保留
    entry[13] = 0     # 创建时间 10ms
    entry[14:16] = b'\x00\x00'  # 创建时间
    entry[16:18] = b'\x00\x00'  # 创建日期
    entry[18:20] = b'\x00\x00'  # 访问日期
    entry[20:22] = struct.pack('<H', (first_cluster >> 16) & 0xFFFF)  # 簇高 16 位
    entry[22:24] = b'\x00\x00'  # 修改时间
    entry[24:26] = b'\x00\x00'  # 修改日期
    entry[26:28] = struct.pack('<H', first_cluster & 0xFFFF)  # 簇低 16 位
    struct.pack_into('<I', entry, 28, size)
    return bytes(entry)

def write_file_to_image(img, data, start_cluster):
    """将文件数据写入指定簇开始的簇链"""
    cluster = start_cluster
    offset = 0
    while offset < len(data):
        sector = cluster_to_sector(cluster)
        chunk = data[offset:offset + SECTORS_PER_CLUSTER * SECTOR]
        chunk = chunk + b'\x00' * (SECTORS_PER_CLUSTER * SECTOR - len(chunk))
        img[sector * SECTOR:(sector + SECTORS_PER_CLUSTER) * SECTOR] = chunk
        offset += SECTORS_PER_CLUSTER * SECTOR
        cluster += 1

if __name__ == '__main__':
    print('FAT32 builder module loaded')
    print(f'  total_sectors={total_sectors}')
    print(f'  sectors_per_fat={sectors_per_fat}')
    print(f'  data_start={data_start}')
    print(f'  clusters={clusters}')
