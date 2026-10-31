# boot_pm.py — 32位保护模式引导加载器机器码生成
# 生成一个从16位实模式切换到32位保护模式的加载器
# 然后跳转到 C 内核入口（或裸机内核入口）

import struct

def build_pm_loader(base_addr=0x1000, kernel_load_addr=0x10000):
    """
    生成保护模式切换代码。
    被加载到 base_addr 处执行。

    流程：
    1. cli 关中断
    2. 写 'P' 标记到 VGA (调试)
    3. 加载 GDT (lgdt cs:[imm16])
    4. 启用 A20 (端口 0x92)
    5. 设置 CR0 PE 位
    6. 远跳转到 32 位代码段 (0x08)
    7. 32 位代码：写 'M' 标记到 VGA, 设置 DS/ES/SS/ESP
    8. 跳转到 kernel_load_addr
    """
    code = bytearray()

    # ===== 16 位代码部分 =====
    # cli
    code += b'\xFA'

    # --- 调试标记：写 'P' 到 VGA 第三字符 (DI 已经是 4，但 ES 是 0) ---
    # 写到 ES:DI，但 ES=0，所以写到 0:4 = 物理地址 4
    # 我们需要写到 0xB8000+4，所以需要 ES=0xB800
    # 但是这会破坏 lgdt 等指令假设的 ES 段
    # 改用直接 mov [0xB8004], byte 'P' 不行，因为 16 位模式地址不能直接访问 0xB8000
    # 用段覆盖: mov byte [0xB800:0x0004], 'P'
    # mov ax, 0xB800
    code += b'\xB8\x00\xB8'           # mov ax, 0xB800
    code += b'\x8E\xC0'              # mov es, ax
    # mov di, 4
    code += b'\xBF\x04\x00'          # mov di, 4
    code += b'\x26\xC6\x05\x50'      # mov byte [es:di], 'P'
    code += b'\x26\xC6\x45\x01\x07'  # mov byte [es:di+1], 0x07
    # add di, 2 (用于下一个标记)
    code += b'\x83\xC7\x02'          # add di, 2
    # 恢复 ES=0
    code += b'\x31\xC0'              # xor ax, ax
    code += b'\x8E\xC0'              # mov es, ax

    # 启用 A20 通过端口 0x92
    code += b'\xE4\x92'           # in al, 0x92
    code += b'\x0C\x02'           # or al, 0x02
    code += b'\xE6\x92'           # out 0x92, al

    # 加载 GDT (lgdt cs:[imm16])
    # 用 32 位 lgdt 指令: 66 2E 0F 01 16 [imm16]
    # imm16 是 gdtr 结构相对于 CS 段基址的偏移
    lgdt_pos = len(code)
    code += b'\x66\x2E\x0F\x01\x16'  # 32位 lgdt cs:[imm16]
    gdtr_ptr_pos = len(code)          # 这里放 imm16 (gdtr 的偏移)
    code += b'\x00\x00'               # 占位

    # 设置 CR0 PE 位
    code += b'\x0F\x20\xC0'           # mov eax, cr0
    code += b'\x0D\x01\x00\x00\x00'   # or eax, 1
    code += b'\x0F\x22\xC0'           # mov cr0, eax

    # 远跳转到 32 位代码段 (0x08:pm_entry)
    # 用 32 位远跳转: 66 EA [imm32] [selector]
    code += b'\x66\xEA'              # 32位远跳转 jmp 0x08:imm32
    pm_entry_pos = len(code)
    code += b'\x00\x00\x00\x00'      # 占位 (pm_entry 物理地址，32位)
    code += b'\x08\x00'              # 段选择子 0x08

    # ===== GDT 数据 =====
    gdt_start = len(code)

    # GDT 描述符表寄存器 (GDTR) - 6 字节
    gdtr_offset = len(code)
    gdt_limit = 3 * 8 - 1  # 3 个描述符 (null + code + data)
    # GDT base 物理地址 = base_addr + gdt_start + 6 (跳过 gdtr 自身 6 字节)
    gdt_base = base_addr + gdt_start + 6
    code += struct.pack('<H', gdt_limit)    # limit (16位)
    code += struct.pack('<I', gdt_base)     # base (32位)

    # GDT 描述符表实体
    # Null descriptor (8 字节)
    code += b'\x00' * 8

    # Code descriptor (0x08): base=0, limit=4GB, 32-bit code
    code += struct.pack('<H', 0xFFFF)   # limit low
    code += struct.pack('<H', 0x0000)   # base low
    code += b'\x00'                     # base mid
    code += b'\x9A'                     # access: present|ring0|code|executable|readable
    code += b'\xCF'                     # granularity: 4KB|32-bit|limit high=0xF
    code += b'\x00'                     # base high

    # Data descriptor (0x10): base=0, limit=4GB, 32-bit data
    code += struct.pack('<H', 0xFFFF)   # limit low
    code += struct.pack('<H', 0x0000)   # base low
    code += b'\x00'                     # base mid
    code += b'\x92'                     # access: present|ring0|data|writable
    code += b'\xCF'                     # granularity: 4KB|32-bit|limit high=0xF
    code += b'\x00'                     # base high

    # ===== 32 位代码部分 =====
    pm_entry = len(code)
    # 回填 pm_entry 物理地址 (绝对地址 = base_addr + pm_entry)
    struct.pack_into('<I', code, pm_entry_pos, base_addr + pm_entry)

    # 回填 gdtr 偏移 (16位偏移相对于 CS 段基址 = base_addr)
    struct.pack_into('<H', code, gdtr_ptr_pos, base_addr + gdtr_offset)

    # 32 位代码：先设置段寄存器 (DS=ES=SS=0x10, 数据段, base=0 limit=4GB)
    # 这样可以直接访问 0xB8000 显存
    code += b'\x66\xB8\x10\x00\x00\x00'  # mov eax, 0x10 (data selector)
    code += b'\x8E\xD8'                   # mov ds, ax
    code += b'\x8E\xC0'                   # mov es, ax
    code += b'\x8E\xD0'                   # mov ss, ax
    code += b'\x8E\xE0'                   # mov fs, ax
    code += b'\x8E\xE8'                   # mov gs, ax

    # 设置栈
    code += b'\xBC\x00\x90\x00\x00'       # mov esp, 0x9000

    # 调试标记：写 'M' 到 VGA [0xB8008] (32 位下直接访问物理地址)
    # C6 05 [disp32] [imm8] = mov byte [disp32], imm8
    code += b'\xC6\x05' + struct.pack('<I', 0xB8008) + b'\x4D'   # mov byte [0xB8008], 'M'
    code += b'\xC6\x05' + struct.pack('<I', 0xB8009) + b'\x07'   # mov byte [0xB8009], 0x07

    # 跳转到内核
    # jmp rel32 (相对跳转)
    code += b'\xE9'                       # rel32 jmp
    jmp_pos = len(code)
    # 目标 = kernel_load_addr
    # 当前位置 = base_addr + jmp_pos + 4 (jmp 指令长度)
    rel = kernel_load_addr - (base_addr + jmp_pos + 4)
    code += struct.pack('<i', rel)

    return bytes(code)


if __name__ == '__main__':
    # 测试
    # PM Loader 嵌入引导扇区，base_addr = 0x7C00
    loader = build_pm_loader(base_addr=0x7C00, kernel_load_addr=0x10000)
    print(f"PM Loader (base=0x7C00): {len(loader)} 字节")
    print(f"前 64 字节 hex: {loader[:64].hex()}")

    # 写入文件
    with open('pm_loader.bin', 'wb') as f:
        f.write(loader)
    print("已写入 pm_loader.bin")
