# burn_usb.py - 将镜像原始写入物理 USB 盘 (多重安全校验)
# 用法(需管理员): python burn_usb.py --disk 2 --iso system.iso --i-know-this-destroys-disk-2
import argparse, hashlib, sys, time, ctypes
from ctypes import wintypes

GENERIC_READ  = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_RW = 0x00000003
OPEN_EXISTING = 3
IOCTL_DISK_GET_LENGTH_INFO = 0x00074058
FSCTL_LOCK_VOLUME       = 0x00090018
FSCTL_DISMOUNT_VOLUME   = 0x00090020

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype = wintypes.HANDLE
k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                            ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
k32.DeviceIoControl.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
                                ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
k32.ReadFile.argtypes  = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                          ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
k32.WriteFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                          ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
k32.SetFilePointerEx.argtypes = [wintypes.HANDLE, ctypes.c_longlong,
                                 ctypes.POINTER(ctypes.c_longlong), wintypes.DWORD]

CHUNK = 4 * 1024 * 1024

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--disk', type=int, required=True)
    ap.add_argument('--iso', required=True)
    ap.add_argument('--i-know-this-destroys-disk-2', action='store_true')
    ap.add_argument('--vol', default='Z', help='disk2 上分区的临时盘符 (用于 LOCK/DISMOUNT)')
    args = ap.parse_args()

    if args.disk != 2 or not getattr(args, 'i_know_this_destroys_disk_2'):
        print('SAFETY: 参数不满足, 拒绝执行'); sys.exit(2)

    iso = open(args.iso, 'rb')
    iso.seek(0, 2); iso_len = iso.tell(); iso.seek(0)

    # ---- 先锁定并卸载该盘上的卷, 否则裸写物理盘会 err=5 (ACCESS_DENIED) ----
    volpath = r'\\.\%s:' % args.vol
    vh = k32.CreateFileW(volpath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_RW,
                         None, OPEN_EXISTING, 0, None)
    vol_locked = False
    if vh and vh != wintypes.HANDLE(-1).value:
        br = wintypes.DWORD(0)
        ok1 = k32.DeviceIoControl(vh, FSCTL_LOCK_VOLUME, None, 0, None, 0,
                                  ctypes.byref(br), None)
        ok2 = k32.DeviceIoControl(vh, FSCTL_DISMOUNT_VOLUME, None, 0, None, 0,
                                  ctypes.byref(br), None)
        vol_locked = bool(ok1 and ok2)
        print('volume %s: lock=%d dismount=%d' % (args.vol, ok1, ok2), flush=True)
        if not vol_locked:
            print('WARN: volume lock failed err=%d, continue anyway'
                  % ctypes.get_last_error(), flush=True)
    else:
        print('WARN: cannot open %s err=%d (will try raw write directly)'
              % (volpath, ctypes.get_last_error()), flush=True)

    path = r'\\.\PhysicalDrive%d' % args.disk
    h = k32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_RW,
                        None, OPEN_EXISTING, 0, None)
    if not h or h == wintypes.HANDLE(-1).value:
        print('打开 %s 失败 err=%d (需要管理员)' % (path, ctypes.get_last_error())); sys.exit(3)

    # 读物理盘容量; 部分 USB 设备不支持该 IOCTL(err=50), 失败则仅警告
    # (容量门槛已由提权侧 Get-Disk 校验: 必须为 USB 且 40-80GB)
    length = ctypes.c_longlong(0)
    retb = wintypes.DWORD(0)
    ok = k32.DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, None, 0,
                             ctypes.byref(length), ctypes.sizeof(length),
                             ctypes.byref(retb), None)
    cap = length.value if ok else -1
    if ok:
        print('target: %s  size=%d bytes (%.1f GB)' % (path, cap, cap / 1e9), flush=True)
        if not (40_000_000_000 <= cap <= 80_000_000_000):
            print('SAFETY: size out of 40-80GB range, abort!'); sys.exit(5)
        if iso_len > cap:
            print('SAFETY: image larger than disk, abort!'); sys.exit(6)
    else:
        print('WARN: IOCTL_GET_LENGTH unsupported err=%d, rely on outer Get-Disk gate'
              % ctypes.get_last_error(), flush=True)

    print('image : %s  %d bytes (%.2f MB)' % (args.iso, iso_len, iso_len / 1e6), flush=True)
    print('---- writing ----', flush=True)
    t0 = time.time()
    written = 0
    buf = (ctypes.c_ubyte * CHUNK)()
    while True:
        n = iso.readinto(buf)
        if not n:
            break
        off = written
        where = ctypes.c_longlong(off)
        k32.SetFilePointerEx(h, where, None, 0)
        wr = wintypes.DWORD(0)
        ok = k32.WriteFile(h, buf, n, ctypes.byref(wr), None)
        if not ok or wr.value != n:
            print('write failed @%d err=%d' % (off, ctypes.get_last_error())); sys.exit(7)
        written += n
        pct = written * 100 // iso_len
        sys.stdout.write('\r  %d%%  %d/%d MB' % (pct, written // 1048576, iso_len // 1048576))
        sys.stdout.flush()
    print()
    k32.FlushFileBuffers(h)
    print('write done in %.1fs, verifying by readback...' % (time.time() - t0), flush=True)

    iso.seek(0)
    want = hashlib.sha256(iso.read()).digest()
    k32.SetFilePointerEx(h, ctypes.c_longlong(0), None, 0)
    got = hashlib.sha256()
    remain = iso_len
    rbuf = (ctypes.c_ubyte * CHUNK)()
    while remain:
        n = min(CHUNK, remain)
        rd = wintypes.DWORD(0)
        ok = k32.ReadFile(h, rbuf, n, ctypes.byref(rd), None)
        if not ok or rd.value != n:
            print('read failed err=%d' % ctypes.get_last_error()); sys.exit(8)
        got.update(bytes(rbuf[:rd.value]))
        remain -= rd.value
    if got.digest() != want:
        print('VERIFY FAILED: sha256 mismatch'); sys.exit(9)
    print('SHA256 verify OK: %s' % got.hexdigest(), flush=True)
    k32.CloseHandle(h)
    if vh and vh != wintypes.HANDLE(-1).value:
        k32.CloseHandle(vh)   # 关闭句柄即自动解除卷锁
    print('OK')

if __name__ == '__main__':
    main()
