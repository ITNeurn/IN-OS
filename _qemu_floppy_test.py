# _qemu_floppy_test.py - 直接用 boot.img 作为软盘启动，测试 CHS 读取
# 用 telnet socket 连接 QEMU monitor，避免 stdin/stdout 问题
import subprocess, sys, os, time, socket

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
BOOT_IMG = r'C:\qemu_iso\boot.img'  # 英文路径避免 QEMU 中文路径问题
VRAM = r'C:\qemu_iso\vram_chs.bin'
MEM_K = r'C:\qemu_iso\mem_chs_10200.bin'
MONITOR_PORT = 55555

if not os.path.exists(BOOT_IMG):
    print(f'错误: boot.img 不存在: {BOOT_IMG}')
    print('请先复制 boot.img 到 C:\\qemu_iso\\')
    sys.exit(1)

print(f'boot.img: {os.path.getsize(BOOT_IMG)} 字节')

cmd = [
    QEMU,
    '-fda', BOOT_IMG,
    '-m', '256',
    '-boot', 'a',
    '-display', 'none',
    '-monitor', f'telnet:127.0.0.1:{MONITOR_PORT},server,nowait',
    '-serial', 'null',
    '-no-reboot',
    '-no-shutdown',
]

print(f'启动 QEMU (软盘模式, CHS 读取, monitor telnet:{MONITOR_PORT})...')
proc = subprocess.Popen(
    cmd,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)

# 等待 QEMU monitor 端口可用
sock = None
for _ in range(20):
    try:
        sock = socket.create_connection(('127.0.0.1', MONITOR_PORT), timeout=1)
        break
    except Exception:
        time.sleep(0.5)

if sock is None:
    print('错误: 无法连接 QEMU monitor')
    proc.kill()
    sys.exit(1)

print('已连接 QEMU monitor')

def send_cmd(cmd_str):
    try:
        sock.sendall((cmd_str + '\n').encode())
    except Exception:
        return ''
    time.sleep(0.5)
    sock.settimeout(1.0)
    resp = b''
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            resp += chunk
            if b'(qemu)' in resp:
                break
    except (socket.timeout, ConnectionResetError, OSError):
        pass
    return resp.decode('utf-8', errors='replace')

# 等待引导扇区执行
time.sleep(3)

# dump VGA 显存
r = send_cmd(f'memsave 0xb8000 4000 {VRAM.replace(chr(92), "/")}')
print(f'VGA dump 响应: {r.strip()[:100]}')

# dump 0x10200 内核代码
r = send_cmd(f'memsave 0x10200 256 {MEM_K.replace(chr(92), "/")}')
print(f'kernel dump 响应: {r.strip()[:100]}')

# 查看寄存器
r = send_cmd('info registers')
print(f'寄存器: {r[:300]}')

# 退出
send_cmd('quit')
time.sleep(1)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()

print()
print('=== VGA 显存前 200 字节 (解析为 80x25 文本) ===')
if os.path.exists(VRAM):
    data = open(VRAM, 'rb').read()
    chars = []
    for i in range(0, 20, 2):
        ch = data[i]
        if 32 <= ch < 127:
            chars.append(chr(ch))
        elif ch == 0:
            chars.append(' ')
        else:
            chars.append(f'[{ch:02x}]')
    print(f'  标记位: {" ".join(chars)}')
    print(f'  hex 前 20: {" ".join(f"{b:02x}" for b in data[:20])}')
    print(f'  期望: B L P M K C ... (引导扇区→CHS 读→PM Loader→kernel_main)')
    print('  文本内容:')
    for row in range(8):
        line = ''
        for col in range(80):
            off = (row * 80 + col) * 2
            if off < len(data):
                ch = data[off]
                if 32 <= ch < 127:
                    line += chr(ch)
                elif ch == 0:
                    line += ' '
                else:
                    line += '?'
            else:
                line += ' '
        line = line.rstrip()
        if line:
            print(f'    行{row}: |{line}|')
else:
    print('VRAM dump 不存在')

print()
print('=== 0x10200 处 64 字节 (kernel_main 代码) ===')
if os.path.exists(MEM_K):
    data = open(MEM_K, 'rb').read()
    print(f'  hex: {" ".join(f"{b:02x}" for b in data[:64])}')
    if len(data) >= 3 and data[0] == 0x55 and data[1] == 0x89 and data[2] == 0xE5:
        print('  ✓ kernel_main 已加载! (55 89 e5 = push ebp; mov ebp,esp)')
    elif all(b == 0 for b in data[:16]):
        print('  ✗ 全 0, kernel 未加载')
    else:
        print(f'  ? 内容非零但不是 kernel_main 序言')
else:
    print('内存 dump 不存在')
