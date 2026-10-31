# -*- coding: utf-8 -*-
# 软盘引导测试 - 直接写 VGA 显存 (0xB8000)，不依赖 int 0x10
import subprocess, time, re

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
IMG = r'C:\qemu_iso\test_floppy.img'

# 引导扇区：直接写 0xB8000 显存
bs = bytearray(512)
# mov ax, 0xB800
bs[0] = 0xB8; bs[1] = 0x00; bs[2] = 0xB8    # B8 00 B8
# mov es, ax
bs[3] = 0x8E; bs[4] = 0xC0                   # 8E C0
# xor di, di
bs[5] = 0x31; bs[6] = 0xFF                   # 31 FF
# mov ax, 0x0746  (attr=0x07, char='F'=0x46, little-endian: 46 07)
bs[7] = 0xB8; bs[8] = 0x46; bs[9] = 0x07     # B8 46 07
# stosw  [es:di]=ax, di+=2
bs[10] = 0xAB                               # AB
# hlt
bs[11] = 0xF4                               # F4
# jmp -2
bs[12] = 0xEB; bs[13] = 0xFE                 # EB FE
# 510-511: 55 AA
bs[510] = 0x55; bs[511] = 0xAA

with open(IMG, 'wb') as f:
    f.write(bs)
    f.write(b'\x00' * (1474560 - 512))

cmd = [QEMU, '-fda', IMG, '-boot', 'order=a', '-display', 'none',
       '-vga', 'std', '-monitor', 'stdio', '-no-reboot', '-m', '32']

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)
time.sleep(6)

# 检查寄存器（看 EIP 是否在 0x7C00 附近）
p.stdin.write('info registers\n')
p.stdin.flush()
time.sleep(2)
# 检查 0x7C00 内容
p.stdin.write('xp /16bx 0x7c00\n')
p.stdin.flush()
time.sleep(2)
# 读显存开头
p.stdin.write('xp /16bx 0xb8000\n')
p.stdin.flush()
time.sleep(3)
p.stdin.write('quit\n')
p.stdin.flush()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()

out = p.stdout.read() if p.stdout else ''
print(out[:4000])
