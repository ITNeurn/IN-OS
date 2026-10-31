# 检查 installer.bin 的内容和大小
import os

path = r'c:\Users\PC\Desktop\根目录\installer.bin'
with open(path, 'rb') as f:
    data = f.read()

print(f'installer.bin 大小: {len(data)} 字节')

# 搜索 "Micro-Kernel" 字符串
target = b'Micro-Kernel'
idx = data.find(target)
if idx >= 0:
    print(f'找到 "Micro-Kernel" 在偏移 {idx} (0x{idx:x})')
    print(f'上下文: {data[idx:idx+40]}')
else:
    print('未找到 "Micro-Kernel" — 字符串可能被截断!')

# 搜索所有字符串
for s in [b'Micro-Kernel', b'Reading', b'Writing', b'Installation', b'Press', b'ERROR']:
    idx = data.find(s)
    print(f'  "{s.decode()}" at offset {idx}' if idx >= 0 else f'  "{s.decode()}" NOT FOUND')

# 显示最后 100 字节
print(f'\n最后 100 字节:')
start = max(0, len(data) - 100)
for i in range(start, len(data), 16):
    hexs = ' '.join(f'{b:02x}' for b in data[i:i+16])
    asci = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in data[i:i+16])
    print(f'{i:04x}: {hexs}  {asci}')

# 检查 0x100 偏移处（如果 msg_welcome 在这里）
print(f'\n偏移 0x100 处的内容:')
if len(data) > 0x100:
    for i in range(0x100, min(len(data), 0x160), 16):
        hexs = ' '.join(f'{b:02x}' for b in data[i:i+16])
        asci = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in data[i:i+16])
        print(f'{i:04x}: {hexs}  {asci}')
