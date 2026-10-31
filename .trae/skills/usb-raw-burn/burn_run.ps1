# burn_run.ps1 - elevated: verify disk2 -> assign temp letter -> lock+dismount -> raw burn -> verify
$ErrorActionPreference = 'Continue'
$log = 'C:\qemu_iso\burn_py.log'
Remove-Item $log -ErrorAction SilentlyContinue

function Run-DP($lines, $tag) {
    $tmp = "C:\qemu_iso\_dp_$tag.txt"
    Set-Content -Path $tmp -Value ($lines -join "`n") -Encoding ASCII
    diskpart /s $tmp
}

Write-Host '== Pre-burn checks =='
$d = Get-Disk -Number 2
$d | Format-Table Number,FriendlyName,BusType,PartitionStyle,Size
if ($d.BusType -ne 'USB') { Write-Host 'ABORT: Disk 2 is NOT USB'; cmd /c pause; exit 1 }
if ($d.Size -lt 40GB -or $d.Size -gt 80GB) { Write-Host "ABORT: size $($d.Size)"; cmd /c pause; exit 1 }
Write-Host ('Disk2 OK: USB {0:N1} GB' -f ($d.Size/1GB))

# 临时给磁盘2 的第一个分区分配 Z: (python 用卷句柄 LOCK+DISMOUNT)
$part = Get-Partition -DiskNumber 2 -ErrorAction SilentlyContinue | Select-Object -First 1
if ($part) {
    if ($part.DriveLetter) { $L = "$($part.DriveLetter)" }
    else {
        Write-Host 'Assigning temp letter Z: to disk2 partition 1'
        Run-DP @('select disk 2','select partition 1','assign letter=Z') 'assign' | Out-Host
        Start-Sleep -Seconds 2
        $L = 'Z'
    }
} else { $L = 'Z' }

$py = 'C:\Users\PC\AppData\Local\Programs\Python\Python312\python.exe'
if (-not (Test-Path $py)) { $py = (Get-Command python).Source }
Write-Host "Using python: $py  (volume $L)"
& $py -u 'C:\qemu_iso\burn_usb.py' --disk 2 --iso 'C:\qemu_iso\system.iso' `
      --vol $L --i-know-this-destroys-disk-2 *>&1 | Tee-Object -FilePath $log
$code = $LASTEXITCODE
Write-Host "burn_usb.py exit code = $code"

# 摘除临时盘符并刷新, 让系统识别新的 ISO 分区布局
Run-DP @('select disk 2','select partition 1',"remove letter=$L") 'remove' | Out-Host
Run-DP @('rescan') 'rescan' | Out-Host
if ($code -eq 0) { Write-Host 'ALL_DONE_OK' } else { Write-Host 'BURN_FAILED' }
Write-Host 'Press any key to close this window...'
cmd /c pause
