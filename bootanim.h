// bootanim.h - UEFI 路径开机动画 (内核早期, GOP LFB 真彩)
// efi64.asm 把 FAT 根目录 FRAMES.BIN 加载到物理内存, 约定低地址传参:
//   0x514 = 魔数 0x4D415246 ('FRAM')   0x518 = frames 物理地址   0x51C = 字节长度
// frames.bin 布局 (extract_frames.py 生成):
//   0..767   256 色调色板, 每色 3 字节 RGB
//   768      帧数 uint32 LE
//   772..    帧偏移表 uint32 LE[帧数] (相对帧数据区起点 772+4*帧数)
//   帧数据   PackBits RLE: c<128 -> c+1 字面; c>128 -> 257-c 次重复; c=128 nop
// 源分辨率 320x200, 按屏幕整数倍放大居中。任意键 (i8042 PS/2) 连按两拍跳过。
// 依赖: paging_map_mmio (usb_xhci.h), fb_fill/fb_map/g_fb*/g_pitch, inb, pit_wait_wrap
#ifndef BOOTANIM_H
#define BOOTANIM_H

#define BA_W 320
#define BA_H 200
#define BA_PIX (BA_W * BA_H)
static uint8_t  ba_fb[BA_PIX];
static uint32_t ba_pal[256];

// 诊断 (内核控制台 gprintf 输出): 阶段码 + 关键原始值
static uint32_t ba_diag_stage = 0;
static uint32_t ba_diag_magic = 0, ba_diag_phys = 0, ba_diag_len = 0, ba_diag_n = 0;

// i8042 状态低 2 位 == 01 (输出缓冲满, 输入缓冲空) 且数据非 00/FF
// 真机无 PS/2 控制器时端口多读到 FF, 双条件防误跳
static int ba_key_hit(void) {
    unsigned st = inb(0x64);
    if ((st & 3u) != 1u) return 0;
    unsigned d = inb(0x60);
    return (d != 0 && d != 0xFF) ? 1 : 0;
}
static void ba_drain_kbd(void) {
    for (int k = 0; k < 16; k++) {
        if (!(inb(0x64) & 1u)) break;
        (void)inb(0x60);
    }
}

// 一行横向 sf 个重复像素
static void ba_putrun(volatile uint8_t *p, uint32_t cc, int sf) {
    if (g_bppb == 4) {
        for (int k = 0; k < sf; k++) *(volatile uint32_t *)(p + (uint32_t)k * 4) = cc;
    } else {
        for (int k = 0; k < sf; k++) p[k] = (uint8_t)cc;
    }
}

// PIT 通道0 配置: 100Hz (重装值 11932, 每 10ms 一次回绕); 用于稳定 14fps 节拍
// OVMF 退出后 PIT 重装值不确定 (默认 65536=55ms 或固件已设 100Hz),
// 计数差法在回绕点会误算, 故显式配成已知频率, 播完恢复 65536 默认
static void ba_pit_set(uint16_t reload) {
    outb(0x43, 0x36);                       // 通道0: lo/hi, mode3 方波
    outb(0x40, (uint8_t)(reload & 0xFF));
    outb(0x40, (uint8_t)(reload >> 8));
}

static void bootanim_play(void) {
    // 此时 CR0.PG=0 (efi64 进内核前关分页, 直到 usb_input_init 扫到 xHCI 才开 PAE),
    // 线性地址=物理地址, 直接访问 UEFI 放置帧数据的物理地址, 不能用 paging_map_mmio
    // (关分页时其窗口 VA 会落到物理 0xE2000000 零页, 读到帧数 0)
    ba_diag_magic = *(volatile uint32_t *)0x514;
    ba_diag_phys  = *(volatile uint32_t *)0x518;
    ba_diag_len   = *(volatile uint32_t *)0x51C;
    if (ba_diag_magic != 0x4D415246u) { ba_diag_stage = 1; return; }
    ba_diag_stage = 2;
    uint32_t phys = ba_diag_phys;
    uint32_t flen = ba_diag_len;
    if (!phys || phys > 0xD0000000u || flen < 800u || flen > 0x4000000u) return;

    const uint8_t *f = (const uint8_t *)(uint32_t)phys;
    uint32_t n = (uint32_t)f[768] | ((uint32_t)f[769] << 8) |
                 ((uint32_t)f[770] << 16) | ((uint32_t)f[771] << 24);
    ba_diag_n = n;
    if (n < 1 || n > 10000) { ba_diag_stage = 3; return; }
    ba_diag_stage = 4;
    uint32_t dstart = 772u + 4u * n;
    if (dstart >= flen) { ba_diag_stage = 5; return; }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = (uint32_t)f[772 + i*4] | ((uint32_t)f[773 + i*4] << 8) |
                     ((uint32_t)f[774 + i*4] << 16) | ((uint32_t)f[775 + i*4] << 24);
        if (o >= flen - dstart) { ba_diag_stage = 6; return; }
    }
    ba_diag_stage = 7;

    for (int i = 0; i < 256; i++)
        ba_pal[i] = fb_map(((uint32_t)f[i*3] << 16) |
                           ((uint32_t)f[i*3+1] << 8) | f[i*3+2]);

    int sf = (int)g_fb_w / BA_W;
    int t = (int)g_fb_h / BA_H;
    if (t < sf) sf = t;
    if (sf < 1) sf = 1;
    int ox = ((int)g_fb_w - BA_W * sf) / 2;
    int oy = ((int)g_fb_h - BA_H * sf) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    fb_fill(0, 0, (int)g_fb_w - 1, (int)g_fb_h - 1, 0);
    ba_pit_set(11932);                     // 100Hz: 每回绕 10ms
    uint16_t last = pit_read();
    int keyc = 0, played = 0, shown = 0;
    for (uint32_t fr = 0; fr < n; fr++) {
        if (ba_key_hit()) { if (++keyc >= 2) { ba_drain_kbd(); break; } }
        else keyc = 0;

        uint32_t off = (uint32_t)f[772 + fr*4] | ((uint32_t)f[773 + fr*4] << 8) |
                       ((uint32_t)f[774 + fr*4] << 16) | ((uint32_t)f[775 + fr*4] << 24);
        uint32_t end = (fr + 1 < n)
            ? ((uint32_t)f[772 + (fr+1)*4] | ((uint32_t)f[773 + (fr+1)*4] << 8) |
               ((uint32_t)f[774 + (fr+1)*4] << 16) | ((uint32_t)f[775 + (fr+1)*4] << 24))
            : (flen - dstart);
        const uint8_t *sp = f + dstart + off;
        const uint8_t *se = f + dstart + end;
        int di = 0;
        while (sp < se && di < BA_PIX) {
            uint8_t c = *sp++;
            if (c < 128) {
                int cnt = c + 1;
                while (cnt-- && di < BA_PIX) ba_fb[di++] = *sp++;
            } else if (c > 128) {
                int cnt = 257 - c;
                uint8_t v = *sp++;
                while (cnt-- && di < BA_PIX) ba_fb[di++] = v;
            }
        }

        for (int y = 0; y < BA_H; y++) {
            int sy = oy + y * sf;
            const uint8_t *row = ba_fb + y * BA_W;
            for (int ry = 0; ry < sf; ry++) {
                volatile uint8_t *p = g_fb + (uint32_t)(sy + ry) * g_pitch
                                          + (uint32_t)ox * g_bppb;
                for (int x = 0; x < BA_W; x++) {
                    ba_putrun(p, ba_pal[row[x]], sf);
                    p += (uint32_t)sf * g_bppb;
                }
            }
        }
        played = 1;
        shown++;
        for (int k = 0; k < 7; k++) pit_wait_wrap(&last);  // 7 x 10ms = 70ms (~14fps)
    }
    ba_pit_set(0);                         // 恢复默认 65536 (55ms/回绕)
    ba_diag_stage = 8;
    ba_diag_n = (uint32_t)shown;
    if (played) fb_fill(0, 0, (int)g_fb_w - 1, (int)g_fb_h - 1, 0);
}

#endif // BOOTANIM_H
