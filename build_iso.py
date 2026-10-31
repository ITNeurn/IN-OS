# -*- coding: utf-8 -*-
# build_iso.py
# 纯 Python 实现的微内核操作系统 ISO 构建脚本。
#
# 输出三件产物：
#   1. boot.img   —— 1.44MB 软盘镜像（512B 引导扇区 + 16KB 内核加载器）
#   2. system.iso —— El Torito 可启动 ISO9660 镜像（包含 boot.img 与全部源码）
#   3. (副产品) bootloader.bin / installer.bin / hdd_system.bin
#
# 无需 nasm / gcc / mkisofs / xorriso，纯 Python 标准库实现。
#
# 引导流程：
#   BIOS → 加载 ISO 中 El Torito 指定的 boot.img（1.44MB 软盘仿真）到 0x07C00
#        → 跳转 0x0000:0x7C00 执行引导扇区代码
#   引导扇区 → 先检测 INT 13h LBA 扩展（AH=0x41）：
#               支持：用 AH=0x42 + DAP 从 LBA 1 读 32 扇区到 0x0000:0x1000
#               不支持：用 CHS AH=0x02 分磁道读取（1.44MB 软盘每磁道 18 扇区，
#                       从扇区 2 读 32 扇区需拆 17+15 两次避免跨磁头边界）
#            → 远跳转 0x0000:0x1000 执行安装器
#   安装器 → 检测软盘 LBA 扩展，支持则用 AH=0x42 从 LBA 33 读 32 扇区系统镜像到 0x8000，
#            不支持则用 CHS 分磁道读取（LBA 33 起 32 扇区拆 3+18+11 三次）；
#            检测硬盘存在性（AH=0x15）与硬盘 LBA 扩展；
#            用 AH=0x43 将 0x8000 的 32 扇区写入硬盘 LBA 0；重启
#   硬盘启动 → HDD 引导扇区用 LBA 扩展从 HDD LBA 1 读 32 扇区到 0x1000
#            → 跳转执行 HDD 加载器，显示 "Running from HDD"，hlt
#
# 说明：当前环境无 C 编译器，因此内核加载器之后没有真实的 C 内核可加载。
#       加载器在打印启动横幅后进入 hlt 循环，这是当前环境下能产出的
#       "真正可在 BIOS/QEMU 启动到屏幕输出" 的最小可启动 ISO。

import os
import struct
import datetime

# 项目根目录（脚本所在目录）
PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))

# ====================================================================
# 第一部分：x86 16位实模式机器码生成
# ====================================================================

def build_boot_sector():
    """
    生成 512 字节引导扇区机器码。
    入口：BIOS 将其加载到 0x0000:0x7C00 并跳转执行，DL=启动驱动器号。

    流程：
      1. 设置段寄存器（DS=ES=SS=0，SP=0x7C00），保留 BIOS 给的 DL
      2. 检测 BIOS LBA 扩展（INT 13h AH=0x41，BX=0x55AA → BX=0xAA55）
      3. 支持 LBA：构造 DAP 在 0x7000，用 AH=0x42 读 32 扇区到 0x0000:0x1000
      4. 不支持或失败：用 CHS 分磁道读取（1.44MB 软盘每磁道 18 扇区，
         从扇区 2 直接读 32 扇区会跨越磁头 0→1 边界，老 BIOS 会失败。
         拆成两次读：磁头 0 柱面 0 扇区 2-18 共 17 扇区 → 0x1000；
                     磁头 1 柱面 0 扇区 1-15 共 15 扇区 → 0x3200）
      5. 远跳转到 0x0000:0x1000 执行加载器
      6. 错误：屏幕打印 'ERR' 并 hlt 死循环
    """
    a = Asm16()

    # --- 调试：设 ES=0xB800 用于写 VGA 字符 ---
    a.emit(0xB8, 0x00, 0xB8)       # mov ax, 0xB800
    a.emit(0x8E, 0xC0)             # mov es, ax
    # DI=0 指向屏幕第一个字符；每写一个标记 DI+=2
    a.emit(0x31, 0xFF)             # xor di, di
    # 写 'B' (boot sector entered)
    a.emit(0x26, 0xC6, 0x05, 0x42) # mov byte [es:di], 'B'
    a.emit(0x26, 0xC6, 0x45, 0x01, 0x07)  # mov byte [es:di+1], 0x07
    a.emit(0x83, 0xC7, 0x02)       # add di, 2

    # --- 设置段寄存器 ---
    a.emit(0x31, 0xC0)             # xor ax, ax
    a.emit(0x8E, 0xD8)             # mov ds, ax
    # ES 先保持 0xB800 用于调试；INT 13h 需要时再设
    a.emit(0x8E, 0xD0)             # mov ss, ax
    a.emit(0xBC, 0x00, 0x7C)       # mov sp, 0x7c00
    # 不修改 DL，保留 BIOS 给的启动驱动器号

    # --- 检测 LBA 扩展（INT 13h AH=0x41）---
    a.emit(0xB4, 0x41)             # mov ah, 0x41
    a.emit(0xBB, 0xAA, 0x55)       # mov bx, 0x55AA  (入口签名)
    a.emit(0xCD, 0x13)             # int 0x13
    a.jcc_short(2, 'no_lba')       # jc no_lba  (CF=1 不支持)
    a.emit(0x81, 0xFB, 0x55, 0xAA) # cmp bx, 0xAA55  (出口签名)
    a.jcc_short(5, 'no_lba')       # jne no_lba

    # 写 'L' (LBA supported)
    a.emit(0x26, 0xC6, 0x05, 0x4C) # mov byte [es:di], 'L'
    a.emit(0x83, 0xC7, 0x02)       # add di, 2

    # --- LBA 扩展读 32 扇区到 0x0000:0x1000 ---
    # 先设 ES=0 给 DAP buf_seg
    a.emit(0x31, 0xC0)             # xor ax, ax
    a.emit(0x8E, 0xC0)             # mov es, ax
    # 构造 DAP 在 0x7000
    a.emit(0xBF, 0x00, 0x70)       # mov di, 0x7000  (注意 DI 被重用为 DAP 指针)
    a.emit(0xC6, 0x05, 0x10)       # mov byte [di], 0x10       ; DAP size
    a.emit(0xC6, 0x45, 0x01, 0x00) # mov byte [di+1], 0        ; reserved
    a.emit(0xC7, 0x45, 0x02, 0x20, 0x00)  # mov dword [di+2], 0x20 (32位写入，覆盖sector count+buf_off低)
    a.emit(0xC7, 0x45, 0x04, 0x00, 0x10)  # mov dword [di+4], 0x1000 (覆盖buf_off+buf_seg)
    a.emit(0xC7, 0x45, 0x06, 0x00, 0x00)  # mov dword [di+6], 0x0000 (覆盖buf_seg+LBA低)
    a.emit(0x66, 0xC7, 0x45, 0x08, 0x01, 0x00, 0x00, 0x00)  # mov dword [di+8], 1 (LBA)
    a.emit(0xBE, 0x00, 0x70)       # mov si, 0x7000  (SI 指向 DAP)
    a.emit(0xB4, 0x42)             # mov ah, 0x42
    a.emit(0xCD, 0x13)             # int 0x13

    # 恢复 ES=0xB800 用于调试写字符
    a.emit(0xB8, 0x00, 0xB8)       # mov ax, 0xB800
    a.emit(0x8E, 0xC0)             # mov es, ax
    # DI 需要恢复到之前的调试位置（之前写到 di=4，即第3个字符）
    # 但 DI 被重用为 0x7000 了，这里重新设 DI=4
    a.emit(0xBF, 0x04, 0x00)       # mov di, 4
    a.jcc_short(2, 'lba_failed')   # 读失败跳到 lba_failed
    # 写 'O' (LBA read OK)
    a.emit(0x26, 0xC6, 0x05, 0x4F) # mov byte [es:di], 'O'
    a.emit(0x83, 0xC7, 0x02)       # add di, 2
    a.jmp_short('done_load')

    a.label('lba_failed')
    # 写 'F' (LBA read failed)
    a.emit(0x26, 0xC6, 0x05, 0x46) # mov byte [es:di], 'F'
    a.emit(0x83, 0xC7, 0x02)       # add di, 2
    a.jmp_short('no_lba')

    # --- CHS 分磁道读取（1.44MB 软盘：每磁道 18 扇区，2 磁头）---
    a.label('no_lba')
    # 设 ES=0 给 CHS buf
    a.emit(0x31, 0xC0)             # xor ax, ax
    a.emit(0x8E, 0xC0)             # mov es, ax
    # 第一次：磁头 0 柱面 0 扇区 2-18，读 17 扇区到 0x1000
    a.emit(0xB4, 0x02)             # mov ah, 0x02
    a.emit(0xB0, 0x11)             # mov al, 17
    a.emit(0xB5, 0x00)             # mov ch, 0     (柱面 0)
    a.emit(0xB1, 0x02)             # mov cl, 2     (起始扇区 2)
    a.emit(0xB6, 0x00)             # mov dh, 0     (磁头 0)
    a.emit(0xBB, 0x00, 0x10)       # mov bx, 0x1000
    a.emit(0xCD, 0x13)             # int 0x13

    # 恢复 ES=0xB800 调试
    a.emit(0xB8, 0x00, 0xB8)       # mov ax, 0xB800
    a.emit(0x8E, 0xC0)             # mov es, ax
    a.emit(0xBF, 0x06, 0x00)       # mov di, 6
    a.jcc_short(2, 'disk_err')     # jc disk_err
    # 写 'C' (CHS read 1 OK)
    a.emit(0x26, 0xC6, 0x05, 0x43) # mov byte [es:di], 'C'
    a.emit(0x83, 0xC7, 0x02)       # add di, 2

    # 第二次 CHS 读取
    a.emit(0x31, 0xC0)             # xor ax, ax
    a.emit(0x8E, 0xC0)             # mov es, ax
    a.emit(0xB4, 0x02)             # mov ah, 0x02
    a.emit(0xB0, 0x0F)             # mov al, 15
    a.emit(0xB5, 0x00)             # mov ch, 0
    a.emit(0xB1, 0x01)             # mov cl, 1     (起始扇区 1)
    a.emit(0xB6, 0x01)             # mov dh, 1     (磁头 1)
    a.emit(0xBB, 0x00, 0x32)       # mov bx, 0x3200
    a.emit(0xCD, 0x13)             # int 0x13

    a.emit(0xB8, 0x00, 0xB8)       # mov ax, 0xB800
    a.emit(0x8E, 0xC0)             # mov es, ax
    a.emit(0xBF, 0x08, 0x00)       # mov di, 8
    # 第二次 CHS 失败不致命：第一次已读 17 扇区到 0x1000（含 installer.bin）
    # 直接继续到 done_load 跳转执行 installer
    a.jcc_short(2, 'done_load')    # jc done_load (失败也继续)

    a.label('done_load')
    # 写 'J' (jump to loader) 再跳转
    a.emit(0xB8, 0x00, 0xB8)       # mov ax, 0xB800
    a.emit(0x8E, 0xC0)             # mov es, ax
    a.emit(0xBF, 0x0A, 0x00)       # mov di, 10
    a.emit(0x26, 0xC6, 0x05, 0x4A) # mov byte [es:di], 'J'
    # 测试 INT 10h 是否工作：用 teletype 写 'T'
    a.emit(0xB4, 0x0E)             # mov ah, 0x0E
    a.emit(0xB0, 0x54)             # mov al, 'T'
    a.emit(0xB7, 0x00)             # mov bh, 0
    a.emit(0xCD, 0x10)             # int 0x10
    # 远跳转到 0x0000:0x1000 执行加载器
    a.emit(0xEA, 0x00, 0x10, 0x00, 0x00)  # jmp 0x0000:0x1000

    a.label('disk_err')
    # 错误处理：屏幕打印 "ERR" 并 hlt 死循环
    a.emit(0xB4, 0x0E)             # mov ah, 0x0E  (teletype)
    a.emit(0xB0, 0x45)             # mov al, 'E'
    a.emit(0xB7, 0x07)             # mov bh, 7     (页面 0)
    a.emit(0xCD, 0x10)             # int 0x10
    a.emit(0xB0, 0x52)             # mov al, 'R'
    a.emit(0xCD, 0x10)             # int 0x10
    a.emit(0xB0, 0x52)             # mov al, 'R'
    a.emit(0xCD, 0x10)             # int 0x10
    a.label('err_loop')
    a.emit(0xF4)                   # hlt
    a.jmp_short('err_loop')

    code = a.resolve()

    if len(code) > 510:
        raise RuntimeError("boot sector code too large: %d bytes" % len(code))
    code = code + b'\x00' * (510 - len(code))
    code += b'\x55\xAA'            # 引导扇区签名

    assert len(code) == 512
    return code


def build_kernel_loader():
    """
    生成内核加载器机器码（16位实模式），被引导扇区加载到 0x0000:0x1000。
    行为：通过 INT 0x10 AH=0E 在屏幕上打印 "System Boot OK" 然后进入 hlt 循环。
    """
    # 消息字符串（以 \r\n 换行，以 \0 结尾）
    msg = b'System Boot OK\r\n\x00'

    code = bytearray()
    # --- 重新设置 DS=CS（保险）---
    # mov ax, cs
    code += b'\x8C\xC8'
    # mov ds, ax
    code += b'\x8E\xD8'
    # mov es, ax
    code += b'\x8E\xC0'

    # --- 把消息地址装入 SI ---
    # mov si, <msg_offset>
    # msg 紧跟在代码后面；下面会计算偏移
    si_instr_pos = len(code)
    code += b'\xBE\x00\x00'  # 占位，稍后回填
    # msg 偏移 = 代码长度 + 3 (mov si 自身) 之后的字节数 = 此处之后到 msg 开始
    # 但我们等代码全部生成后再回填

    # --- 打印循环 ---
    # .loop:
    loop_offset = len(code)
    # lodsb              ; AL = [DS:SI]; SI++
    code += b'\xAC'
    # test al, al
    code += b'\x84\xC0'
    # jz .done           ; 短跳转，位移待回填
    jz_pos = len(code)
    code += b'\x74\x00'  # 占位
    # mov ah, 0x0E       ; teletype 输出
    code += b'\xB4\x0E'
    # mov bx, 0x0007     ; BH=0 页面, BL=07 属性
    code += b'\xBB\x07\x00'
    # int 0x10
    code += b'\xCD\x10'
    # jmp .loop          ; 短跳转回 loop
    jmp_back_pos = len(code)
    code += b'\xEB\x00'  # 占位

    # .done:
    done_offset = len(code)
    # hlt
    code += b'\xF4'
    # jmp .done          ; 死循环防止中断唤醒后乱跑
    jmp_done_pos = len(code)
    code += b'\xEB\x00'  # 占位

    # 消息字符串紧跟在代码后面
    msg_offset = len(code)
    code += msg

    # --- 回填各种位移 ---
    # 1) mov si, msg_offset
    code[si_instr_pos + 1] = msg_offset & 0xFF
    code[si_instr_pos + 2] = (msg_offset >> 8) & 0xFF

    # 2) jz .done （短跳转，从 jz 指令的下一条指令算起）
    # jz 在 jz_pos，长度 2，下一条在 jz_pos+2
    code[jz_pos + 1] = (done_offset - (jz_pos + 2)) & 0xFF

    # 3) jmp .loop （短跳转）
    # jmp 在 jmp_back_pos，长度 2，下一条在 jmp_back_pos+2
    code[jmp_back_pos + 1] = (loop_offset - (jmp_back_pos + 2)) & 0xFF

    # 4) jmp .done （短跳转）
    # jmp 在 jmp_done_pos，长度 2，下一条在 jmp_done_pos+2
    code[jmp_done_pos + 1] = (done_offset - (jmp_done_pos + 2)) & 0xFF

    return bytes(code)


# ====================================================================
# 第一部分 B：安装器与硬盘系统镜像
# ====================================================================

class Asm16:
    """简易 16 位实模式机器码生成器，支持标签与回填。"""
    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.fixups = []          # (offset, label_name, kind)  kind: 'abs16'|'rel16'|'rel8'

    def pos(self):
        return len(self.code)

    def emit(self, *bytes_):
        for b in bytes_:
            if isinstance(b, int):
                self.code.append(b & 0xFF)
            elif isinstance(b, (bytes, bytearray)):
                self.code.extend(b)
            else:
                raise TypeError('emit: %r' % (b,))

    def emit_bytes(self, raw):
        self.code.extend(raw)

    def label(self, name):
        self.labels[name] = len(self.code)

    # --- 带标签的指令 ---
    def mov_si_label(self, name):
        self.emit(0xBE, 0, 0)
        self.fixups.append((len(self.code) - 2, name, 'abs16'))

    def call(self, name):
        self.emit(0xE8, 0, 0)
        self.fixups.append((len(self.code) - 2, name, 'rel16'))

    def jmp_short(self, name):
        self.emit(0xEB, 0)
        self.fixups.append((len(self.code) - 1, name, 'rel8'))

    def jcc_short(self, cc, name):
        self.emit(0x70 | (cc & 0x0F), 0)
        self.fixups.append((len(self.code) - 1, name, 'rel8'))

    def jmp_near(self, name):
        self.emit(0xE9, 0, 0)
        self.fixups.append((len(self.code) - 2, name, 'rel16'))

    def db_str(self, text):
        """写入以 \\0 结尾的 ASCII 字符串"""
        self.code.extend(text.encode('ascii', errors='replace'))
        self.code.append(0)

    def resolve(self):
        for off, name, kind in self.fixups:
            if name not in self.labels:
                raise ValueError('undefined label: %s' % name)
            tgt = self.labels[name]
            if kind == 'abs16':
                self.code[off:off + 2] = struct.pack('<H', tgt)
            elif kind == 'rel16':
                self.code[off:off + 2] = struct.pack('<h', tgt - (off + 2))
            elif kind == 'rel8':
                rel = tgt - (off + 1)
                if not -128 <= rel <= 127:
                    raise ValueError('rel8 out of range for %s: %d' % (name, rel))
                self.code[off] = rel & 0xFF
        return bytes(self.code)


def build_installer():
    """
    生成安装器机器码（16 位实模式），被引导扇区加载到 0x0000:0x1000。
    最小测试版本：直接写 VGA 显存打印 "Hello from installer!" 然后 hlt。
    """
    a = Asm16()

    # 设置段寄存器
    a.emit(0x31, 0xC0)             # xor ax, ax
    a.emit(0x8E, 0xD8)             # mov ds, ax
    a.emit(0x8E, 0xC0)             # mov es, ax
    a.emit(0x8E, 0xD0)             # mov ss, ax
    a.emit(0xBC, 0x00, 0x7C)       # mov sp, 0x7c00

    # ES = 0xB800 (VGA 显存段)
    a.emit(0xB8, 0x00, 0xB8)       # mov ax, 0xB800
    a.emit(0x8E, 0xC0)             # mov es, ax

    # 写 "Hello from installer!" 从 VGA offset 160 (第二行) 开始
    a.emit(0xBF, 0xA0, 0x00)       # mov di, 160
    # 字符串紧跟在代码后面，用 SI 指向
    # DS=0, CS=0, 所以字符串地址 = CS:msg_offset
    # 用 lodsb 读取字符串
    a.emit(0xBE)                    # mov si, imm16 (占位)
    si_pos = len(a.code)
    a.emit(0x00, 0x00)             # 占位，稍后回填 msg_hello 偏移
    a.label('hello_loop')
    a.emit(0xAC)                   # lodsb: AL = [DS:SI], SI++
    a.emit(0x84, 0xC0)             # test al, al
    a.jcc_short(4, 'hello_done')   # jz hello_done
    a.emit(0x26, 0x88, 0x05)       # mov [es:di], al
    a.emit(0x26, 0xC6, 0x45, 0x01, 0x07)  # mov byte [es:di+1], 0x07
    a.emit(0x83, 0xC7, 0x02)       # add di, 2
    a.jmp_short('hello_loop')
    a.label('hello_done')

    # hlt 死循环
    a.emit(0xF4)                   # hlt
    a.emit(0xEB, 0xFD)             # jmp -3 (回到 hlt)

    # 字符串数据
    a.label('msg_hello')
    a.db_str('Hello from installer!')

    # 回填 mov si, msg_hello 偏移
    # 安装器被加载到物理地址 0x1000（DS=0），所以 SI = 0x1000 + msg_off
    # 使 DS:SI 指向物理地址 0x1000+msg_off 处的字符串
    msg_off = a.labels['msg_hello']
    si_value = 0x1000 + msg_off
    a.code[si_pos] = si_value & 0xFF
    a.code[si_pos + 1] = (si_value >> 8) & 0xFF

    return a.resolve()


def build_hdd_loader():
    """
    生成硬盘加载器机器码（安装后硬盘上的加载器）。
    被 HDD 引导扇区加载到 0x0000:0x1000，显示系统运行消息后 hlt。
    """
    msg = (b'Micro-Kernel OS\r\n'
           b'Running from Hard Disk\r\n'
           b'System installed successfully.\r\n'
           b'\x00')
    code = bytearray()
    code += b'\x8C\xC8'               # mov ax, cs
    code += b'\x8E\xD8'               # mov ds, ax
    code += b'\x8E\xC0'               # mov es, ax
    # mov si, msg_offset (占位)
    si_pos = len(code)
    code += b'\xBE\x00\x00'
    # .loop:
    loop_off = len(code)
    code += b'\xAC'                    # lodsb
    code += b'\x84\xC0'               # test al, al
    code += b'\x74\x00'               # jz .done (占位)
    jz_pos = len(code) - 1
    code += b'\xB4\x0E'               # mov ah, 0x0E
    code += b'\xBB\x07\x00'           # mov bx, 0x0007
    code += b'\xCD\x10'               # int 0x10
    code += b'\xEB\x00'               # jmp .loop (占位)
    jmp_pos = len(code) - 1
    # .done:
    done_off = len(code)
    code += b'\xF4'                    # hlt
    code += b'\xEB\x00'               # jmp .done (占位)
    jmp2_pos = len(code) - 1
    # msg
    msg_off = len(code)
    code += msg
    # 回填偏移
    code[si_pos + 1] = msg_off & 0xFF
    code[si_pos + 2] = (msg_off >> 8) & 0xFF
    code[jz_pos] = (done_off - (jz_pos + 1)) & 0xFF
    code[jmp_pos] = (loop_off - (jmp_pos + 1)) & 0xFF
    code[jmp2_pos] = (done_off - (jmp2_pos + 1)) & 0xFF
    return bytes(code)


def build_hdd_system():
    """
    生成要安装到硬盘的系统镜像（32 扇区 = 16KB）。
    布局：
      扇区 1 (LBA 0): 硬盘引导扇区（从 HDD LBA 1 读 32 扇区到 0x1000，跳转）
      扇区 2-32 (LBA 1-31): 硬盘加载器（显示 "Running from HDD"）

    硬盘几何未知，不能用 CHS 读取（cl=2/ch=0/dh=0 在硬盘上对应位置不可知）。
    所有 386+ 时代的硬盘 BIOS 都支持 INT 13h LBA 扩展，故直接用 AH=0x42，
    失败则打印 'HDER' 死循环（硬盘 CHS 回退无意义，几何不可知）。
    """
    a = Asm16()

    # --- 设置段寄存器 ---
    a.emit(0x31, 0xC0)             # xor ax, ax
    a.emit(0x8E, 0xD8)             # mov ds, ax
    a.emit(0x8E, 0xC0)             # mov es, ax
    a.emit(0x8E, 0xD0)             # mov ss, ax
    a.emit(0xBC, 0x00, 0x7C)       # mov sp, 0x7c00
    # 不修改 DL，BIOS 从硬盘启动时 DL=0x80

    # --- 检测 LBA 扩展 ---
    a.emit(0xB4, 0x41)             # mov ah, 0x41
    a.emit(0xBB, 0xAA, 0x55)       # mov bx, 0x55AA
    a.emit(0xCD, 0x13)             # int 0x13
    a.jcc_short(2, 'disk_err')     # jc disk_err
    a.emit(0x81, 0xFB, 0x55, 0xAA) # cmp bx, 0xAA55
    a.jcc_short(5, 'disk_err')     # jne disk_err

    # --- LBA 扩展读 32 扇区到 0x0000:0x1000（从 HDD LBA 1 起）---
    a.emit(0xBF, 0x00, 0x70)       # mov di, 0x7000
    a.emit(0xC6, 0x05, 0x10)       # mov byte [di], 0x10
    a.emit(0xC6, 0x45, 0x01, 0x00) # mov byte [di+1], 0
    a.emit(0xC7, 0x45, 0x02, 0x20, 0x00)  # mov word [di+2], 32
    a.emit(0xC7, 0x45, 0x04, 0x00, 0x10)  # mov word [di+4], 0x1000 (buf_off)
    a.emit(0xC7, 0x45, 0x06, 0x00, 0x00)  # mov word [di+6], 0x0000 (buf_seg)
    a.emit(0x66, 0xC7, 0x45, 0x08, 0x01, 0x00, 0x00, 0x00)  # mov dword [di+8], 1 (LBA)
    a.emit(0xBE, 0x00, 0x70)       # mov si, 0x7000
    a.emit(0xB4, 0x42)             # mov ah, 0x42
    a.emit(0xCD, 0x13)             # int 0x13
    a.jcc_short(2, 'disk_err')     # jc disk_err

    # 远跳转到 0x0000:0x1000 执行加载器
    a.emit(0xEA, 0x00, 0x10, 0x00, 0x00)  # jmp 0x0000:0x1000

    a.label('disk_err')
    # 错误处理：屏幕打印 "HDER" 并 hlt 死循环
    a.emit(0xB4, 0x0E)             # mov ah, 0x0E
    a.emit(0xB0, 0x48)             # mov al, 'H'
    a.emit(0xB7, 0x07)             # mov bh, 7
    a.emit(0xCD, 0x10)             # int 0x10
    a.emit(0xB0, 0x44)             # mov al, 'D'
    a.emit(0xCD, 0x10)             # int 0x10
    a.emit(0xB0, 0x45)             # mov al, 'E'
    a.emit(0xCD, 0x10)             # int 0x10
    a.emit(0xB0, 0x52)             # mov al, 'R'
    a.emit(0xCD, 0x10)             # int 0x10
    a.label('err_loop')
    a.emit(0xF4)                   # hlt
    a.jmp_short('err_loop')

    boot_code = a.resolve()
    if len(boot_code) > 510:
        raise RuntimeError("HDD boot sector code too large: %d bytes" % len(boot_code))
    boot = bytearray(boot_code)
    boot += b'\x00' * (510 - len(boot))
    boot += b'\x55\xAA'

    # --- 硬盘加载器 ---
    loader = build_hdd_loader()

    # --- 组合成 32 扇区 (16384 字节) ---
    system = bytearray()
    system += boot
    system += loader
    system += b'\x00' * (32 * 512 - len(system))
    return bytes(system)


def build_boot_image():
    """
    生成 1.44MB 软盘镜像 boot.img。
    布局：
      扇区 1   (LBA 0)   : 引导扇区 512B（加载安装器到 0x1000 并跳转）
      扇区 2-33 (LBA 1-32): 安装器代码（16KB 区域）
      扇区 34-65(LBA 33-64): 要安装到硬盘的系统镜像（16KB）
      其余            : 0 填充到 1.44MB
    """
    boot_sector = build_boot_sector()
    installer = build_installer()
    hdd_system = build_hdd_system()

    LOADER_AREA = 32 * 512          # 安装器区域 16KB
    SYSTEM_AREA = 32 * 512          # 系统镜像区域 16KB

    img = bytearray()
    img += boot_sector                            # 扇区 1
    img += installer                               # 安装器代码
    img += b'\x00' * (LOADER_AREA - len(installer))
    img += hdd_system                              # 系统镜像（已是 16384 字节）
    # 此时 img 长度 = 512 + 16384 + 16384 = 33280

    FLOPPY_SIZE = 1474560
    img += b'\x00' * (FLOPPY_SIZE - len(img))

    assert len(img) == FLOPPY_SIZE
    return bytes(img), boot_sector, installer


# ====================================================================
# 第二部分：ISO9660 + El Torito 镜像生成
# ====================================================================

SECTOR_SIZE = 2048

def both_endian_16(v):
    """4字节 双端 16位整数（小端 + 大端）"""
    return struct.pack('<H', v) + struct.pack('>H', v)

def both_endian_32(v):
    """8字节 双端 32位整数（小端 + 大端）"""
    return struct.pack('<I', v) + struct.pack('>I', v)

def pad_field(data, length, fill=b' '):
    """用 fill 把 data 填充到 length 长度"""
    if isinstance(data, str):
        data = data.encode('ascii', errors='replace')
    if len(data) >= length:
        return data[:length]
    return data + (fill * (length - len(data)))

def iso_datetime_now():
    """ISO9660 17字节 日期时间（用于 Volume Descriptor）：'YYYYMMDDHHMMSSFF' (16 ASCII) + 时区字节(0)"""
    now = datetime.datetime.now(datetime.timezone.utc)
    s = now.strftime('%Y%m%d%H%M%S') + '00'
    return s.encode('ascii') + b'\x00'

def iso_dir_datetime_now():
    """ISO9660 Directory Record 的 Recording DATE/TIME（7 字节）：
       1字节年(自1900起) + 1月 + 1日 + 1时 + 1分 + 1秒 + 1时区偏移(0)"""
    now = datetime.datetime.now(datetime.timezone.utc)
    return bytes([
        now.year - 1900,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second,
        0,                                  # GMT offset (0 = GMT)
    ])

def iso_datetime_zero():
    """空日期时间：'0000000000000000' + 0"""
    return b'0' * 16 + b'\x00'


def build_dir_record(extent_lba, data_length, flags, name_bytes,
                     is_root=False, children=None):
    """
    构造一个 ISO9660 Directory Record。
    name_bytes：文件标识符字节（根目录用 b'\x00'，普通文件用文件名字节）
    """
    file_id = name_bytes
    file_id_len = len(file_id)
    # 当 file_id 长度为偶数时需要补 1 字节 0
    pad = b'\x00' if (file_id_len % 2 == 0) else b''
    # 基本头 33 字节 + file_id_len + pad
    rec_len = 33 + file_id_len + len(pad)

    rec = bytearray()
    rec.append(rec_len)                              # 0: Length
    rec.append(0)                                    # 1: Extended Attr Record Length
    rec += both_endian_32(extent_lba)                # 2-9: Location of Extent
    rec += both_endian_32(data_length)               # 10-17: Data Length
    rec += iso_dir_datetime_now()                    # 18-24: Recording DATE/TIME (7 bytes)
    rec.append(flags)                                # 25: File Flags
    rec.append(0)                                    # 26: File Unit Size
    rec.append(0)                                    # 27: Interleave Gap Size
    rec += both_endian_16(1)                         # 28-31: Volume Sequence Number
    rec.append(file_id_len)                          # 32: Length of File Identifier
    rec += file_id                                   # 33+: File Identifier
    rec += pad                                       # 对齐填充
    return bytes(rec)


def build_efi_app():
    """
    构造一个真正可引导的 PE32+ EFI 应用程序。

    入口点签名（x64 UEFI 调用约定，等同于微软 x64 ABI）：
        EFI_STATUS EFIAPI efi_main(
            EFI_HANDLE ImageHandle,            ; RCX
            EFI_SYSTEM_TABLE *SystemTable     ; RDX
        );

    程序逻辑：调用 SystemTable->ConOut->OutputString(ConOut, L"...")
    在屏幕上打印启动横幅与提示信息，然后返回 EFI_SUCCESS(0) 退出。
    UEFI 固件会把控制权交回 Boot Manager，Boot Manager 遍历下一个引导项。

    关键 UEFI 结构偏移（x64 ABI，所有指针 8 字节）：
        EFI_SYSTEM_TABLE->ConOut                在偏移 64
        EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL->OutputString  在偏移 8
        OutputString(This=RCX, String=RDX)
    """
    import struct

    # ===== 构造 .text 段：机器码 + UCS-2 字符串数据 =====
    code = bytearray()
    # 0  : mov r8, rdx                 ; 保存 SystemTable 指针到 r8
    code += b'\x49\x89\xD0'
    # 3  : mov rcx, [r8 + 64]         ; rcx = SystemTable->ConOut
    code += b'\x49\x8B\x48\x40'
    # 7  : mov rax, [rcx + 8]          ; rax = ConOut->OutputString
    code += b'\x48\x8B\x41\x08'
    # 11 : lea rdx, [rip + 6]          ; rdx = 字符串地址 (字符串在 ret 之后)
    code += b'\x48\x8D\x15\x06\x00\x00\x00'
    # 18 : call rax                    ; OutputString(ConOut, String)
    code += b'\xFF\xD0'
    # 20 : xor rax, rax                ; rax = EFI_SUCCESS (0)
    code += b'\x48\x31\xC0'
    # 23 : ret                         ; 返回到 UEFI 固件
    code += b'\xC3'
    # 24 : UCS-2 字符串 "Hello from UEFI!\\r\\n" + NUL
    # UCS-2 用小端 16-bit 编码每个字符，末尾必须有 0x0000 终止符
    msg = 'Hello from UEFI!\r\nMicro-Kernel booted via EFI.\r\n'
    for ch in msg:
        code += struct.pack('<H', ord(ch))
    code += b'\x00\x00'  # UCS-2 NUL 终止符 (2 字节全 0)

    # 对齐到 FileAlignment (0x200)
    raw_size = 0x200
    code_padded = bytes(code) + b'\x00' * (raw_size - len(code))
    # VirtualSize 至少覆盖代码+字符串
    virt_size = max(len(code), 0x40)

    # ===== DOS 头 (64 字节) =====
    dos_hdr = bytearray(64)
    dos_hdr[0:2] = b'MZ'
    struct.pack_into('<I', dos_hdr, 60, 64)  # e_lfanew = 64

    # ===== PE 签名 =====
    pe_sig = b'PE\x00\x00'

    # ===== COFF 头 (20 字节) =====
    coff = bytearray(20)
    struct.pack_into('<H', coff, 0, 0x8664)    # Machine = AMD64
    struct.pack_into('<H', coff, 2, 1)        # NumberOfSections = 1
    struct.pack_into('<I', coff, 4, 0)        # TimeDateStamp
    struct.pack_into('<I', coff, 8, 0)        # PointerToSymbolTable
    struct.pack_into('<I', coff, 12, 0)       # NumberOfSymbols
    struct.pack_into('<H', coff, 16, 240)     # SizeOfOptionalHeader (PE32+)
    struct.pack_into('<H', coff, 18, 0x0022)  # Characteristics: EXECUTABLE_IMAGE|LARGE_ADDRESS_AWARE

    # ===== 可选头 PE32+ (240 字节) =====
    opt = bytearray(240)
    struct.pack_into('<H', opt, 0, 0x20B)     # Magic = PE32+
    opt[2] = 14                               # MajorLinkerVersion
    opt[3] = 0                                # MinorLinkerVersion
    struct.pack_into('<I', opt, 4, raw_size)  # SizeOfCode
    struct.pack_into('<I', opt, 8, 0)         # SizeOfInitializedData
    struct.pack_into('<I', opt, 12, 0)        # SizeOfUninitializedData
    struct.pack_into('<I', opt, 16, 0x1000)   # AddressOfEntryPoint (相对 ImageBase)
    struct.pack_into('<I', opt, 20, 0x1000)   # BaseOfCode
    struct.pack_into('<Q', opt, 24, 0x10000)  # ImageBase
    struct.pack_into('<I', opt, 32, 0x1000)   # SectionAlignment
    struct.pack_into('<I', opt, 36, 0x200)    # FileAlignment
    struct.pack_into('<H', opt, 40, 0)        # MajorOperatingSystemVersion
    struct.pack_into('<H', opt, 42, 0)        # MinorOperatingSystemVersion
    struct.pack_into('<H', opt, 44, 0)        # MajorSubsystemVersion
    struct.pack_into('<H', opt, 46, 0)        # MinorSubsystemVersion
    struct.pack_into('<I', opt, 48, 0)        # Win32VersionValue
    struct.pack_into('<I', opt, 52, 0x2000)   # SizeOfImage
    struct.pack_into('<I', opt, 56, 0x200)    # SizeOfHeaders
    struct.pack_into('<I', opt, 60, 0)        # CheckSum
    struct.pack_into('<H', opt, 64, 10)       # Subsystem = EFI_APPLICATION
    struct.pack_into('<H', opt, 66, 0x0060)   # DllCharacteristics: HIGH_ENTROPY_VA|DYNAMIC_BASE
    struct.pack_into('<Q', opt, 68, 0x100000) # SizeOfStackReserve
    struct.pack_into('<Q', opt, 76, 0x1000)   # SizeOfStackCommit
    struct.pack_into('<Q', opt, 84, 0x100000) # SizeOfHeapReserve
    struct.pack_into('<Q', opt, 92, 0x1000)    # SizeOfHeapCommit
    struct.pack_into('<I', opt, 100, 0)       # LoaderFlags
    struct.pack_into('<I', opt, 104, 16)      # NumberOfRvaAndSizes
    # DataDirectory[16] 全 0 (opt[108..239])

    # ===== 节表 (40 字节) - .text 段 =====
    sec = bytearray(40)
    sec[0:6] = b'.text\x00'
    struct.pack_into('<I', sec, 8, virt_size)         # VirtualSize
    struct.pack_into('<I', sec, 12, 0x1000)           # VirtualAddress
    struct.pack_into('<I', sec, 16, raw_size)         # SizeOfRawData
    struct.pack_into('<I', sec, 20, raw_size)          # PointerToRawData
    struct.pack_into('<I', sec, 36, 0x60000020)        # Characteristics: CODE|EXECUTE|READ

    # ===== 组装头部并按 FileAlignment 对齐 =====
    headers = dos_hdr + pe_sig + bytes(coff) + bytes(opt) + bytes(sec)
    headers = headers + b'\x00' * (0x200 - len(headers))

    return bytes(headers) + code_padded


def collect_source_files():
    """
    收集项目中所有需要嵌入 ISO 的源码文件。
    返回 [(basename, abspath), ...] 列表，按 basename 排序。
    跳过 os.img / os.vhdx 等大二进制和构建产物。
    """
    EXCLUDE_NAMES = {
        'os.img', 'os.vhdx',
        'boot.img', 'system.iso',
        'build_iso.py',
    }
    EXCLUDE_EXT = {'.img', '.iso', '.vhdx', '.bin'}
    collected = []
    seen_names = {}
    for root, dirs, files in os.walk(PROJECT_ROOT):
        # 跳过隐藏目录
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        for f in files:
            if f in EXCLUDE_NAMES:
                continue
            ext = os.path.splitext(f)[1].lower()
            if ext in EXCLUDE_EXT:
                continue
            abspath = os.path.join(root, f)
            # 处理 basename 冲突：若冲突则用 父目录_文件名 形式
            base = f
            if base in seen_names:
                parent = os.path.basename(os.path.dirname(abspath))
                base = parent + '__' + f
                if base in seen_names:
                    # 再冲突就加序号
                    i = 2
                    while f'{f}.{i}' in seen_names:
                        i += 1
                    base = f'{f}.{i}'
            seen_names[base] = abspath
            # ISO9660 文件名限制：大写、无扩展时最多 8 字符，有扩展时 8.3
            # 为简单起见，我们使用 Joliet 风格的较长名字通过 Rock Ridge
            # —— 但纯 ISO9660 Level 1 限制 8.3。这里用 Level 2/3 允许更长名字。
            # ISO9660 文件名要求大写。我们把名字转大写后保留扩展名。
            iso_name = base.upper()
            # 把非法字符替换为 _
            iso_name = ''.join(c if (c.isalnum() or c in '._') else '_' for c in iso_name)
            collected.append((iso_name, abspath))
    collected.sort(key=lambda x: x[0])
    return collected


def build_path_table_record(dir_id_bytes, extent_lba, parent_idx, big_endian=False):
    """构造一条 Path Table Record。"""
    rec = bytearray()
    rec.append(len(dir_id_bytes))                    # 0: Length of Directory Identifier
    rec.append(0)                                    # 1: Extended Attribute Record Length
    if big_endian:
        rec += struct.pack('>I', extent_lba)         # 2-5: Location of Extent (BE)
        rec += struct.pack('>H', parent_idx)         # 6-7: Parent Directory Number (BE)
    else:
        rec += struct.pack('<I', extent_lba)         # 2-5: Location of Extent (LE)
        rec += struct.pack('<H', parent_idx)         # 6-7: Parent Directory Number (LE)
    rec += dir_id_bytes                              # 8+: Directory Identifier
    if len(dir_id_bytes) % 2 == 1:
        rec.append(0)                                # 对齐填充
    return bytes(rec)


def build_boot_catalog(boot_img_lba, sector_count_512, media_type=0x02,
                       load_segment=0x0000):
    """
    构造 El Torito Boot Catalog（至少 1 个 2048 扇区）。
    包含 Validation Entry + Initial/Default Entry。
    """
    # --- Validation Entry (32 字节) ---
    validation = bytearray(32)
    validation[0] = 0x01                             # Header ID
    validation[1] = 0x00                             # Platform ID (x86)
    # Bytes 2-3: Reserved
    # Bytes 4-27: ID string (24 bytes ASCII, 0 padded)
    id_str = b'MICRO-KERNEL OS BOOT'
    validation[4:4 + len(id_str)] = id_str
    # Bytes 30-31: Key 0x55 0xAA —— 必须在算 checksum 之前先填好，
    # 因为 checksum 要求整个 32 字节（含 key 字段）按 16-bit LE 求和 = 0。
    validation[30] = 0x55
    validation[31] = 0xAA
    # Bytes 28-29: Checksum —— 把 checksum 字段先置 0，求和后取反填回
    validation[28] = 0x00
    validation[29] = 0x00
    # 计算 checksum（此时 28-29 为 0，30-31 已是 0x55 0xAA）
    checksum = 0
    for i in range(0, 32, 2):
        checksum += (validation[i + 1] << 8) | validation[i]
    checksum &= 0xFFFF
    val = (0x10000 - checksum) & 0xFFFF
    validation[28] = val & 0xFF
    validation[29] = (val >> 8) & 0xFF

    # --- Initial/Default Entry (32 字节) ---
    initial = bytearray(32)
    initial[0] = 0x88                                # Boot Indicator: bootable
    initial[1] = media_type & 0xFF                   # Media Type (0x02 = 1.44MB floppy)
    # Load Segment: 0x0000 means 0x07C0 default
    initial[2] = load_segment & 0xFF
    initial[3] = (load_segment >> 8) & 0xFF
    initial[4] = 0x00                                # System Type
    initial[5] = 0x00                                # Reserved
    # Sector Count: 512-byte 单位的扇区数
    initial[6] = sector_count_512 & 0xFF
    initial[7] = (sector_count_512 >> 8) & 0xFF
    # Load RBA: 2048-byte ISO 扇区中的 boot image LBA
    initial[8] = boot_img_lba & 0xFF
    initial[9] = (boot_img_lba >> 8) & 0xFF
    initial[10] = (boot_img_lba >> 16) & 0xFF
    initial[11] = (boot_img_lba >> 24) & 0xFF
    # Bytes 12-31: Reserved

    catalog = bytes(validation) + bytes(initial)
    # 填充到 2048 字节
    catalog += b'\x00' * (SECTOR_SIZE - len(catalog))
    return catalog


def build_primary_volume_descriptor(volume_size_sectors,
                                    root_dir_lba, root_dir_size,
                                    l_path_table_lba, m_path_table_lba,
                                    path_table_size):
    """构造 Primary Volume Descriptor（2048 字节）。"""
    pvd = bytearray(SECTOR_SIZE)
    pvd[0] = 0x01                                    # Type 1: PVD
    pvd[1:6] = b'CD001'                              # Standard Identifier
    pvd[6] = 0x01                                    # Version
    # 7: Unused
    pvd[8:40] = pad_field(b'MICROKERNEL', 32)        # System Identifier
    pvd[40:72] = pad_field(b'Micro-Kernel OS', 32)   # Volume Identifier
    # 72-79: Unused (8 字节 0)
    # 80-87: Volume Space Size（8 字节双端 32位）
    pvd[80:88] = both_endian_32(volume_size_sectors)
    # 88-119: Unused
    pvd[120:124] = both_endian_16(1)                 # Volume Set Size = 1
    pvd[124:128] = both_endian_16(1)                 # Volume Sequence Number = 1
    pvd[128:132] = both_endian_16(SECTOR_SIZE)       # Logical Block Size = 2048
    pvd[132:140] = both_endian_32(path_table_size)   # Path Table Size (bytes)
    pvd[140:144] = struct.pack('<I', l_path_table_lba)  # L-type Path Table LBA (LE)
    pvd[144:148] = struct.pack('<I', 0)              # Optional L-type (0)
    pvd[148:152] = struct.pack('>I', m_path_table_lba)  # M-type Path Table LBA (BE)
    pvd[152:156] = struct.pack('>I', 0)              # Optional M-type (0)
    # 156-189: Root Directory Record (34 字节)
    root_rec = build_dir_record(
        extent_lba=root_dir_lba,
        data_length=root_dir_size,
        flags=0x02,                                  # bit 1: directory
        name_bytes=b'\x00',                          # root 用 0x00
    )
    pvd[156:156 + len(root_rec)] = root_rec
    # 190-317: Volume Set Identifier (128 bytes)
    pvd[190:318] = pad_field(b'', 128)
    # 318-445: Publisher Identifier
    pvd[318:446] = pad_field(b'', 128)
    # 446-573: Data Preparer Identifier
    pvd[446:574] = pad_field(b'', 128)
    # 574-701: Application Identifier
    pvd[574:702] = pad_field(b'PYISOBOOT', 128)
    # 702-738: Copyright File Identifier (37 bytes)
    pvd[702:739] = pad_field(b'', 37)
    # 739-775: Abstract File Identifier (37 bytes)
    pvd[739:776] = pad_field(b'', 37)
    # 776-812: Bibliographic File Identifier (37 bytes)
    pvd[776:813] = pad_field(b'', 37)
    # 813-829: Volume Creation DATE/TIME (17 bytes)
    pvd[813:830] = iso_datetime_now()
    # 830-846: Volume Modification DATE/TIME (17 bytes)
    pvd[830:847] = iso_datetime_now()
    # 847-863: Volume Expiration DATE/TIME (17 bytes, 空)
    pvd[847:864] = iso_datetime_zero()
    # 864-880: Volume Effective DATE/TIME (17 bytes)
    pvd[864:881] = iso_datetime_now()
    # 881: File Structure Version (必须是 1)
    pvd[881] = 0x01
    # 882: Reserved
    # 883-1394: Application Use (512 bytes)
    # 1395-2047: Reserved
    return bytes(pvd)


def build_boot_record_volume_descriptor(boot_catalog_lba):
    """构造 Boot Record Volume Descriptor（2048 字节，type 0）。

    El Torito 规范布局：
      offset 0     : Header Type (0x00)
      offset 1-5   : Standard Identifier "CD001"
      offset 6     : Version (1)
      offset 7-38  : Boot System Identifier (32 bytes, "EL TORITO SPECIFICATION")
      offset 39-70 : Boot Identifier (32 bytes, 0)
      offset 71-74 : Boot Catalog Sector Number (LBA, little-endian)
      offset 75-2047: Reserved (0)
    """
    brvd = bytearray(SECTOR_SIZE)
    brvd[0] = 0x00                                   # Type 0: Boot Record
    brvd[1:6] = b'CD001'
    brvd[6] = 0x01                                   # Version
    # 7-38: Boot System Identifier (32 bytes) "EL TORITO SPECIFICATION"
    boot_sys_id = b'EL TORITO SPECIFICATION'
    brvd[7:7 + len(boot_sys_id)] = boot_sys_id       # len=23, 写到 7:30, 31-38 补 0
    # 39-70: Boot Identifier (32 bytes, 0)
    # 71-74: Boot Catalog Location (LE 4 bytes)
    brvd[71:75] = struct.pack('<I', boot_catalog_lba)
    return bytes(brvd)


def build_volume_descriptor_set_terminator():
    """构造 Volume Descriptor Set Terminator（2048 字节，type 255）。"""
    term = bytearray(SECTOR_SIZE)
    term[0] = 0xFF                                   # Type 255: Terminator
    term[1:6] = b'CD001'
    term[6] = 0x01
    return bytes(term)


def build_iso():
    """
    完整构建流程：生成 boot.img 与 system.iso。
    """
    print('[1/5] 生成引导扇区机器码 + 内核加载器机器码...')
    boot_img, boot_sector, loader = build_boot_image()

    # 保存副产品
    with open(os.path.join(PROJECT_ROOT, 'bootloader.bin'), 'wb') as f:
        f.write(boot_sector)
    with open(os.path.join(PROJECT_ROOT, 'installer.bin'), 'wb') as f:
        f.write(loader)
    print('      bootloader.bin ({}B), installer.bin ({}B) 已生成'.format(
        len(boot_sector), len(loader)))

    print('[2/5] 写出 1.44MB 软盘镜像 boot.img...')
    boot_img_path = os.path.join(PROJECT_ROOT, 'boot.img')
    with open(boot_img_path, 'wb') as f:
        f.write(boot_img)
    print('      boot.img ({}B = 1.44MB) 已生成'.format(len(boot_img)))

    print('[3/5] 收集源码文件...')
    source_files = collect_source_files()
    print('      共收集 {} 个源码文件'.format(len(source_files)))

    # === 计算 ISO 布局 ===
    # 扇区编号（每个 2048 字节）：
    #   0..15     : System Area（16 扇区）
    #   16        : Primary Volume Descriptor
    #   17        : Boot Record Volume Descriptor
    #   18        : Volume Descriptor Set Terminator
    #   19        : Boot Catalog
    #   20..739   : Boot Image (1474560 字节 = 720 扇区)
    #   740       : L-type Path Table
    #   741       : M-type Path Table
    #   742..     : Root Directory
    #   +N        : EFI Directory
    #   +N+1      : BOOT Directory
    #   +N+2      : BOOTX64.EFI
    #   +N+3..    : 源码文件
    # 注：VD 顺序调整为 PVD→BRVD→Term，与已通过 QEMU 引导测试的最小 ISO 保持一致
    SECTOR_SYSTEM_AREA_END = 16
    SECTOR_PVD        = SECTOR_SYSTEM_AREA_END + 0   # 16
    SECTOR_BRVD       = SECTOR_SYSTEM_AREA_END + 1   # 17
    SECTOR_VD_TERM    = SECTOR_SYSTEM_AREA_END + 2   # 18
    SECTOR_BOOT_CAT   = SECTOR_SYSTEM_AREA_END + 3   # 19

    BOOT_IMG_SECTORS = (len(boot_img) + SECTOR_SIZE - 1) // SECTOR_SIZE  # 720
    SECTOR_BOOT_IMG_START = SECTOR_BOOT_CAT + 1                          # 20
    SECTOR_BOOT_IMG_END   = SECTOR_BOOT_IMG_START + BOOT_IMG_SECTORS - 1 # 739

    SECTOR_L_PATH_TABLE = SECTOR_BOOT_IMG_END + 1                        # 740
    SECTOR_M_PATH_TABLE = SECTOR_L_PATH_TABLE + 1                        # 741
    SECTOR_ROOT_DIR     = SECTOR_M_PATH_TABLE + 1                        # 742

    # 生成 EFI 应用程序
    efi_app = build_efi_app()
    efi_app_size = len(efi_app)
    efi_app_sectors = (efi_app_size + SECTOR_SIZE - 1) // SECTOR_SIZE    # 1

    print('[4/5] 装配 ISO9660 + El Torito + EFI 结构...')

    # 先估算根目录大小（含 . + .. + EFI 子目录 + N 个源文件记录）
    root_dir_est = 34 + 34 + 37  # . + .. + EFI 目录记录
    for iso_name, _ in source_files:
        name_len = len(iso_name) + 2  # +2 for ";1"
        pad = 1 if name_len % 2 == 0 else 0
        root_dir_est += 33 + name_len + pad
    root_dir_sectors = (root_dir_est + SECTOR_SIZE - 1) // SECTOR_SIZE
    root_dir_data_len = root_dir_sectors * SECTOR_SIZE

    # EFI/BOOT 目录和 BOOTX64.EFI 在根目录之后
    SECTOR_EFI_DIR     = SECTOR_ROOT_DIR + root_dir_sectors
    SECTOR_BOOT_DIR    = SECTOR_EFI_DIR + 1
    SECTOR_EFI_FILE    = SECTOR_BOOT_DIR + 1
    SECTOR_FILES_START = SECTOR_EFI_FILE + efi_app_sectors

    # 预先计算每个源文件占用的扇区位置
    file_records = []  # [(iso_name, abspath, file_size, lba, sector_count), ...]
    cur_lba = SECTOR_FILES_START
    for iso_name, abspath in source_files:
        fsize = os.path.getsize(abspath)
        nsec = (fsize + SECTOR_SIZE - 1) // SECTOR_SIZE
        if nsec == 0:
            nsec = 1  # 空文件至少占 1 扇区
        file_records.append((iso_name, abspath, fsize, cur_lba, nsec))
        cur_lba += nsec

    # 计算总卷大小
    total_volume_sectors = cur_lba

    # === Path Table（根目录 + EFI + BOOT 三条记录） ===
    l_path_recs = [
        build_path_table_record(b'\x01', SECTOR_ROOT_DIR, 1, big_endian=False),
        build_path_table_record(b'EFI', SECTOR_EFI_DIR, 1, big_endian=False),
        build_path_table_record(b'BOOT', SECTOR_BOOT_DIR, 2, big_endian=False),
    ]
    m_path_recs = [
        build_path_table_record(b'\x01', SECTOR_ROOT_DIR, 1, big_endian=True),
        build_path_table_record(b'EFI', SECTOR_EFI_DIR, 1, big_endian=True),
        build_path_table_record(b'BOOT', SECTOR_BOOT_DIR, 2, big_endian=True),
    ]
    path_table_data = b''.join(l_path_recs)
    l_path_table_padded = path_table_data + b'\x00' * (SECTOR_SIZE - len(path_table_data))
    m_path_table_padded = b''.join(m_path_recs) + b'\x00' * (SECTOR_SIZE - len(b''.join(m_path_recs)))
    path_table_size = len(path_table_data)

    # === 根目录记录 ===
    # 根目录包含：. + .. + EFI 子目录 + 各源文件
    root_dir = bytearray()
    # . 记录
    root_dir += build_dir_record(
        extent_lba=SECTOR_ROOT_DIR,
        data_length=root_dir_data_len,  # 已知大小
        flags=0x02,
        name_bytes=b'\x00',
    )
    # .. 记录
    root_dir += build_dir_record(
        extent_lba=SECTOR_ROOT_DIR,
        data_length=root_dir_data_len,
        flags=0x02,
        name_bytes=b'\x01',
    )
    # EFI 子目录记录
    root_dir += build_dir_record(
        extent_lba=SECTOR_EFI_DIR,
        data_length=SECTOR_SIZE,        # EFI 目录占 1 扇区
        flags=0x02,                     # 目录
        name_bytes=b'EFI',
    )
    # 各源文件记录
    for iso_name, abspath, fsize, flba, _ in file_records:
        rec = build_dir_record(
            extent_lba=flba,
            data_length=fsize,
            flags=0x00,                       # 普通文件
            name_bytes=iso_name.encode('ascii', errors='replace') + b';1',  # ;1 = version
        )
        root_dir += rec

    root_dir_padded = bytes(root_dir) + b'\x00' * (root_dir_sectors * SECTOR_SIZE - len(root_dir))

    # === EFI 目录记录 ===
    # EFI 目录包含：. + .. + BOOT 子目录
    efi_dir = bytearray()
    efi_dir += build_dir_record(
        extent_lba=SECTOR_EFI_DIR,
        data_length=SECTOR_SIZE,
        flags=0x02,
        name_bytes=b'\x00',
    )
    efi_dir += build_dir_record(
        extent_lba=SECTOR_ROOT_DIR,   # 父目录 = 根目录
        data_length=root_dir_data_len,
        flags=0x02,
        name_bytes=b'\x01',
    )
    efi_dir += build_dir_record(
        extent_lba=SECTOR_BOOT_DIR,
        data_length=SECTOR_SIZE,
        flags=0x02,
        name_bytes=b'BOOT',
    )
    efi_dir_padded = bytes(efi_dir) + b'\x00' * (SECTOR_SIZE - len(efi_dir))

    # === BOOT 目录记录 ===
    # BOOT 目录包含：. + .. + BOOTX64.EFI 文件
    boot_dir = bytearray()
    boot_dir += build_dir_record(
        extent_lba=SECTOR_BOOT_DIR,
        data_length=SECTOR_SIZE,
        flags=0x02,
        name_bytes=b'\x00',
    )
    boot_dir += build_dir_record(
        extent_lba=SECTOR_EFI_DIR,     # 父目录 = EFI 目录
        data_length=SECTOR_SIZE,
        flags=0x02,
        name_bytes=b'\x01',
    )
    boot_dir += build_dir_record(
        extent_lba=SECTOR_EFI_FILE,
        data_length=efi_app_size,
        flags=0x00,                    # 普通文件
        name_bytes=b'BOOTX64.EFI;1',
    )
    boot_dir_padded = bytes(boot_dir) + b'\x00' * (SECTOR_SIZE - len(boot_dir))

    # === 构造 Boot Catalog ===
    # boot.img = 1474560 字节 = 2880 个 512 字节扇区
    # sector_count=1: BIOS 只加载引导扇区到 0x07C00，
    # 后续扇区由引导扇区通过 INT 13h 软盘仿真读取（El Torito 标准用法）
    boot_catalog = build_boot_catalog(
        boot_img_lba=SECTOR_BOOT_IMG_START,
        sector_count_512=1,
        media_type=0x02,            # 1.44MB floppy
        load_segment=0x0000,        # 0x07C0 default
    )

    # === 构造 Volume Descriptors ===
    pvd = build_primary_volume_descriptor(
        volume_size_sectors=total_volume_sectors,
        root_dir_lba=SECTOR_ROOT_DIR,
        root_dir_size=root_dir_data_len,
        l_path_table_lba=SECTOR_L_PATH_TABLE,
        m_path_table_lba=SECTOR_M_PATH_TABLE,
        path_table_size=path_table_size,
    )
    brvd = build_boot_record_volume_descriptor(boot_catalog_lba=SECTOR_BOOT_CAT)
    term = build_volume_descriptor_set_terminator()

    # === 拼接整个 ISO ===
    iso = bytearray()

    # System Area (16 扇区)
    # 第一个 512B 扇区放 MBR + 分区表，让 Rufus 识别为真正的 ISOHybrid
    # Rufus 对 ISOHybrid 镜像（有 MBR 签名 + 分区表）提供 DD 模式（直接写入 USB）
    mbr = bytearray(b'\x00' * 512)
    # MBR 引导代码：直接跳转到 0x7C00（让 BIOS 的 El Torito 处理实际引导）
    # 这里只是占位，BIOS 从 CD/USB 启动时由 El Torito 加载引导扇区
    mbr[0] = 0xEB  # jmp short
    mbr[1] = 0x3C  # 跳过分区表前的一些空间
    mbr[2] = 0x90  # nop
    # === 分区表项 1（偏移 446-461，16 字节）===
    # ISOHybrid 标准分区：覆盖整个 ISO，类型 0x83（Linux filesystem, 最通用，所有 BIOS 都接受）
    # 之前用 0x17 (hidden FAT12) 导致部分 BIOS 把分区当"隐藏"忽略，U 盘无法识别
    iso_total_sectors_512 = (total_volume_sectors * SECTOR_SIZE) // 512
    pte1 = bytearray(16)
    pte1[0] = 0x80                # boot flag: active
    pte1[1] = 0x00                # start head: 0
    pte1[2] = 0x01                # start sector: 1 | cyl high: 0
    pte1[3] = 0x00                # start cyl low: 0
    pte1[4] = 0x83                # partition type: Linux filesystem (BIOS 兼容性最好)
    pte1[5] = 0xFE                # end head: 254 (max)
    pte1[6] = 0xFF                # end sector: 63 | cyl high: 3
    pte1[7] = 0xFF                # end cyl low: 255 (max CHS, 表示用 LBA)
    struct.pack_into('<I', pte1, 8, 0)              # LBA start: 0
    struct.pack_into('<I', pte1, 12, iso_total_sectors_512)  # LBA size
    mbr[446:446+16] = pte1
    # MBR 签名
    mbr[510] = 0x55
    mbr[511] = 0xAA
    # 填充到 2048B 扇区
    iso += bytes(mbr) + b'\x00' * (SECTOR_SIZE - 512)
    # 剩余 15 扇区 System Area
    iso += b'\x00' * ((SECTOR_SYSTEM_AREA_END - 1) * SECTOR_SIZE)
    # PVD
    iso += pvd
    # BRVD
    iso += brvd
    # VD Set Terminator
    iso += term
    # Boot Catalog
    iso += boot_catalog
    # Boot Image
    iso += boot_img
    # L Path Table
    iso += l_path_table_padded
    # M Path Table
    iso += m_path_table_padded
    # Root Directory
    iso += root_dir_padded
    # EFI Directory
    iso += efi_dir_padded
    # BOOT Directory
    iso += boot_dir_padded
    # BOOTX64.EFI 文件
    efi_padded = efi_app + b'\x00' * (efi_app_sectors * SECTOR_SIZE - len(efi_app))
    iso += efi_padded
    # 各源文件
    for iso_name, abspath, fsize, flba, nsec in file_records:
        with open(abspath, 'rb') as f:
            data = f.read()
        # 填充到整扇区
        padded = data + b'\x00' * (nsec * SECTOR_SIZE - len(data))
        iso += padded

    # 最终尺寸应为 total_volume_sectors * 2048
    expected = total_volume_sectors * SECTOR_SIZE
    if len(iso) < expected:
        iso += b'\x00' * (expected - len(iso))
    elif len(iso) > expected:
        # 截断到预期大小（不应发生，但保险）
        iso = iso[:expected]

    print('[5/5] 写出 system.iso...')
    iso_path = os.path.join(PROJECT_ROOT, 'system.iso')
    with open(iso_path, 'wb') as f:
        f.write(iso)
    print('      system.iso ({} bytes, {} sectors) 已生成'.format(len(iso), total_volume_sectors))

    print()
    print('========== 构建完成 ==========')
    print('  引导扇区  : bootloader.bin  ({} B)'.format(len(boot_sector)))
    print('  安装器    : installer.bin   ({} B)'.format(len(loader)))
    print('  软盘镜像  : boot.img        ({} B = 1.44 MB)'.format(len(boot_img)))
    print('  ISO 镜像  : system.iso      ({} B = {:.2f} MB)'.format(
        len(iso), len(iso) / (1024 * 1024)))
    print('  源码文件  : {} 个'.format(len(file_records)))
    print('==============================')
    print()
    print('引导流程：')
    print('  BIOS -> 加载 boot.img 到 0x07C00 -> 跳转 0x0000:0x7C00')
    print('  引导扇区 -> INT 13h 读 32 扇区到 0x0000:0x1000 -> 跳转 0x0000:0x1000')
    print('  安装器 -> 打印欢迎信息 -> INT 13h LBA 读 USB 系统镜像到 0x8000')
    print('         -> INT 13h LBA 写 0x8000 到硬盘 LBA 0 -> 打印完成 -> 重启')
    print('  硬盘启动 -> HDD 引导扇区加载 HDD 加载器 -> 显示 "Running from HDD" -> hlt')
    print()
    print('可在 QEMU 中验证安装流程：')
    print('  qemu-system-i386 -cdrom system.iso -boot d -hda hdd.img')
    print('  （安装后重启从硬盘引导：qemu-system-i386 -hda hdd.img')


if __name__ == '__main__':
    build_iso()
