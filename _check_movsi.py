# 查找 installer.bin 中所有 mov si, imm16 指令
path = r'c:\Users\PC\Desktop\根目录\installer.bin'
with open(path, 'rb') as f:
    data = f.read()

print(f'installer.bin 大小: {len(data)} 字节')

# 0xBE xx yy = mov si, imm16 (小端 yyxx)
import struct
for i in range(len(data) - 2):
    if data[i] == 0xBE:
        val = struct.unpack('<H', data[i+1:i+3])[0]
        # 看附近是否有 call (0xE8)
        ctx = data[max(0,i-2):i+8]
        hexs = ' '.join(f'{b:02x}' for b in ctx)
        print(f'偏移 0x{i:03x}: mov si, 0x{val:04x} ({val})  ctx: {hexs}')

# 也检查字符串位置
for name, s in [('msg_welcome', b'Micro-Kernel'), ('msg_reading', b'Reading'),
                ('msg_writing', b'Writing'), ('msg_done', b'Installation'),
                ('msg_reboot', b'Press'), ('msg_read_err', b'ERROR: Read'),
                ('msg_write_err', b'ERROR: Write')]:
    idx = data.find(s)
    if idx >= 0:
        print(f'{name}: 偏移 {idx} (0x{idx:x})')
