# _qemu_hd_test.py - 从 FAT32 硬盘镜像启动, 验证完整引导链 + 动画
import subprocess, os, time, socket

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
HDD = r'C:\qemu_iso\fat32.img'
VRAM_TXT = r'C:\qemu_iso\vga_txt.bin'
VRAM_GFX = r'C:\qemu_iso\vga_gfx.bin'
PORT = 55556

print(f'HDD: {os.path.getsize(HDD)} 字节')

cmd = [
    QEMU,
    '-hda', HDD,
    '-m', '256',
    '-boot', 'c',
    '-display', 'none',
    '-monitor', f'telnet:127.0.0.1:{PORT},server,nowait',
    '-serial', 'null',
    '-no-reboot',
    '-no-shutdown',
]

print('启动 QEMU (硬盘 FAT32)...')
proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

sock = None
for _ in range(20):
    try:
        sock = socket.create_connection(('127.0.0.1', PORT), timeout=1)
        break
    except Exception:
        time.sleep(0.5)
if sock is None:
    print('无法连接 monitor'); proc.kill(); raise SystemExit(1)

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

# 阶段 1: 等 4 秒 (loader 读盘阶段), 检查文本模式标记 B/M
print('等待 4 秒检查引导标记...')
time.sleep(4)
send(f'memsave 0xb8000 4000 {VRAM_TXT.replace(chr(92),"/")}')
send(f'memsave 0x7064 16 {r"C:/qemu_iso/dap_hd.bin"}')
send(f'memsave 0x7000 32 {r"C:/qemu_iso/vars_hd.bin"}')
reg = send('info registers')
print('寄存器输出:')
print(reg[:1200])

# 阶段 2: 等 16 秒 (动画 12.5 秒播放完), 检查内核 banner
print('等待 16 秒让动画播完...')
time.sleep(16)
send(f'memsave 0xb8000 4000 {VRAM_GFX.replace(chr(92),"/")}')

send('quit')
time.sleep(1)
try:
    proc.wait(timeout=3)
except Exception:
    proc.kill()

def dump_txt(path, title):
    print(f'\n=== {title} ===')
    if not os.path.exists(path):
        print('文件不存在'); return
    d = open(path, 'rb').read()
    marks = []
    for i in range(0, 16, 2):
        ch = d[i]
        marks.append(chr(ch) if 32 <= ch < 127 else ('.' if ch == 0 else f'[{ch:02x}]'))
    print('标记位:', ' '.join(marks))
    for row in range(10):
        line = ''
        for col in range(80):
            o = (row*80+col)*2
            if o < len(d):
                ch = d[o]
                line += chr(ch) if 32 <= ch < 127 else (' ' if ch == 0 else '?')
        line = line.rstrip()
        if line:
            print(f'  行{row}: |{line}|')

dump_txt(VRAM_TXT, '早期阶段 (4秒): 应显示 B M 引导标记')
dump_txt(VRAM_GFX, '后期阶段 (20秒): 应显示内核 banner')
