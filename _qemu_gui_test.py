# _qemu_gui_test.py - 验证 VBE 图形桌面 GUI (screendump 截图)
# 用法: python _qemu_gui_test.py [cd|dd|hd]
import subprocess, os, time, socket, sys, struct

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\system.iso'
IMG = r'C:\qemu_iso\fat32.img'
PORT = 55562
OUTDIR = r'C:\qemu_iso'

mode = sys.argv[1] if len(sys.argv) > 1 else 'cd'
if mode == 'cd':
    cmd = [QEMU, '-cdrom', ISO, '-m', '256', '-boot', 'order=d',
           '-display', 'none',
           '-monitor', f'telnet:127.0.0.1:{PORT},server,nowait',
           '-serial', 'null', '-no-reboot', '-no-shutdown']
    print('=== CD/El Torito ===')
elif mode == 'dd':
    cmd = [QEMU, '-hda', ISO, '-m', '256', '-boot', 'c',
           '-display', 'none',
           '-monitor', f'telnet:127.0.0.1:{PORT},server,nowait',
           '-serial', 'null', '-no-reboot', '-no-shutdown']
    print('=== DD (-hda iso) ===')
else:
    cmd = [QEMU, '-hda', IMG, '-m', '256', '-boot', 'c',
           '-display', 'none',
           '-monitor', f'telnet:127.0.0.1:{PORT},server,nowait',
           '-serial', 'null', '-no-reboot', '-no-shutdown']
    print('=== HDD fat32.img ===')

proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sock = None
for _ in range(20):
    try:
        sock = socket.create_connection(('127.0.0.1', PORT), timeout=1)
        break
    except Exception:
        time.sleep(0.5)
if sock is None:
    print('monitor 连不上'); proc.kill(); sys.exit(1)

def send(c, wait=0.6):
    try:
        sock.sendall((c + '\n').encode())
    except Exception:
        return ''
    time.sleep(wait)
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

def ppm_to_png(ppm_path, png_path):
    with open(ppm_path, 'rb') as f:
        data = f.read()
    # P6 header
    assert data[:2] == b'P6', data[:16]
    idx = 2
    vals = []
    while len(vals) < 3:
        while data[idx] in b' \t\r\n': idx += 1
        if data[idx:idx+1] == b'#':
            while data[idx] not in b'\r\n': idx += 1
            continue
        start = idx
        while data[idx] not in b' \t\r\n': idx += 1
        vals.append(int(data[start:idx]))
    idx += 1
    w, h, maxv = vals
    pix = data[idx:idx + w*h*3]
    try:
        import cv2
        import numpy as np
        arr = np.frombuffer(pix, dtype=np.uint8).reshape(h, w, 3)
        arr = cv2.cvtColor(arr, cv2.COLOR_RGB2BGR)   # PPM 是 RGB, cv2 存盘要 BGR
        cv2.imwrite(png_path, arr)
    except Exception as e:
        from PIL import Image
        Image.frombytes('RGB', (w, h), pix).save(png_path)
    # 非黑色像素统计 (验证画面真实内容)
    nonzero = sum(1 for i in range(0, len(pix), 97) if pix[i] or pix[i+1] or pix[i+2])
    print(f'  截图 {w}x{h}, 采样非黑={nonzero}')
    return w, h

shots = [(22, 'gui_1'), (28, 'gui_2')]
t0 = time.time()
results = []
for wait_s, name in shots:
    dt = wait_s - (time.time() - t0)
    if dt > 0: time.sleep(dt)
    ppm = f'{OUTDIR}/{name}.ppm'
    send(f'screendump {ppm.replace(chr(92), "/")}', wait=1.2)
    if os.path.exists(ppm):
        results.append((name, ppm_to_png(ppm, f'{OUTDIR}/{name}.png')))
    else:
        print(f'  {name}: 截图失败')

# info registers 确认保护模式
reg = send('info registers', wait=0.6)
send('quit')
time.sleep(1)
try:
    proc.wait(timeout=3)
except Exception:
    proc.kill()

for ln in reg.splitlines():
    if ln.strip().startswith(('EIP', 'CS ', 'CR0')):
        print('  ' + ln.strip())
for name, (w, h) in results:
    print(f'  {name}: {w}x{h}  -> {OUTDIR}/{name}.png')
