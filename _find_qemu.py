import subprocess, os

# 1) WSL 里找
print("=== WSL: find qemu-system ===")
r = subprocess.run(["wsl.exe", "-d", "Ubuntu", "bash", "-c",
    "ls /usr/bin/qemu-system* /usr/local/bin/qemu-system* 2>&1; "
    "find / -name 'qemu-system-x86_64*' -type f 2>/dev/null | head; "
    "find / -name 'qemu-system-i386*' -type f 2>/dev/null | head"],
    capture_output=True, text=True)
print(r.stdout)
if r.stderr.strip(): print("ERR:", r.stderr)

# 2) Windows 上找常见安装目录
print("=== Win: check common paths ===")
cands = [
    r"C:\Program Files\qemu",
    r"C:\Program Files (x86)\qemu",
    r"C:\qemu",
    r"D:\qemu",
    r"E:\qemu",
    r"F:\qemu",
]
for base in cands:
    if os.path.isdir(base):
        print(base, "->", os.listdir(base))
    else:
        print(base, "(not exist)")

# 3) Windows PATH 全量查找
print("=== Win: where ===")
r = subprocess.run(["where.exe", "/R", "C:\\", "qemu-system-x86_64.exe"],
                   capture_output=True, text=True, timeout=120)
print(r.stdout or "(none in C:)")
