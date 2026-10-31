# 诊断: dump 物理 0x7000 区域 (VBE 参数 + 诊断字段)
import subprocess, os, time, socket, sys

QEMU = r'C:\Program Files\QEMU\qemu-system-i386.exe'
ISO = r'C:\qemu_iso\system.iso'
PORT = 55563
DUMP = r'C:\qemu_iso\vbe_diag.bin'

cmd = [QEMU, '-cdrom', ISO, '-m', '256', '-boot', 'order=d',
       '-display', 'none',
       '-monitor', f'telnet:127.0.0.1:{PORT},server,nowait',
       '-serial', 'null', '-no-reboot', '-no-shutdown']
proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sock = None
for _ in range(20):
    try:
        sock = socket.create_connection(('127.0.0.1', PORT), timeout=1); break
    except Exception:
        time.sleep(0.5)

def send(c, wait=0.5):
    sock.sendall((c + '\n').encode()); time.sleep(wait)
    sock.settimeout(0.6); r = b''
    try:
        while True:
            ch = sock.recv(4096)
            if not ch: break
            r += ch
            if b'(qemu)' in r: break
    except Exception: pass
    return r.decode('utf-8', 'replace')

print('等待 22 秒...'); time.sleep(22)
send(f'memsave 0x7000 96 {DUMP.replace(chr(92), "/")}', wait=1.0)
send('quit')
time.sleep(1)
try: proc.wait(timeout=3)
except Exception: proc.kill()

d = open(DUMP, 'rb').read()
def w(off): return d[off] | (d[off+1] << 8)
def dw(off): return w(off) | (w(off+2) << 16)
print(f'magic   @0x7000 = 0x{dw(0):08X}')
print(f'fb      @0x7004 = 0x{dw(4):08X}')
print(f'pitch   @0x7008 = {w(8)}')
print(f'width   @0x700A = {w(10)}')
print(f'height  @0x700C = {w(12)}')
print(f'bpp     @0x700E = {d[14]}')
print(f'mode    @0x7010 = 0x{w(16):03X}')
print('---- 诊断 (最后一次尝试) ----')
print(f'trymode @0x7042 = 0x{w(0x42):03X}')
print(f'ax 4F01 @0x7044 = 0x{w(0x44):04X}')
print(f'attr    @0x7046 = 0x{w(0x46):04X}')
print(f'bpp     @0x7048 = {d[0x48]}')
print(f'model   @0x7049 = {d[0x49]}')
print(f'ax 4F02 @0x704A = 0x{w(0x4A):04X}')
print(f'phys    @0x704C = 0x{dw(0x4C):08X}')
