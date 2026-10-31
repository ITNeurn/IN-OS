# -*- coding: utf-8 -*-
# 创建一个最小可引导软盘镜像测试 QEMU 是否能正常引导
import subprocess, time

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
IMG = r'C:\qemu_iso\test_floppy.img'

# 512 字节引导扇区：打印 'F' 然后 hlt 死循环
bs = bytearray(512)
bs[0] = 0x31; bs[1] = 0xC0   # xor ax,ax
bs[2] = 0x8E; bs[3] = 0xD8   # mov ds,ax
bs[4] = 0x8E; bs[5] = 0xC0   # mov es,ax
bs[6] = 0xB0; bs[7] = 0x46    # mov al, 'F'
bs[8] = 0xB4; bs[9] = 0x0E    # mov ah, 0x0E
bs[10] = 0xBB; bs[11] = 0x07; bs[12] = 0x00  # mov bx, 0x0007
bs[13] = 0xCD; bs[14] = 0x10  # int 0x10
bs[15] = 0xF4                 # hlt
bs[16] = 0xEB; bs[17] = 0xFE  # jmp -2
bs[510] = 0x55; bs[511] = 0xAA

with open(IMG, 'wb') as f:
    f.write(bs)
print(f'软盘镜像已写入: {IMG} ({len(bs)} bytes)')

# 用 -fda 从软盘引导
cmd = [QEMU, '-fda', IMG, '-boot', 'a', '-display', 'none',
       '-vga', 'std', '-monitor', 'stdio', '-no-reboot', '-m', '32']

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)
time.sleep(4)

# 读显存
p.stdin.write('xp /80bx 0xb8000\n')
p.stdin.flush()
time.sleep(3)
p.stdin.write('quit\n')
p.stdin.flush()

try:
    p.wait(timeout=8)
except subprocess.TimeoutExpired:
    p.kill()

out = p.stdout.read() if p.stdout else ''
print('\n=== QEMU 输出 ===')
print(out[:3000])

# 解析显存
vram = bytearray()
import re
for line in out.splitlines():
    m = re.match(r'^[0-9a-fA-F]+:\s+(.+)$', line.strip())
    if m:
        for h in re.findall(r'(?:0x)?([0-9a-fA-F]{2})', m.group(1)):
            try: vram.append(int(h, 16))
            except: pass

print('\n=== 屏幕内容 ===')
text = ''
for i in range(0, min(len(vram), 160), 2):
    ch = vram[i]
    text += chr(ch) if 0x20 <= ch < 0x7F else (' ' if ch == 0 else f'\\x{ch:02x}')
print(repr(text))
print('\n是否打印了 F:', 'F' in text)
