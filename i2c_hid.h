// i2c_hid.h - Intel LPSS I2C 控制器 + I2C-HID 触摸屏全栈 (盲写, 防御式)
// 依赖: usb_xhci.h 先于本文件引入
//       (pci_read32/paging_map_mmio/mmio32/pit_*/usb_parse_digitizer/dg_bits/gprintfa)
// 防御设计: BAR 全 F / 控制器复位不来 / 地址 NACK / 描述符超限 / 解析失败 /
//           轮询超时 -> 一律置 i2c_degraded=1 永久禁用, 绝不阻塞主循环。
// 真机无 LPSS (QEMU) 是预期路径: init 首行就退出。
#ifndef I2C_HID_H
#define I2C_HID_H

// ---------------- 状态 ----------------
static int i2c_degraded = 1;    // 1 = 无可用控制器/设备 (QEMU 预期)
static int i2c_present = 0;     // 1 = I2C-HID 触摸屏就绪, poll 可用
static int i2c_ctrl_found = 0;  // 1 = 找到 LPSS 控制器 (可能仍无设备)
static int i2c_touch_down = 0;
static int i2c_touch_x = -1, i2c_touch_y = -1;
static int i2c_addr_used = 0;
static uint32_t i2c_ven_did = 0;          // 控制器 (ven<<16)|did

static uint32_t i2c_mmio = 0;
static uint16_t ih_input_reg, ih_max_input, ih_cmd_reg;
static int ih_need;                       // 每次输入报告读取字节数 (<=32)
static uint8_t ih_buf[34];
static int ih_x_off, ih_y_off, ih_tip_off, ih_x_bits, ih_y_bits;
static uint32_t ih_x_max, ih_y_max;

// ---------------- ACPI DSDT/SSDT 解析结果 (hwprobe 扫表时回填) ----------------
// WCOM485B 的从地址不在注册表, 只在固件 AML 的 I2cSerialBusV2 资源描述符里
static int      i2c_acpi_found = 0;
static uint16_t i2c_acpi_addr = 0;        // 7 位 I2C 从地址
static uint32_t i2c_acpi_hz = 0;          // 连接速率 (Hz)
static int      i2c_acpi_gpio = -1;       // GpioInt 首个引脚 (-1 = 未解析到)

// GenericSerialBus 描述符布局 (相对 0x80 大项标记):
// +0 0x80  +1 长度word  +3 名称"Se"  +5 版本=1  +6 资源索引  +7 总线类型(1=I2C)
// +8 通用标志 +9 类型版本=1 +10 类型数据长度=6 +12 速率dword +16 从地址word
// GpioInt: +3 名称"Gp" +5 版本=1 +12 引脚表偏移word; 表内 [引脚数word][引脚word...]
static void i2c_acpi_note(const uint8_t *p, uint32_t len) {
    if (i2c_acpi_found || !p || len < 64u) return;
    uint32_t wpos[4]; int wn = 0;
    for (uint32_t i = 0; i + 8 <= len && wn < 4; i++) {
        if (p[i]=='W' && p[i+1]=='C' && p[i+2]=='O' && p[i+3]=='M' &&
            p[i+4]=='4' && p[i+5]=='8' && p[i+6]=='5' && p[i+7]=='B')
            wpos[wn++] = i;
    }
    if (!wn) return;
    // 全表找 I2cSerialBusV2, 与任一 WCOM485B 距离 ±8KB 即归属 (_CRS 顺序不固定)
    for (uint32_t m = 0; m + 18 <= len; m++) {
        if (p[m] != 0x80u) continue;
        if (!(p[m+3]=='S' && p[m+4]=='e' && p[m+5]==1u &&
              p[m+7]==1u && p[m+9]==1u && p[m+10]==6u && p[m+11]==0u)) continue;
        int near_w = 0;
        for (int k = 0; k < wn; k++) {
            uint32_t d = (m >= wpos[k]) ? (m - wpos[k]) : (wpos[k] - m);
            if (d <= 8192u) { near_w = 1; break; }
        }
        if (!near_w) continue;
        uint16_t a = (uint16_t)(p[m+16] | (p[m+17] << 8));
        uint32_t hz = (uint32_t)p[m+12] | ((uint32_t)p[m+13] << 8) |
                      ((uint32_t)p[m+14] << 16) | ((uint32_t)p[m+15] << 24);
        if (a < 2u || a > 0x7Fu || hz < 10000u || hz > 5000000u) continue;
        i2c_acpi_addr = a;
        i2c_acpi_hz = hz;
        i2c_acpi_found = 1;
        // 距该描述符 ±8KB 内最近的 GpioInt 首引脚
        uint32_t lo = (m > 8192u) ? m - 8192u : 0;
        uint32_t hi = m + 8192u; if (hi + 18 > len) hi = len - 18;
        uint32_t best = 0; int best_d = 0x7FFFFFFF;
        for (uint32_t g = lo; g <= hi; g++) {
            if (p[g]!=0x80u || p[g+3]!='G' || p[g+4]!='p' || p[g+5]!=1u) continue;
            uint16_t pto = (uint16_t)(p[g+12] | (p[g+13] << 8));
            if (pto < 16u || pto >= 512u || g + pto + 4 > len) continue;
            uint32_t d = (g >= m) ? (g - m) : (m - g);
            if ((int)d < best_d) { best = g; best_d = (int)d; }
        }
        if (best) {
            uint16_t pto = (uint16_t)(p[best+12] | (p[best+13] << 8));
            i2c_acpi_gpio = (int)(uint16_t)(p[best+pto+2] | (p[best+pto+3] << 8));
        }
        return;
    }
}

// ---------------- DesignWare I2C 寄存器 ----------------
#define IW_CON        0x00u
#define IW_TAR        0x04u
#define IW_DATA_CMD   0x10u
#define IW_RAW        0x34u
#define IW_RX_TL      0x3Cu
#define IW_CLR_INTR   0x40u
#define IW_CLR_TXABRT 0x54u
#define IW_EN         0x6Cu
#define IW_STATUS     0x70u
#define IW_TXFLR      0x74u
#define IW_RXFLR      0x78u
#define IW_EN_STAT    0x9Cu
#define ST_TFNF       (1u << 1)
#define ST_RFNE       (1u << 3)
#define RAW_TXABRT    (1u << 5)
#define RAW_STOPDET   (1u << 9)
#define DC_READ       (1u << 8)
#define DC_STOP       (1u << 9)
#define DC_RESTART    (1u << 10)
// LPSS 私有寄存器 (BAR+0x200 区)
#define LP_RESETS     0x204u               // bit0 i2c func reset_n, bit1 iDMA reset_n
#define LP_CLK_GATE   0x208u               // bit0 = 时钟运行

static inline uint32_t ir(unsigned off) { return mmio32(i2c_mmio + off); }
static inline void     iw(unsigned off, uint32_t v) { mmio32w(i2c_mmio + off, v); }

// ---------------- 阻塞传输 (仅 init 阶段) ----------------
// write wn 字节 + (rn>0) 重复 START 读 rn 字节; 返回读到字节数, <0 失败
static int i2c_xfer(uint8_t a, const uint8_t *w, int wn, uint8_t *r, int rn) {
    if (!i2c_mmio) return -1;
    iw(IW_EN, 0);
    for (int k = 0; k < 2000 && (ir(IW_EN_STAT) & 1u); k++) { }   // 等DISABLED, 带上限
    iw(IW_TAR, a);
    iw(IW_RX_TL, 0);
    (void)ir(IW_CLR_TXABRT);
    (void)ir(IW_CLR_INTR);
    iw(IW_EN, 1);
    for (int k = 0; k < wn; k++) {
        int spin = 0;
        while (!(ir(IW_STATUS) & ST_TFNF))
            if (++spin > 20000) { iw(IW_EN, 0); return -2; }
        iw(IW_DATA_CMD, w[k] | ((rn == 0 && k == wn - 1) ? DC_STOP : 0));
    }
    for (int k = 0; k < rn; k++) {
        int spin = 0;
        while (!(ir(IW_STATUS) & ST_TFNF))
            if (++spin > 20000) { iw(IW_EN, 0); return -2; }
        iw(IW_DATA_CMD, DC_READ | (k == 0 ? DC_RESTART : 0) | (k == rn - 1 ? DC_STOP : 0));
    }
    int got = 0, spin2 = 0;
    while (got < rn) {
        uint32_t f = ir(IW_RXFLR);
        while (f-- && got < rn) r[got++] = (uint8_t)ir(IW_DATA_CMD);
        if (ir(IW_RAW) & RAW_TXABRT) {          // NACK/中止
            (void)ir(IW_CLR_TXABRT);
            iw(IW_EN, 0);
            return -3;
        }
        if (++spin2 > 40000) { iw(IW_EN, 0); return -4; }
    }
    iw(IW_EN, 0);
    return got;
}

// ---------------- 非阻塞轮询状态机 (每 tick 一次) ----------------
#define IS_IDLE  0
#define IS_CMD   1
#define IS_READ  2
#define IS_DRAIN 3
static int ih_state = IS_IDLE;
static int ih_queued = 0, ih_got = 0, ih_cool = 0, ih_fails = 0, ih_spin = 0;

static void i2c_hid_poll(void) {
    if (!i2c_present || i2c_degraded) return;
    if (ih_state == IS_IDLE) {
        if (ih_cool > 0) { ih_cool--; return; }
        // 发起新一轮: 配置地址 + 写寄存器地址 (2 字节)
        iw(IW_EN, 0);
        for (int k = 0; k < 500 && (ir(IW_EN_STAT) & 1u); k++) { }
        iw(IW_TAR, (uint32_t)i2c_addr_used);
        iw(IW_RX_TL, 0);
        (void)ir(IW_CLR_TXABRT);
        (void)ir(IW_CLR_INTR);
        iw(IW_EN, 1);
        ih_queued = 0; ih_got = 0; ih_spin = 0;
        ih_state = IS_CMD;
        return;
    }
    if (ih_state == IS_CMD) {                   // 写 2 字节寄存器地址 (LE)
        if (!(ir(IW_STATUS) & ST_TFNF)) {
            if (++ih_spin > 20000) { i2c_degraded = 1; i2c_present = 0; }
            return;
        }
        uint8_t rb = (uint8_t)(ih_input_reg >> (ih_queued * 8));
        iw(IW_DATA_CMD, rb | (ih_queued == 1 ? DC_RESTART : 0));
        if (++ih_queued >= 2) { ih_queued = 0; ih_spin = 0; ih_state = IS_READ; }
        return;
    }
    if (ih_state == IS_READ) {                  // 发读命令 (FIFO 不满才发, 非阻塞)
        if (!(ir(IW_STATUS) & ST_TFNF)) return;
        uint32_t cmd = DC_READ | (ih_queued == 0 ? DC_RESTART : 0)
                               | (ih_queued == ih_need - 1 ? DC_STOP : 0);
        iw(IW_DATA_CMD, cmd);
        ih_queued++;
        if (ih_queued >= ih_need) { ih_queued = 0; ih_spin = 0; ih_state = IS_DRAIN; }
        return;
    }
    // IS_DRAIN: 收 RX FIFO + 超时看护
    uint32_t f = ir(IW_RXFLR);
    while (f-- && ih_got < ih_need) ih_buf[ih_got++] = (uint8_t)ir(IW_DATA_CMD);
    if (ir(IW_RAW) & RAW_TXABRT) {              // 设备 NACK: 重试数次后禁用
        (void)ir(IW_CLR_TXABRT);
        ih_state = IS_IDLE;
        ih_cool = 2000;
        if (++ih_fails > 100) { i2c_degraded = 1; i2c_present = 0; }
        return;
    }
    if (ih_got >= ih_need) {
        ih_fails = 0;
        int tip = (ih_tip_off >= 0) ? (int)dg_bits(ih_buf, ih_tip_off, 1)
                                    : (int)(ih_buf[0] & 1u);
        uint32_t px = dg_bits(ih_buf, ih_x_off, ih_x_bits);
        uint32_t py = dg_bits(ih_buf, ih_y_off, ih_y_bits);
        i2c_touch_down = tip;
        if (ih_x_max) i2c_touch_x = (int)((uint64_t)px * (uint32_t)(g_fb_w - 9) / ih_x_max);
        if (ih_y_max) i2c_touch_y = (int)((uint64_t)py * (uint32_t)(g_fb_h - 9) / ih_y_max);
        // 等效触摸: 注入与 USB digitizer 相同的光标通路 (最后动作者生效)
        if (i2c_touch_down) {
            if (!s_raw_btn_prev) g_btn_press = 1;      // tip 0->1 边沿 = 点击
            s_raw_btn_prev = 1;
            cur_x = i2c_touch_x; cur_y = i2c_touch_y; cur_btn = 1;
            if (cur_x < 0) cur_x = 0;
            if (cur_y < 0) cur_y = 0;
            if (cur_x > (int)g_fb_w - 9) cur_x = (int)g_fb_w - 9;
            if (cur_y > (int)g_fb_h - 9) cur_y = (int)g_fb_h - 9;
        } else {
            s_raw_btn_prev = 0;
            cur_btn = 0;
        }
        ih_state = IS_IDLE;
        ih_cool = 2000;
        return;
    }
    if (ir(IW_RAW) & RAW_STOPDET && ih_got == 0) {  // 提前 STOP: 报告比预期短
        (void)ir(IW_CLR_INTR);
        ih_state = IS_IDLE;
        ih_cool = 2000;
        if (++ih_fails > 100) { i2c_degraded = 1; i2c_present = 0; }
        return;
    }
    if (++ih_spin > 60000) {                    // 传输迟迟不完成
        iw(IW_EN, 0);
        ih_state = IS_IDLE;
        ih_cool = 2000;
        if (++ih_fails > 20) { i2c_degraded = 1; i2c_present = 0; }
    }
}

// ---------------- 启动探测 (阻塞, 仅 boot 一次) ----------------
#define I2C_BUS_MAX 6
static uint32_t i2c_bus_mmio[I2C_BUS_MAX];
static uint32_t i2c_bus_bdf[I2C_BUS_MAX];
static uint32_t i2c_bus_vdid[I2C_BUS_MAX];
static int i2c_bus_n = 0;

// LPSS 私有区开时钟/解除复位 + 控制器基础配置
static void i2c_bus_reset(uint32_t con) {
    iw(LP_CLK_GATE, ir(LP_CLK_GATE) | 0x1u);
    iw(LP_RESETS, 0x0u);
    for (int k = 0; k < 10000; k++) { }
    iw(LP_RESETS, 0x3u);
    for (int k = 0; k < 10000; k++) { }
    iw(IW_EN, 0);
    for (int k = 0; k < 5000 && (ir(IW_EN_STAT) & 1u); k++) { }
    iw(IW_CON, con);                                // 0x61 标准100k / 0x65 快速400k
    iw(IW_RX_TL, 0);
    (void)ir(IW_CLR_INTR);
}

// 在当前 i2c_mmio 总线上识别 I2C-HID 设备: HID 描述符头 -> 报告描述符 -> 上电复位
// 返回 1 = 触摸屏已上线 (已置 i2c_present)
static int i2c_identify(int a) {
    uint8_t w[2], d[32];
    w[0] = 0x01; w[1] = 0x00;                         // HIDDescRegister = 0x0001
    if (i2c_xfer((uint8_t)a, w, 2, d, 4) < 0) return 0;
    if (d[2] != 0x01 || d[3] != 0x00) return 0;       // bcdVersion != 1.0
    if (i2c_xfer((uint8_t)a, w, 2, d, 30) < 0) return 0;
    uint16_t rd_len  = (uint16_t)(d[4] | (d[5] << 8));
    uint16_t rd_reg  = (uint16_t)(d[6] | (d[7] << 8));
    ih_input_reg     = (uint16_t)(d[8] | (d[9] << 8));
    ih_max_input     = (uint16_t)(d[10] | (d[11] << 8));
    ih_cmd_reg       = (uint16_t)(d[16] | (d[17] << 8));
    uint16_t ven_id  = (uint16_t)(d[20] | (d[21] << 8));
    uint16_t prd_id  = (uint16_t)(d[22] | (d[23] << 8));
    gprintf("[触摸I2C] 设备地址=%x 厂商标识=%x 产品标识=%x 输入寄存器=%x 最大包=%u 报告长度=%u 报告寄存器=%x\n",
            a, ven_id, prd_id, ih_input_reg, ih_max_input, rd_len, rd_reg);
    if (ih_input_reg == 0 || rd_reg == 0 || rd_len == 0 || rd_len > 1024) return 0;

    uint32_t rd = usb_dma(1024, 8);
    if (!rd) return 0;
    uint8_t rw[2];
    rw[0] = (uint8_t)(rd_reg & 0xFFu); rw[1] = (uint8_t)(rd_reg >> 8);
    int want = rd_len > 512 ? 512 : rd_len;
    int got = i2c_xfer((uint8_t)a, rw, 2, (uint8_t *)rd, want);
    if (got < 16) return 0;
    usb_parse_digitizer((const uint8_t *)rd, got);
    if (!dg_ok) {
        gprintf("[触摸I2C] 地址%x: 描述符正常但非触摸屏, 跳过\n", a);
        return 0;
    }
    ih_x_off = dg_x_off; ih_y_off = dg_y_off; ih_tip_off = dg_tip_off;
    ih_x_bits = dg_x_bits; ih_y_bits = dg_y_bits;
    ih_x_max = dg_x_max; ih_y_max = dg_y_max;
    ih_need = ih_max_input < 32 ? ih_max_input : 32;
    if (ih_need < 4) ih_need = 4;
    if (ih_need > 30) ih_need = 30;

    uint8_t cw[4];
    cw[0] = (uint8_t)(ih_cmd_reg & 0xFFu); cw[1] = (uint8_t)(ih_cmd_reg >> 8);
    cw[2] = 0x08; cw[3] = 0x01;                     // SET_POWER ON
    (void)i2c_xfer((uint8_t)a, cw, 4, 0, 0);
    cw[2] = 0x01; cw[3] = 0x00;                     // RESET
    (void)i2c_xfer((uint8_t)a, cw, 4, 0, 0);
    { uint16_t last = pit_read(); pit_wait_wrap(&last); pit_wait_wrap(&last); }

    i2c_addr_used = a;
    i2c_present = 1;
    i2c_degraded = 0;
    ih_state = IS_IDLE;
    ih_cool = 0;
    ih_fails = 0;
    gprintf("[触摸I2C] 触摸屏已上线 地址=%x X[位%u@偏移%u 最大%u] Y[位%u@偏移%u 最大%u] 触点位@%u\n",
            a, ih_x_bits, ih_x_off, ih_x_max, ih_y_bits, ih_y_off, ih_y_max, ih_tip_off);
    return 1;
}

// 在当前总线上找设备: DSDT 地址优先 (窗口最小), 再全地址盲扫; con 已配好
static int i2c_probe_bus(void) {
    uint8_t probe = 0;
    if (i2c_acpi_found) {
        int g = i2c_xfer((uint8_t)i2c_acpi_addr, 0, 0, &probe, 1);
        if (g >= 0) {
            gprintf("[触摸I2C] ACPI地址 0x%x 有应答, 尝试 HID 握手\n", i2c_acpi_addr);
            if (i2c_identify(i2c_acpi_addr)) return 1;
        } else {
            gprintf("[触摸I2C] ACPI地址 0x%x 无应答(码%d)\n", i2c_acpi_addr, g);
        }
    }
    for (int a = 0x03; a <= 0x77; a++) {
        if (i2c_acpi_found && a == (int)i2c_acpi_addr) continue;
        int g = i2c_xfer((uint8_t)a, 0, 0, &probe, 1);
        if (g < 0) continue;
        gprintf("[触摸I2C] 地址 0x%x 有应答(读取 %d 字节)\n", a, g);
        if (i2c_identify(a)) return 1;
    }
    return 0;
}

static void i2c_hid_init(void) {
    i2c_degraded = 1;
    i2c_present = 0;
    i2c_ctrl_found = 0;
    i2c_bus_n = 0;

    if (i2c_acpi_found) {
        gprintf("[触摸I2C] ACPI定位 WCOM485B: 从地址=0x%x 速率=%uHz 中断GPIO=%d\n",
                i2c_acpi_addr, i2c_acpi_hz, i2c_acpi_gpio);
    }

    // ---- 1. 收集全部 Intel LPSS I2C 控制器 (触摸可能不在第一颗上) ----
    if (pae_on) {
        for (uint32_t bus = 0; bus < 256 && i2c_bus_n < I2C_BUS_MAX; bus++) {
            for (uint32_t dev = 0; dev < 32 && i2c_bus_n < I2C_BUS_MAX; dev++) {
                uint32_t v0 = pci_read32(bus, dev, 0, 0x00);
                if ((v0 & 0xFFFFu) == 0xFFFFu || v0 == 0) continue;
                for (uint32_t fn = 0; fn < 8 && i2c_bus_n < I2C_BUS_MAX; fn++) {
                    uint32_t vd = (fn == 0) ? v0 : pci_read32(bus, dev, fn, 0x00);
                    if ((vd & 0xFFFFu) == 0xFFFFu) continue;
                    uint32_t cc = pci_read32(bus, dev, fn, 0x08) >> 8;
                    int hit = 0;
                    if ((vd >> 16) == 0x8086u) {
                        uint32_t did = vd & 0xFFFFu;
                        // SKL 9D60-9D65 / KBL 9DE0-9DEF / SPT A160-A165 / APL 5AAC 等
                        if ((did >= 0x9D60u && did <= 0x9D6Bu) ||
                            (did >= 0x9DE0u && did <= 0x9DEFu) ||
                            (did >= 0xA160u && did <= 0xA16Bu) ||
                            (did >= 0x5A60u && did <= 0x5A6Bu) ||
                            (did >= 0x4B60u && did <= 0x4B6Bu) ||
                            did == 0x5AACu) hit = 1;
                    }
                    if (!hit && (cc & 0xFFFF00u) == 0x0C8000u) hit = 1;
                    if (!hit) continue;
                    uint32_t bar0 = pci_read32(bus, dev, fn, 0x10);
                    uint32_t bar0h = pci_read32(bus, dev, fn, 0x14);
                    if (bar0 & 1u) continue;
                    if (bar0 == 0u || bar0 == 0xFFFFFFF0u || bar0 == 0xFFFFFFFFu) continue;
                    uint64_t bar_phys = ((uint64_t)bar0h << 32) | (bar0 & ~0xFull);
                    if (bar_phys == 0) continue;
                    uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
                    pci_write32(bus, dev, fn, 0x04, cmd | 0x6u);      // MEM + BUSMASTER
                    uint32_t pm = pci_read32(bus, dev, fn, 0x44);     // PMCSR -> D0 上电
                    pci_write32(bus, dev, fn, 0x44, pm & ~3u);
                    uint32_t va = paging_map_mmio(bar_phys);
                    int dup = 0;
                    for (int k = 0; k < i2c_bus_n; k++)
                        if (i2c_bus_mmio[k] == va) dup = 1;
                    if (dup) continue;
                    uint32_t con0 = *(volatile uint32_t *)va;
                    if (con0 == 0xFFFFFFFFu || con0 == 0) {
                        gprintfa(3, "[触摸I2C] 控制器 PCI %x:%x.%u 无应答(控制=%x), 跳过\n",
                                 bus, dev, fn, con0);
                        continue;
                    }
                    i2c_bus_mmio[i2c_bus_n] = va;
                    i2c_bus_bdf[i2c_bus_n]  = (bus << 8) | (dev << 3) | fn;
                    i2c_bus_vdid[i2c_bus_n] = vd;
                    i2c_bus_n++;
                    i2c_ctrl_found = 1;
                    i2c_ven_did = vd;
                    gprintf("[触摸I2C] 找到控制器 PCI %x:%x.%u 设备ID=%x:%x BAR=%x:%x 虚拟=0x%x 控制=%x\n",
                            bus, dev, fn, (vd >> 16), (vd & 0xFFFFu),
                            bar0h, (bar0 & ~0xFu), va, con0);
                }
            }
        }
    }
    if (!i2c_bus_n) {
        gprintfa(3, "[触摸I2C] 未找到 LPSS I2C 控制器, 触摸不可用(模拟器下属正常)\n");
        return;
    }

    // ---- 2. 逐颗总线: 标准 100kHz 先试, 再按 ACPI 速率重试快速 400kHz ----
    for (int b = 0; b < i2c_bus_n; b++) {
        i2c_mmio = i2c_bus_mmio[b];
        i2c_addr_used = 0;
        gprintf("[触摸I2C] 扫描总线 %d (PCI %x), 标准速率 100kHz\n", b, i2c_bus_bdf[b]);
        i2c_bus_reset(0x61u);
        if (i2c_probe_bus()) return;
        if (i2c_acpi_hz >= 300000u) {
            gprintf("[触摸I2C] 总线 %d 标准速率无应答, 按ACPI速率 %uHz(快速)重试\n", b, i2c_acpi_hz);
            i2c_bus_reset(0x65u);
            if (i2c_probe_bus()) return;
        }
    }
    gprintfa(3, "[触摸I2C] 共 %d 颗控制器, 总线上均无 I2C-HID 设备应答, 触摸不可用\n", i2c_bus_n);
    if (i2c_acpi_found)
        gprintfa(3, "[触摸I2C] 提示: ACPI已给地址0x%x但无应答, 可能触控IC未上电(ACPI _PS0/GPIO电源门未执行)\n",
                 i2c_acpi_addr);
}

#endif // I2C_HID_H
