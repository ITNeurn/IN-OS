# _qemu_iso_test.py - 测试 system.iso 两种引导模式
#   dd  : -hda system.iso  (模拟 Rufus DD 写入 USB, 走 MBR 链)
#   cd  : -cdrom system.iso (El Torito 硬盘模拟)
import subprocess, os, time, socket, sys

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\system.iso'
PORT = 55561
VRAM = r'C:\qemu_iso\iso_vga.bin'

mode = sys.argv[1] if len(sys.argv) > 1 else 'dd'
if mode == 'cd':
    cmd = [QEMU, '-cdrom', ISO, '-m', '256', '-boot', 'order=d',
           '-display', 'none',
           '-monitor', f'telnet:127.0.0.1:{PORT},server,nowait',
           '-serial', 'null', '-no-reboot', '-no-shutdown']
    print('=== CD/El Torito 模式 (-cdrom, 硬盘模拟) ===')
else:
    cmd = [QEMU, '-hda', ISO, '-m', '256', '-boot', 'c',
           '-display', 'none',
           '-monitor', f'telnet:127.0.0.1:{PORT},server,nowait',
           '-serial', 'null', '-no-reboot', '-no-shutdown']
    print('=== DD 模式 (-hda system.iso, MBR 链) ===')

print(f'ISO: {os.path.getsize(ISO)} 字节')
proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

sock = None
for _ in range(20):
    try:
        sock = socket.create_connection(('127.0.0.1', PORT), timeout=1)
        break
    except Exception:
        time.sleep(0.5)
if sock is None:
    print('无法连接 monitor'); proc.kill(); sys.exit(1)

def send(c):
    try:
        sock.sendall((c + '\n').encode())
    except Exception:
        return ''
    time.sleep(0.4)
    sock.settimeout(0.8)
    r = b''
    try:
        while True:
            ch = sock.recv(4096)
            if not ch: break
            r += ch
            if b'(qemu)' in r: break
    except Exception:
        pass
    return r.decode('utf-8', errors='replace')

# 端到端: 加载 2.86MB 帧 + 12.5s 动画, 等 24 秒应到内核
print('等待 24 秒 (加载+动画) ...')
time.sleep(24)
send(f'memsave 0xb8000 4000 {VRAM.replace(chr(92),"/")}')
reg = send('info registers')
send('quit')
time.sleep(1)
try:
    proc.wait(timeout=3)
except Exception:
    proc.kill()

# 早期标记 (可能已被动画/内核覆盖, 仅参考)
ok_kernel = False
if os.path.exists(VRAM):
    d = open(VRAM, 'rb').read()
    full = ''
    for o in range(0, len(d), 2):
        ch = d[o]
        full += chr(ch) if 32 <= ch < 127 else ' '
    print('\n--- 屏幕文本 (非空行) ---')
    for row in range(25):
        line = full[row*80:(row+1)*80].rstrip()
        if line.strip():
            print(f'  行{row:2}: |{line}|')
    ok_kernel = 'Micro-Kernel OS' in full and 'sys_sched' in full

# 寄存器判断保护模式
pm = ('CS =0008' in reg or 'CS =000 8' in reg)
print(f'\n内核 banner 出现: {"是" if ok_kernel else "否"}')
print('寄存器摘要:')
for ln in reg.splitlines():
    if ln.strip().startswith(('EIP', 'CS ', 'CR0')):
        print('  ' + ln.strip())
print(f'结果: {"成功 ✓" if ok_kernel else "失败 ✗"}')
