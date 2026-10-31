import subprocess

# 检查 WSL Ubuntu 里 apt 源和 qemu 包情况
checks = [
    "lsb_release -a 2>/dev/null",
    "apt-cache policy qemu-system-x86 2>/dev/null | head -5",
    "dpkg -l | grep -i qemu | head -20",
    "cat /etc/apt/sources.list 2>/dev/null | head -5",
    "ls /etc/apt/sources.list.d/ 2>/dev/null",
]
for c in checks:
    print("====", c)
    r = subprocess.run(["wsl.exe", "-d", "Ubuntu", "bash", "-c", c],
                       capture_output=True, text=True)
    print(r.stdout)
    if r.stderr.strip():
        print("ERR:", r.stderr)
