# IN-OS

从零实现的操作系统：自研引导（BIOS / UEFI）、内核、中文像素与编码字库、终端与图形界面、
任务调度、系统恢复与快照，以及系统编译器与 ARM→X86 翻译层。

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `system/` | 微内核、任务调度核心、通用驱动（内存 / 磁盘 / 显示 / 键盘鼠标） |
| `用户/` | 用户层：图形界面、命令行、调用器、高速内存读写 |
| `字库/` | 像素字库、编码字库、显示驱动与字库管理器 |
| `系统核心任务调度驱动/` | 调度管理器、系统恢复器（哈希校验）、运行备份与快照 |
| `翻译层/` | ARM 指令 → X86 指令翻译、编译核心与编译缓存 |
| `超级调度器/` | 顶层调度器 |
| 根目录 | 引导扇区（`mbr.asm`）、UEFI 引导（`efi64.asm`）、内核（`kernel_full.c`）、镜像构建与烧写脚本、硬件探测头文件（`hwprobe.h`、`usb_xhci.h`、`i2c_hid.h` 等） |

## 构建

依赖 Python 3 与对应汇编 / 编译工具链：

- `python build_iso.py` —— 生成可启动 ISO
- `python build_fat32_iso.py` / `python build_kernel_iso.py` —— 其他镜像构建方式
- `burn_usb.py` —— 把镜像烧写到 U 盘

根目录下以 `_qemu_*.py` / `_test_*.py` 命名的脚本是调试与验证用的临时用例，可按需参考。

## 镜像下载

系统镜像体积较大，未纳入仓库，请在 [Releases](https://github.com/ITNeurn/IN-OS/releases) 下载：

| 镜像 | 说明 |
| --- | --- |
| `system.iso` | 可启动 ISO，可直接挂载到虚拟机 |
| `uefi_boot.img` | UEFI 启动盘镜像，可烧写 U 盘或在 QEMU 中作为磁盘启动 |
| `os.img` | 原始磁盘镜像 |
| `os.vhdx` | Hyper-V 虚拟磁盘镜像 |

QEMU 快速体验：

```
qemu-system-x86_64 -drive file=uefi_boot.img,format=raw
```

## 许可证

[MIT](LICENSE)
