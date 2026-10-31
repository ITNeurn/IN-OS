import subprocess
r = subprocess.run(["wsl.exe", "-d", "Ubuntu", "bash", "-c",
                   "cat /etc/apt/sources.list.d/ubuntu.sources; echo '---'; "
                   "sudo -n true 2>&1 | head -1; echo sudo_rc=$?"],
                   capture_output=True, text=True)
print(r.stdout)
if r.stderr.strip():
    print("ERR:", r.stderr)
