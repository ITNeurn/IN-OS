# extract_frames.py - 从 MP4 提取帧，转为 VGA 320x200 256色 + RLE 压缩
# 使用固定调色板 (6x6x6 色彩立方体 216色 + 40灰度 = 256色)，无需 sklearn
import cv2
import numpy as np
import struct
import os

VIDEO = r'C:\Users\PC\Desktop\IN OS开机动画.mp4'
OUT_DIR = r'C:\Users\PC\Desktop\根目录'
TARGET_FPS = 15
TARGET_W, TARGET_H = 320, 200

print('=== 提取视频帧 ===')
cap = cv2.VideoCapture(VIDEO)
src_fps = cap.get(cv2.CAP_PROP_FPS)
src_cnt = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
print(f'源视频: {src_cnt} 帧 @ {src_fps:.1f}fps')

step = max(1, round(src_fps / TARGET_FPS))
target_cnt = (src_cnt + step - 1) // step
print(f'目标: 约 {target_cnt} 帧 @ {TARGET_FPS}fps (每 {step} 帧取 1 帧)')

# 生成固定 256 色调色板 (BGR 顺序, VGA 期望 BGR)
# 216 = 6x6x6 色彩立方体 + 40 灰度
def build_palette():
    palette = np.zeros((256, 3), dtype=np.uint8)
    # 0-215: 6x6x6 cube (R, G, B 各 6 级)
    levels = np.array([0, 51, 102, 153, 204, 255], dtype=np.uint8)
    idx = 0
    for r in range(6):
        for g in range(6):
            for b in range(6):
                # OpenCV 是 BGR, 但我们存 RGB 然后在写入时转
                palette[idx] = [levels[r], levels[g], levels[b]]  # 存为 RGB
                idx += 1
    # 216-255: 40 灰度
    for i in range(40):
        v = int(i * 255 / 39)
        palette[216 + i] = [v, v, v]
    return palette

palette = build_palette()
print(f'调色板: {len(palette)} 色 (216 cube + 40 gray)')

def map_pixel(r, g, b):
    """将 RGB 像素映射到调色板索引"""
    # 判断是否近似灰度
    if abs(int(r) - int(g)) < 8 and abs(int(g) - int(b)) < 8:
        gray = int((int(r) + int(g) + int(b)) // 3)
        return 216 + min(39, gray * 40 // 256)
    else:
        ri = min(5, int(r) * 6 // 256)
        gi = min(5, int(g) * 6 // 256)
        bi = min(5, int(b) * 6 // 256)
        return ri * 36 + gi * 6 + bi

def quantize(img_bgr, palette):
    """将 BGR 图像量化为调色板索引"""
    # img_bgr 是 HxWx3 (BGR)
    h, w = img_bgr.shape[:2]
    b, g, r = img_bgr[:, :, 0], img_bgr[:, :, 1], img_bgr[:, :, 2]
    # 灰度判断
    gray_mask = (np.abs(r.astype(np.int16) - g.astype(np.int16)) < 8) & \
                (np.abs(g.astype(np.int16) - b.astype(np.int16)) < 8)
    # 灰度索引
    gray_vals = ((r.astype(np.int32) + g.astype(np.int32) + b.astype(np.int32)) // 3)
    gray_idx = 216 + np.minimum(39, gray_vals * 40 // 256)
    # 立方体索引 (转 int32 避免 uint8 溢出)
    ri = np.minimum(5, r.astype(np.int32) * 6 // 256)
    gi = np.minimum(5, g.astype(np.int32) * 6 // 256)
    bi = np.minimum(5, b.astype(np.int32) * 6 // 256)
    cube_idx = ri * 36 + gi * 6 + bi
    # 合并
    indexed = np.where(gray_mask, gray_idx, cube_idx).astype(np.uint8)
    return indexed

# RLE 压缩 (PackBits 风格)
def rle_encode(indexed_flat):
    data = indexed_flat.tolist()
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        run = 1
        while i + run < n and run < 127 and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            out.append(257 - run)
            out.append(data[i])
            i += run
        else:
            literal = []
            while i < n and len(literal) < 127:
                run2 = 1
                while i + run2 < n and run2 < 127 and data[i + run2] == data[i]:
                    run2 += 1
                if run2 >= 3:
                    break
                literal.append(data[i])
                i += 1
            out.append(len(literal) - 1)
            out.extend(literal)
    return bytes(out)

# 逐帧处理
print('提取并压缩帧...')
frames_data = []
frame_offsets = []
total_size = 0
idx = 0
frame_idx = 0

while True:
    ret, frame = cap.read()
    if not ret:
        break
    if idx % step == 0:
        small = cv2.resize(frame, (TARGET_W, TARGET_H), interpolation=cv2.INTER_AREA)
        indexed = quantize(small, palette)
        compressed = rle_encode(indexed.flatten())
        frames_data.append(compressed)
        frame_offsets.append(total_size)
        total_size += len(compressed)
        frame_idx += 1
        if frame_idx % 10 == 0 or frame_idx == 1:
            raw = TARGET_W * TARGET_H
            ratio = len(compressed) / raw * 100
            print(f'  帧 {frame_idx:3d}: {len(compressed):>7} 字节 (压缩率 {ratio:.1f}%)')
    idx += 1
cap.release()

print(f'\n总帧数: {len(frames_data)}')
print(f'总帧数据: {total_size} 字节 ({total_size/1024:.1f} KB)')
maxf = max(len(f) for f in frames_data)
print(f'单帧最大: {maxf} 字节 (loader 帧缓冲上限 64000)')
if maxf > 63000:
    print('警告: 存在接近/超过 64000 的帧, 需扩大 loader 帧缓冲')

# 写 frames.bin
# 格式:
#   0-767:    调色板 (256 * 3 = 768 字节, RGB 顺序)
#   768-771:  帧数量 (uint32 LE)
#   772+:     帧偏移表 (uint32 LE * 帧数, 相对于帧数据区起点 = 772 + 4*帧数)
#   ...:      帧数据区 (RLE 压缩后的帧)
palette_bytes = palette.tobytes()  # 256 * 3 = 768
num_frames = len(frames_data)
header_size = 768 + 4 + 4 * num_frames
# 修正偏移: 帧偏移相对于帧数据区起点
frame_data_start = header_size
frame_offsets_abs = [off for off in frame_offsets]  # 已经是相对值

index_bytes = struct.pack('<I', num_frames) + struct.pack(f'<{num_frames}I', *frame_offsets_abs)

out_path = os.path.join(OUT_DIR, 'frames.bin')
with open(out_path, 'wb') as f:
    f.write(palette_bytes)
    f.write(index_bytes)
    for fd in frames_data:
        f.write(fd)

size = os.path.getsize(out_path)
print(f'\n输出: {out_path}')
print(f'大小: {size} 字节 ({size/1024:.1f} KB)')
