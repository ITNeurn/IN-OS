# _qemu_test_kernel.py — 用 QEMU 启动 system.iso 并 dump VGA 显存
# 验证 32 位保护模式 C 内核是否成功打印 banner

import subprocess
import sys
import os
import re
import time

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\system.iso'
VRAM_DUMP = r'C:\qemu_iso\vram_kernel.bin'
SCREENSHOT = r'C:\qemu_iso\screen_kernel.ppm'

print('=== QEMU 启动测试 (保护模式 C 内核 ISO) ===')
print(f'ISO: {ISO} ({os.path.getsize(ISO)} 字节)')

# 启动 QEMU，无显示，使用 monitor stdio 进行控制
# -display none  : 不显示窗口
# -serial stdio  : 串口输出重定向到 stdout
# -monitor stdio : monitor 重定向到 stdout (二者只能选一个，所以用 -monitor none)
# 我们用 QMP 监视器的方式：用 -monitor stdio + screen dump
cmd = [
    QEMU,
    '-drive', f'file={ISO},format=raw,if=ide,index=0,media=cdrom',
    '-m', '256',
    '-display', 'none',
    '-monitor', 'stdio',
    '-serial', 'null',
    '-no-reboot',
    '-no-shutdown',
]

print(f'命令: {" ".join(cmd)}')
print('启动 QEMU (运行 5 秒后 dump 显存)...')

# 启动进程
proc = subprocess.Popen(
    cmd,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    bufsize=1,
)

# 等待 QEMU 启动并执行
time.sleep(5)

# 发送 monitor 命令：dump 显存到文件
# 用 memsave 命令：memsave 4000 0xb8000 "filename"
# 4000 = 80*25*2 字节
monitor_cmd = f'memsave 0 4000 0xb8000 "{VRAM_DUMP}"\n'
print(f'发送 monitor 命令: {monitor_cmd.strip()}')
proc.stdin.write(monitor_cmd)
proc.stdin.flush()
time.sleep(1)

# 截图 (VGA dump)
screenshot_cmd = f'screendump {SCREENSHOT}\n'
print(f'发送 monitor 命令: {screenshot_cmd.strip()}')
proc.stdin.write(screenshot_cmd)
proc.stdin.flush()
time.sleep(1)

# 退出 QEMU
proc.stdin.write('quit\n')
proc.stdin.flush()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print()
print('=== 分析 VGA 显存 ===')

if not os.path.exists(VRAM_DUMP):
    print(f'错误: 显存 dump 文件不存在: {VRAM_DUMP}')
    sys.exit(1)

with open(VRAM_DUMP, 'rb') as f:
    vram = f.read()

print(f'显存大小: {len(vram)} 字节 (期望 4000)')
print()

# VGA 文本模式：每个字符 2 字节 (字符 + 属性)
# 属性 0x07 = 灰底黑字
# 提取每行 80 字符
text_lines = []
for row in range(25):
    line_chars = []
    for col in range(80):
        offset = (row * 80 + col) * 2
        if offset < len(vram):
            ch = vram[offset]
            attr = vram[offset + 1]
            # 只显示可打印 ASCII
            if 0x20 <= ch < 0x7F:
                line_chars.append(chr(ch))
            elif ch == 0:
                line_chars.append(' ')
            else:
                line_chars.append('?')
        else:
            line_chars.append(' ')
    line = ''.join(line_chars).rstrip()
    if line:
        text_lines.append(line)

print('VGA 文本输出 (非空行):')
print('-' * 60)
for line in text_lines:
    print(f'| {line}')
print('-' * 60)

# 检查关键 banner 是否出现
expected_keywords = [
    'Micro-Kernel OS',
    'Protected Mode',
    'Kernel running',
]

print()
print('=== Banner 检测 ===')
all_text = '\n'.join(text_lines)
for kw in expected_keywords:
    if kw.lower() in all_text.lower():
        print(f'  [OK] "{kw}" 已检测到')
    else:
        print(f'  [MISS] "{kw}" 未检测到')

if any(kw.lower() in all_text.lower() for kw in expected_keywords):
    print()
    print('=== 测试结果: PASS ===')
    print('保护模式 C 内核成功启动并打印了 banner')
else:
    print()
    print('=== 测试结果: FAIL ===')
    print('保护模式内核未启动或 banner 未打印')
    # 检查是否有 BIOS 输出残留 (SeaBIOS)
    if 'SeaBIOS' in all_text:
        print('检测到 SeaBIOS 字符串 (BIOS 启动正常，但内核未接管)')
