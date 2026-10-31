import subprocess
# 检查 WSL 里是否有 mkisofs / genisoimage / xorriso
for tool in ['mkisofs', 'genisoimage', 'xorriso', 'isohybrid']:
    r = subprocess.run(['wsl.exe', '-d', 'Ubuntu', 'bash', '-c', f'which {tool} 2>&1; {tool} --version 2>&1 | head -1'],
                       capture_output=True, text=True)
    print(f'== {tool} ==')
    print(r.stdout.strip())
