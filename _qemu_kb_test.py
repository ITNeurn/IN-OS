# _qemu_kb_test.py - 验证图形终端键盘命令 (monitor sendkey -> screendump)
import subprocess, socket, time, sys, os

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\system.iso'
PORT = 55570
OUT = r'C:\qemu_iso'

cmd = [QEMU, '-cdrom', ISO, '-m', '256', '-boot', 'order=d', '-display', 'none',
       '-monitor', f'telnet:127.0.0.1:{PORT},server,nowait',
       '-serial', 'null', '-no-reboot', '-no-shutdown']
p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
s = None
for _ in range(20):
    try:
        s = socket.create_connection(('127.0.0.1', PORT), timeout=1)
        break
    except Exception:
        time.sleep(0.5)
if s is None:
    print('monitor 连不上'); p.kill(); sys.exit(1)

def q(c, w=0.35):
    s.sendall((c + '\n').encode())
    time.sleep(w)
    s.settimeout(0.6)
    r = b''
    try:
        while True:
            d = s.recv(4096)
            if not d:
                break
            r += d
            if b'(qemu)' in r:
                break
    except Exception:
        pass

def type_cmd(word):
    for k in word:
        q(f'sendkey {k}', 0.22)
    q('sendkey ret', 0.6)

def shot(name):
    ppm = f'{OUT}/{name}.ppm'
    q(f'screendump {ppm.replace(chr(92), "/")}', 1.2)
    import cv2
    import numpy
    d = open(ppm, 'rb').read()
    idx = 2; vals = []
    while len(vals) < 3:
        while d[idx] in b' \t\r\n':
            idx += 1
        st = idx
        while d[idx] not in b' \t\r\n':
            idx += 1
        vals.append(int(d[st:idx]))
    idx += 1
    w, h, _ = vals
    pix = d[idx:idx + w*h*3]
    arr = numpy.frombuffer(pix, dtype=numpy.uint8).reshape(h, w, 3)
    cv2.imwrite(f'{OUT}/{name}.png', cv2.cvtColor(arr, cv2.COLOR_RGB2BGR))
    print(f'{name}.png ok')

time.sleep(18)
type_cmd('help')
time.sleep(1.0)
shot('kb1')
type_cmd('stop')      # 关闭自动流水线
time.sleep(0.5)
type_cmd('create')
time.sleep(0.5)
shot('kb2')
q('quit')
time.sleep(1)
try:
    p.wait(timeout=3)
except Exception:
    p.kill()
