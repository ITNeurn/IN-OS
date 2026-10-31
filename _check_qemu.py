import subprocess

# 直接调用 wsl.exe 避开 PowerShell profile
r = subprocess.run(
    ["wsl.exe", "-d", "Ubuntu", "bash", "-c",
     "which qemu-system-x86_64; qemu-system-x86_64 --version | head -1"],
    capture_output=True, text=True
)
print("STDOUT:", r.stdout)
print("STDERR:", r.stderr)
print("RC:", r.returncode)
