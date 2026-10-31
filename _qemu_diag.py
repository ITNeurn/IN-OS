# _qemu_diag.py - 诊断 QEMU 引导 ISO 的情况
import subprocess
import sys
import os
import time
import threading

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\system.iso'
VRAM_DUMP = r'C:\qemu_iso\vram_kernel.bin'

print(f'ISO: {ISO} ({os.path.getsize(ISO)} 字节)')
print(f'QEMU: {QEMU}')

cmd = [
    QEMU,
    '-drive', f'file={ISO},format=raw,if=ide,index=0,media=cdrom',
    '-m', '256',
    '-boot', 'd',           # 强制从光盘启动
    '-display', 'none',
    '-monitor', 'stdio',
    '-serial', 'null',
    '-no-reboot',
    '-no-shutdown',
]

print(f'启动 QEMU...')

proc = subprocess.Popen(
    cmd,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=False,  # 用字节模式
    bufsize=0,
)

# 收集输出
output_data = bytearray()
def reader():
    while True:
        try:
            chunk = proc.stdout.read(1)
            if not chunk:
                break
            output_data.extend(chunk)
        except Exception:
            break

t = threading.Thread(target=reader, daemon=True)
t.start()

# 等待 5 秒让 QEMU 启动
time.sleep(5)

# 发送 monitor 命令：用 memsave 但是以 QEMU 期望的格式
# QEMU monitor: memsave addr size file
# 我们的地址是 0xb8000 (VGA 显存)
# 大小是 4000 (80*25*2)
# 注意：在 QEMU monitor 中，地址是十进制或 0x 前缀的十六进制
cmd_text = b'memsave 0xb8000 4000 ' + VRAM_DUMP.encode('ascii').replace(b'\\', b'/') + b'\n'
print(f'发送 monitor: {cmd_text.strip()}')
proc.stdin.write(cmd_text)
proc.stdin.flush()
time.sleep(2)

# 再发送 info 命令查看状态
proc.stdin.write(b'info registers\n')
proc.stdin.flush()
time.sleep(1)

# 退出
proc.stdin.write(b'quit\n')
proc.stdin.flush()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print()
print('=== QEMU 输出 (前 2000 字节) ===')
try:
    print(output_data[:2000].decode('utf-8', errors='replace'))
except Exception as e:
    print(f'解码错误: {e}')
    print(repr(output_data[:2000]))

print()
print('=== 检查 VRAM dump 文件 ===')
if os.path.exists(VRAM_DUMP):
    print(f'文件存在: {VRAM_DUMP} ({os.path.getsize(VRAM_DUMP)} 字节)')
    with open(VRAM_DUMP, 'rb') as f:
        vram = f.read()
    # 显示前 400 字节
    print('前 400 字节 hex:')
    for i in range(0, min(400, len(vram)), 32):
        hex_str = ' '.join(f'{b:02x}' for b in vram[i:i+32])
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in vram[i:i+32])
        print(f'  {i:04x}: {hex_str:<96} {ascii_str}')
else:
    print(f'文件不存在: {VRAM_DUMP}')
    # 尝试查找是否有其他文件被创建
    print('检查 C:\\qemu_iso 目录是否有新文件:')
    for f in os.listdir(r'C:\qemu_iso'):
        full = os.path.join(r'C:\qemu_iso', f)
        if os.path.isfile(full):
            print(f'  {f} ({os.path.getsize(full)} 字节)')
