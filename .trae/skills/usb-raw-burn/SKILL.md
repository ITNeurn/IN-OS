---
name: "usb-raw-burn"
description: "Windows 下把镜像 raw 裸写到物理 USB 盘(如 system.iso 烧 U 盘)的安全流程：多重校验+卷锁定+SHA256回读。当用户要求烧录/dd 写入 U 盘、物理磁盘、制作启动盘时调用。"
---

# Windows 物理 U 盘安全裸写 (Raw Burn)

把磁盘镜像（ISO/IMG）按扇区原样写入物理 USB 盘，等价于 Linux `dd if=image of=/dev/sdX`。
本工作区已实测验证（system.iso 36,579,328 字节 → 磁盘2 USB 58.2GB，SHA256 回读一致）。

## 触发场景

- 用户要求把 system.iso / fat32.img 等**烧到 U 盘 / 物理磁盘 / 制作 USB 启动盘**
- 需要 dd 到 `\\.\PhysicalDriveN`
- 不适用于：普通文件拷贝、Rufus GUI 操作（那是用户手动工具）

## 不可跳过的铁律

1. **烧录前必须向用户二次确认目标盘号**，用 `Get-Disk` 展示 Number/FriendlyName/BusType/容量/分区样式，明确报出系统盘盘号，得到明确"确认"才动手。
2. 写代码里必须有**硬编码多重闸门**：盘号匹配 + BusType=USB + 容量区间（本机 U 盘 40–80GB）+ 显式确认参数 `--i-know-this-destroys-disk-2`。任一不过立即退出。
3. 路径/脚本/镜像一律放**纯英文目录**（如 `C:\qemu_iso\`）。提权（Start-Process -Verb RunAs）传中文路径会编码错乱，表现为窗口一闪而过、无日志。
4. 非管理员会话不能直接写物理盘；用 `Start-Process powershell -Verb RunAs` 拉起提权进程，脚本末尾 `cmd /c pause` 防闪退，输出 `Tee-Object` 到英文路径日志供主会话轮询。

## 标准流程（配套脚本在本技能目录）

- `burn_usb.py`：核心裸写（纯 ctypes/Win32，无第三方依赖）。参数：
  `python burn_usb.py --disk 2 --iso C:\qemu_iso\system.iso --vol Z --i-know-this-destroys-disk-2`
- `burn_run.ps1`：提权外壳。Get-Disk 校验 → 无盘符则 diskpart 临时 `assign letter=Z` → 调 python → 摘除盘符 → `rescan`。

执行步骤：

1. `Get-Disk | Select Number,FriendlyName,BusType,Size,PartitionStyle` 枚举 → 向用户确认。
2. 把两个脚本和镜像放到 `C:\qemu_iso\`（镜像 QEMU 也在用该目录）。
3. 主会话发起：
   `Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','C:\qemu_iso\burn_run.ps1'`
   提醒用户点 UAC"是"；主会话 `Start-Sleep` 后轮询 `C:\qemu_iso\burn_py.log`。
4. 成功标志：日志出现 `SHA256 verify OK: <hash>` 且 ps1 打印 `ALL_DONE_OK`。

## 关键坑位（都已踩过）

- **err=5 ACCESS_DENIED**：卷还挂着。必须先打开卷句柄 `\\.\Z:` 做 `FSCTL_LOCK_VOLUME`(0x90018) + `FSCTL_DISMOUNT_VOLUME`(0x90020)，再写 `\\.\PhysicalDrive2`。只 diskpart remove letter 不够。
- **err=50 ERROR_NOT_SUPPORTED**：部分 U 盘不支持 `IOCTL_DISK_GET_LENGTH_INFO`。容量校验改由外层 Get-Disk 负责，IOCTL 失败只 WARN 不中止。
- **TypeError: takes 5 arguments (4 given)**：`SetFilePointerEx.argtypes` 是 4 个参数（HANDLE, LARGE_INTEGER 位移, PLARGE_INTEGER 新位移指针, DWORD 移动方式），没有第 5 个 lpOverlapped 的 ctypes 声明位（调用时也只传 4 个）。
- 写循环：4MB 块、每块前 `SetFilePointerEx(h, off, None, 0)`、写完 `FlushFileBuffers`，再从头回读整镜像长度做 SHA256 比对。
- 写完后 Windows 资源管理器仍可能显示旧的 32MB 分区缓存——属正常，重新插拔即按新 MBR 刷新；用同镜像在 QEMU `-hda system.iso` 验证过 DD 可引导。
- 关闭卷句柄即自动释放 LOCK；最后 `rescan` 让系统识别新布局。

## 完成后回报

三段式：①写入字节数/耗时 ②SHA256 校验哈希 ③明确声明系统盘/数据盘未触碰，并提示用户安全弹出后再实机启动。
