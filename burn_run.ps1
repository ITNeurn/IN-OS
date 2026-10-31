# burn_run.ps1 - 提权执行: 校验磁盘2 -> 卸载卷 -> raw 烧录 -> 回读校验
$ErrorActionPreference = 'Stop'
$log = 'C:\qemu_iso\burn.log'
Start-Transcript -Path $log -Force | Out-Null

try {
    Write-Host '== 烧录前最终校验 =='
    $d = Get-Disk -Number 2
    $d | Format-Table Number,FriendlyName,BusType,PartitionStyle,Size | Out-String | Write-Host
    if ($d.BusType -ne 'USB') { throw '磁盘2 不是 USB! 中止' }
    if ($d.Size -lt 40GB -or $d.Size -gt 80GB) { throw "磁盘2 容量异常: $($d.Size), 中止" }
    Write-Host ('磁盘2 校验通过: USB {0:N1} GB' -f ($d.Size/1GB))

    # 摘除磁盘2 上所有卷的盘符并 dismount, 避免写物理盘时文件系统占用
    foreach ($p in (Get-Partition -DiskNumber 2 -ErrorAction SilentlyContinue)) {
        if ($p.DriveLetter) {
            $L = "$($p.DriveLetter)"
            Write-Host "移除盘符 $L : 并 dismount"
            $script = "select volume $L`nremove letter=$L dismount`n"
            $tmp = 'C:\qemu_iso\_dp_remove.txt'
            Set-Content -Path $tmp -Value $script -Encoding ASCII
            diskpart /s $tmp | Out-String | Write-Host
        }
    }

    Write-Host '== 执行 raw 烧录 =='
    $py = (Get-Command python).Source
    & $py 'C:\Users\PC\Desktop\根目录\burn_usb.py' --disk 2 `
        --iso 'C:\Users\PC\Desktop\根目录\system.iso' `
        --i-know-this-destroys-disk-2
    if ($LASTEXITCODE -ne 0) { throw "burn_usb.py 失败 exit=$LASTEXITCODE" }

    Write-Host '== 重新扫描磁盘 =='
    Set-Content -Path 'C:\qemu_iso\_dp_rescan.txt' -Value "rescan`n" -Encoding ASCII
    diskpart /s 'C:\qemu_iso\_dp_rescan.txt' | Out-String | Write-Host
    Write-Host 'ALL_DONE_OK'
}
catch {
    Write-Host ("BURN_FAILED: " + $_.Exception.Message)
}
finally {
    Stop-Transcript | Out-Null
}
