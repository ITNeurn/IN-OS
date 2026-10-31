# -*- coding: utf-8 -*-
# 测试 pycdlib 生成的 ISO 能否在 QEMU 引导
import subprocess, time, re

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\test_pycdlib.iso'

cmd = [QEMU, '-cdrom', ISO, '-boot', 'd', '-display', 'none',
       '-vga', 'std', '-monitor', 'stdio', '-no-reboot', '-m', '32']

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)
time.sleep(6)

p.stdin.write('info registers\n')
p.stdin.flush()
time.sleep(2)
p.stdin.write('xp /32bx 0x7c00\n')
p.stdin.flush()
time.sleep(2)
p.stdin.write('xp /64bx 0xb8000\n')
p.stdin.flush()
time.sleep(5)
p.stdin.write('quit\n')
p.stdin.flush()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()

out = p.stdout.read() if p.stdout else ''
print(out[:4000])
