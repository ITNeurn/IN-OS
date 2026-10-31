# -*- coding: utf-8 -*-
# 软盘引导测试 - 读更多显存
import subprocess, time, re

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
IMG = r'C:\qemu_iso\test_floppy.img'

cmd = [QEMU, '-drive', f'file={IMG},format=raw,if=floppy',
       '-boot', 'a', '-display', 'none', '-vga', 'std',
       '-monitor', 'stdio', '-no-reboot', '-m', '32']

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)
time.sleep(5)

# 读 4000 字节显存 (80x25x2)
p.stdin.write('xp /4000bx 0xb8000\n')
p.stdin.flush()
time.sleep(8)
p.stdin.write('quit\n')
p.stdin.flush()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()

out = p.stdout.read() if p.stdout else ''

# 解析显存
vram = bytearray()
for line in out.splitlines():
    m = re.match(r'^[0-9a-fA-F]+:\s+(.+)$', line.strip())
    if m:
        for h in re.findall(r'(?:0x)?([0-9a-fA-F]{2})', m.group(1)):
            try: vram.append(int(h, 16))
            except: pass

print(f'解析字节数: {len(vram)}')
print('\n=== 屏幕内容 (80x25) ===')
print('-' * 82)
for row in range(25):
    line = ''
    for col in range(80):
        idx = (row * 80 + col) * 2
        if idx < len(vram):
            ch = vram[idx]
            line += chr(ch) if 0x20 <= ch < 0x7F else (' ' if ch == 0 else f'\\x{ch:02x}')
        else:
            line += '?'
    print(f'|{line}|')
print('-' * 82)

# 找 'F'
text = ''
for i in range(0, len(vram), 2):
    ch = vram[i]
    text += chr(ch) if 0x20 <= ch < 0x7F else (' ' if ch == 0 else '.')
print('\n是否打印了 F:', 'F' in text)
