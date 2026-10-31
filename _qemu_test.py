# -*- coding: utf-8 -*-
# _qemu_test.py 用 QEMU 启动 system.iso，6 秒后读 VGA 文本显存 0xB8000 还原屏幕内容。
import subprocess, time, os, sys

ROOT = r'c:\Users\PC\Desktop\根目录'
QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\system.iso'  # 复制到英文路径避免 QEMU 中文路径问题
PPM = r'C:\qemu_iso\screen.ppm'
VRAM = r'C:\qemu_iso\vram.bin'

print('=== QEMU BIOS 模式启动测试 ===')
print(f'QEMU: {QEMU}')
print(f'ISO:  {ISO}')

# 启动 QEMU: -display none + -vga std 保留 VGA, -monitor stdio 接管控制台
cmd = [
    QEMU,
    '-cdrom', ISO,
    '-boot', 'd',
    '-display', 'none',
    '-vga', 'std',
    '-monitor', 'stdio',
    '-no-reboot',
    '-m', '32',
]
print(f'\n启动命令: {" ".join(cmd)}')
print('等待 8 秒让 BIOS 启动 + 引导扇区 + 安装器执行...')

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)

time.sleep(8)

# 发送 monitor 命令：xp 直接输出 VGA 显存到 stdout
print('\n发送 monitor 命令: xp (dump 0xB8000)')
try:
    # xp /4000bx 0xb8000 = 以字节(hex)格式输出 0xB8000 起 4000 字节
    p.stdin.write('xp /4000bx 0xb8000\n')
    p.stdin.flush()
    time.sleep(8)  # 给 xp 命令充足时间输出 4000 字节
    # quit
    print('发送 monitor 命令: quit')
    p.stdin.write('quit\n')
    p.stdin.flush()
except (BrokenPipeError, OSError) as e:
    print(f'  [warn] stdin 写入失败: {e}')

try:
    p.wait(timeout=15)
except subprocess.TimeoutExpired:
    p.kill()
    print('  [warn] QEMU 超时，强制终止')

# 读 QEMU 输出
try:
    out = p.stdout.read() if p.stdout else ''
except:
    out = ''
print(f'\nQEMU stdout/stderr (前 5000 字符):')
print(out[:5000] if out else '(无输出)')

# 从 xp 命令输出解析 VGA 显存
# xp 输出格式: "000b8000: 0x20 0x07 0x20 0x07 ..."
import re
vram_bytes = bytearray()
for line in out.splitlines():
    # 匹配格式 "000b8000: 0x20 0x07 ..." 或 "00000000000b8000: 20 07 ..."
    m = re.match(r'^[0-9a-fA-F]+:\s+(.+)$', line.strip())
    if m:
        hex_part = m.group(1)
        # 用正则抓取所有 0xNN 或 NN 形式的字节
        for h in re.findall(r'(?:0x)?([0-9a-fA-F]{2})', hex_part):
            try:
                vram_bytes.append(int(h, 16))
            except ValueError:
                pass

print(f'\n=== VGA 显存分析 (从 xp 输出解析) ===')
print(f'解析字节数: {len(vram_bytes)}')
if len(vram_bytes) < 4000:
    print(f'[WARN] 字节数不足 4000 (80x25x2)')
    vram_bytes.extend(b'\x00' * (4000 - len(vram_bytes)))
vram = bytes(vram_bytes[:4000])

# 文本模式：每个字符 2 字节 (char + attr)
# 80x25 = 2000 字符 = 4000 字节
print('\n屏幕内容 (80x25 文本模式):')
print('-' * 82)
for row in range(25):
    line = ''
    for col in range(80):
        idx = (row * 80 + col) * 2
        if idx + 1 < len(vram):
            ch = vram[idx]
            attr = vram[idx + 1]
            # 字符（ASCII 可见字符）
            if 0x20 <= ch < 0x7F:
                line += chr(ch)
            elif ch == 0:
                line += ' '
            else:
                line += f'\\x{ch:02x}'
        else:
            line += '?'
    print(f'|{line}|')
print('-' * 82)

# 检查关键字符串
print('\n关键字符串检查:')
text = ''
for i in range(0, len(vram), 2):
    ch = vram[i]
    if 0x20 <= ch < 0x7F:
        text += chr(ch)
    elif ch == 0:
        text += ' '
    else:
        text += '.'

keywords = ['Micro-Kernel', 'Installing', 'system', 'HDD', 'UEFI', 'Hello', 'boot', 'Boot',
            'Loading', 'Complete', 'ERR', 'Installer', 'Micro', 'OS']
for kw in keywords:
    if kw.lower() in text.lower():
        # 找位置
        idx = text.lower().find(kw.lower())
        ctx = text[max(0, idx-5):idx+len(kw)+10]
        print(f'  [FOUND] "{kw}" 上下文: "{ctx}"')
    else:
        print(f'  [MISS]  "{kw}"')

print('\n=== 测试完成 ===')
