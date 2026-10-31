# -*- coding: utf-8 -*-
# 用 pycdlib 解析 test_min.iso 的 PVD，并用 pycdlib 生成一个正确的对照 ISO
import pycdlib
import struct

print("=== 1. 用 pycdlib 解析我的 test_min.iso ===")
iso = pycdlib.PyCdlib()
try:
    iso.open(r'C:\qemu_iso\test_min.iso')
    print("PVD 解析成功")
    # 打印 PVD 关键字段
    print(f"  Volume Identifier: {iso.pvd.volume_identifier.decode('ascii').strip()!r}")
    print(f"  Volume Space Size: {iso.pvd.space_size}")
    print(f"  Logical Block Size: {iso.pvd.logical_block_size}")
    print(f"  Path Table Size: {iso.pvd.path_table_size}")
    print(f"  Volume Set Size: {iso.pvd.volume_set_size}")
    print(f"  Volume Sequence Number: {iso.pvd.sequence_number}")
    print(f"  File Structure Version: {iso.pvd.file_structure_version}")
    print(f"  L Path Table LBA: {iso.pvd.path_table_location}")
    print(f"  M Path Table LBA: {iso.pvd.path_table_location_msb}")
    # 根目录记录
    rde = iso.pvd.root_dir_record
    print(f"  Root Dir Record: extent={rde.extent_location()}, size={rde.data_length}")
    iso.close()
except Exception as e:
    print(f"  解析失败: {e}")

print("\n=== 2. 查看 pycdlib 内部 PVD 字段偏移定义 ===")
# 查看 pycdlib 源码里 PVD 的字段定义
import pycdlib.head as head
print("PVD 字段 (来自 pycdlib.head):")
# 查找 PVD 相关的类
for name in dir(head):
    obj = getattr(head, name)
    if 'Primary' in name or 'PVD' in name or 'pvd' in name.lower():
        print(f"  {name}: {obj}")

# 查找 PVD record 类的字段定义
print("\n搜索 PVD record 类:")
import inspect
for mod_name in ['pycdlib.head', 'pycdlib.pycdlib', 'pycdlib.dr']:
    try:
        mod = __import__(mod_name, fromlist=[''])
        for name in dir(mod):
            obj = getattr(mod, name)
            if inspect.isclass(obj) and ('Primary' in name or 'Pvd' in name or 'pvd' in name.lower()):
                print(f"  {mod_name}.{name}")
                # 打初始化或字段
                src = inspect.getsource(obj)
                # 找 offset 相关的行
                for line in src.split('\n'):
                    if 'offset' in line.lower() or 'fmt' in line.lower() or '_' in line and '=' in line:
                        print(f"    {line.strip()}")
    except Exception as e:
        pass
