# _qemu_mem_check.py - 检查 QEMU 中 0x10200 处实际加载的代码
import subprocess, sys, os, time, threading

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\system.iso'
DUMP = r'C:\qemu_iso\mem_0x10200.bin'

cmd = [
    QEMU,
    '-drive', f'file={ISO},format=raw,if=ide,index=0,media=cdrom',
    '-m', '256',
    '-boot', 'd',
    '-display', 'none',
    '-monitor', 'stdio',
    '-serial', 'null',
    '-no-reboot',
    '-no-shutdown',
]

print(f'启动 QEMU 检查 0x10200 内存...')
proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=False, bufsize=0)

out = bytearray()
def reader():
    while True:
        c = proc.stdout.read(1)
        if not c: break
        out.extend(c)
t = threading.Thread(target=reader, daemon=True)
t.start()

time.sleep(4)

# dump 0x10200 处 256 字节
proc.stdin.write(b'memsave 0x10200 256 ' + DUMP.replace('\\','/').encode() + b'\n')
proc.stdin.flush()
time.sleep(2)

# 也 dump 0x10000 处
proc.stdin.write(b'memsave 0x10000 512 ' + DUMP.replace('.bin','_10000.bin').replace('\\','/').encode() + b'\n')
proc.stdin.flush()
time.sleep(2)

proc.stdin.write(b'quit\n')
proc.stdin.flush()
try:
    proc.wait(timeout=5)
except:
    proc.kill()

print()
print('=== 0x10200 处 256 字节 (应为 kernel_main 代码) ===')
if os.path.exists(DUMP):
    data = open(DUMP,'rb').read()
    print(f'size: {len(data)}')
    for i in range(0, min(128, len(data)), 16):
        h = ' '.join(f'{b:02x}' for b in data[i:i+16])
        a = ''.join(chr(b) if 32<=b<127 else '.' for b in data[i:i+16])
        print(f'  {i:04x}: {h:<48} {a}')
else:
    print('dump 文件不存在')

print()
print('=== 0x10000 处 512 字节 (应为引导扇区副本) ===')
DUMP2 = DUMP.replace('.bin','_10000.bin')
if os.path.exists(DUMP2):
    data = open(DUMP2,'rb').read()
    print(f'size: {len(data)}')
    for i in range(0, min(64, len(data)), 16):
        h = ' '.join(f'{b:02x}' for b in data[i:i+16])
        a = ''.join(chr(b) if 32<=b<127 else '.' for b in data[i:i+16])
        print(f'  {i:04x}: {h:<48} {a}')
else:
    print('dump 文件不存在')

# 期望的 kernel_main 开头（从 ISO 字节 41472 处取）
iso = open(ISO,'rb').read()
expected = iso[41472:41472+16]
print()
print('=== ISO 中 boot.img+512 处 (期望值) ===')
print('  ' + ' '.join(f'{b:02x}' for b in expected))
