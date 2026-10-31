# -*- coding: utf-8 -*-
# 找 pycdlib 里 PVD 字段偏移定义
import pycdlib, os, inspect

print("pycdlib path:", os.path.dirname(pycdlib.__file__))
print("\npycdlib 模块文件:")
for f in sorted(os.listdir(os.path.dirname(pycdlib.__file__))):
    if f.endswith('.py'):
        print(f"  {f}")

# 找 PVD record 类
print("\n搜索 PVD 类定义:")
import importlib
pkg_path = os.path.dirname(pycdlib.__file__)
for fname in sorted(os.listdir(pkg_path)):
    if not fname.endswith('.py') or fname.startswith('__'):
        continue
    mod_name = f'pycdlib.{fname[:-3]}'
    try:
        mod = importlib.import_module(mod_name)
    except Exception:
        continue
    for name in dir(mod):
        obj = getattr(mod, name)
        if inspect.isclass(obj) and obj.__module__ == mod_name:
            if any(k in name for k in ['Primary', 'Pvd', 'PVD', 'VolumeDescriptor', 'BootRecord']):
                print(f"\n=== {mod_name}.{name} ===")
                src = inspect.getsource(obj)
                # 打印包含 offset/fmt/struct 的行
                for i, line in enumerate(src.split('\n')):
                    if any(k in line.lower() for k in ['offset', 'fmt', '_fmt', 'struct', 'space_size', 'path_table', 'sequence', 'set_size', 'block_size', 'file_structure', 'root_dir']):
                        print(f"  {line.rstrip()}")
