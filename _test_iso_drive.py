# -*- coding: utf-8 -*-
# ISO 引导测试 - 用 -drive 代替 -cdrom，明确 format=raw
import subprocess, time, re

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\test_min.iso'

# 用 -drive 明确指定 format=raw, media=cdrom
cmd = [QEMU,
       '-drive', f'file={ISO},format=raw,if=ide,index=1,media=cdrom',
       '-boot', 'order=d',
       '-display', 'none', '-vga', 'std',
       '-monitor', 'stdio', '-no-reboot', '-m', '32']

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)
time.sleep(6)

p.stdin.write('info block\n')
p.stdin.flush()
time.sleep(2)
p.stdin.write('info registers\n')
p.stdin.flush()
time.sleep(2)
p.stdin.write('xp /20bx 0x7c00\n')
p.stdin.flush()
time.sleep(2)
p.stdin.write('xp /160bx 0xb8000\n')
p.stdin.flush()
time.sleep(5)
p.stdin.write('quit\n')
p.stdin.flush()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()

out = p.stdout.read() if p.stdout else ''
print(out[:5000])
