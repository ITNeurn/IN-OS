# -*- coding: utf-8 -*-
# 软盘引导测试 - 1.44MB 镜像 + 检查软盘状态
import subprocess, time, re

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
IMG = r'C:\qemu_iso\test_floppy.img'

# 512 字节引导扇区
bs = bytearray(512)
bs[0] = 0x31; bs[1] = 0xC0   # xor ax,ax
bs[2] = 0x8E; bs[3] = 0xD8   # mov ds,ax
bs[4] = 0x8E; bs[5] = 0xC0   # mov es,ax
bs[6] = 0xB0; bs[7] = 0x46    # mov al, 'F'
bs[8] = 0xB4; bs[9] = 0x0E    # mov ah, 0x0E
bs[10] = 0xBB; bs[11] = 0x07; bs[12] = 0x00
bs[13] = 0xCD; bs[14] = 0x10  # int 0x10
bs[15] = 0xF4                 # hlt
bs[16] = 0xEB; bs[17] = 0xFE  # jmp -2
bs[510] = 0x55; bs[511] = 0xAA

# 填充到 1.44MB
with open(IMG, 'wb') as f:
    f.write(bs)
    f.write(b'\x00' * (1474560 - 512))
print(f'软盘镜像: {IMG} (1.44MB)')

cmd = [QEMU, '-fda', IMG, '-boot', 'order=a', '-display', 'none',
       '-vga', 'std', '-monitor', 'stdio', '-no-reboot', '-m', '32']

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)
time.sleep(5)

# 检查软盘状态
p.stdin.write('info block\n')
p.stdin.flush()
time.sleep(2)
# 读 0x7C00
p.stdin.write('xp /20bx 0x7c00\n')
p.stdin.flush()
time.sleep(3)
# 读显存
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
print(out[:5000])
