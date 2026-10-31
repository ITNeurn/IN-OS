# -*- coding: utf-8 -*-
# 用 QEMU 启动最小测试 ISO，读取 VGA 显存验证引导是否成功
import subprocess, time, re

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\test_min.iso'

print('=== QEMU 最小 ISO 测试 ===')
cmd = [QEMU, '-cdrom', ISO, '-boot', 'd', '-display', 'none',
       '-vga', 'std', '-monitor', 'stdio', '-no-reboot', '-m', '32']
print(' '.join(cmd))

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)
time.sleep(8)
try:
    p.stdin.write('xp /4000bx 0xb8000\n')
    p.stdin.flush()
    time.sleep(8)
    p.stdin.write('quit\n')
    p.stdin.flush()
except (BrokenPipeError, OSError) as e:
    print(f'  [warn] stdin: {e}')

try:
    p.wait(timeout=15)
except subprocess.TimeoutExpired:
    p.kill()
    print('  [warn] QEMU 超时')

out = p.stdout.read() if p.stdout else ''
print('\nQEMU 输出前 3000 字符:')
print(out[:3000] if out else '(无)')

# 解析显存
vram = bytearray()
for line in out.splitlines():
    m = re.match(r'^[0-9a-fA-F]+:\s+(.+)$', line.strip())
    if m:
        for h in re.findall(r'(?:0x)?([0-9a-fA-F]{2})', m.group(1)):
            try:
                vram.append(int(h, 16))
            except ValueError:
                pass

# 找 'A' 字符 (0x41) 的位置
non_zero = [(i, vram[i]) for i in range(min(len(vram), 200)) if vram[i] != 0]
print(f'\n显存前 200 字节非零项: {non_zero[:20]}')
print(f'总解析字节数: {len(vram)}')

# 检查 'A' (0x41) + attr 0x07
found_a = False
for i in range(0, min(len(vram), 4000), 2):
    if vram[i] == 0x41 and vram[i+1] == 0x07:
        print(f'[OK] 在偏移 {i} 找到 A+0x07')
        found_a = True
        break
if not found_a:
    print('[FAIL] 没找到 A+0x07')

print('\n=== 测试完成 ===')
