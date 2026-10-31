# -*- coding: utf-8 -*-
# 诊断：启动最小 ISO，检查 0x7C00 处是否有 boot image，以及寄存器/显存状态。
import subprocess, time, re

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\test_min.iso'

cmd = [QEMU, '-cdrom', ISO, '-boot', 'd', '-display', 'none',
       '-vga', 'std', '-monitor', 'stdio', '-no-reboot', '-m', '32']

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)
time.sleep(6)

def send(s):
    try:
        p.stdin.write(s + '\n')
        p.stdin.flush()
    except (BrokenPipeError, OSError) as e:
        print(f'[warn] 写入失败: {e}')

def read_avail(wait=2):
    time.sleep(wait)
    # 非阻塞读
    import os
    out = ''
    return out

# 1. 检查 0x7C00 内容 (boot image 是否被加载)
send('xp /40bx 0x7c00')
time.sleep(3)
# 2. 检查寄存器
send('info registers')
time.sleep(3)
# 3. 读完整显存 (分批)
send('xp /2000bx 0xb8000')
time.sleep(5)
send('xp /2000bx 0xb8200')
time.sleep(5)
send('quit')
time.sleep(1)

try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()

out = p.stdout.read() if p.stdout else ''
print(out)
