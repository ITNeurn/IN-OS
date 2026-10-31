// usb_xhci.h - 最小 xHCI 主机控制器 + USB HID (boot protocol) 键盘/鼠标驱动
// 供 kernel_full.c 在 #include "font_fb.h" 及 fb/gprintf/pit 助手定义之后引入。
// 设计: 轮询式(无中断/无MSI), 单控制器, 最多 4 设备。
// 键盘: HID boot protocol -> ASCII 注入 gui_kb_drain;
// 鼠标: 相对位移累积 -> 屏幕光标(保存/恢复底图, 每帧最后绘制在最上层)。
//
// 关键依据: xHCI 1.1 spec + Linux xhci.h TRB 类型表 (勿凭记忆改 GUID/偏移):
//   TRB: NORMAL=1 SETUP=2 DATA=3 STATUS=4 LINK=6 EVENT_DATA=7 TR_NOOP=8
//        ENABLE_SLOT=9 DISABLE_SLOT=10 ADDRESS_DEVICE=11 CONFIG_EP=12
//        事件: TRANSFER=32 COMPLETION=33 PORT_STATUS=34
//   EP 类型: 3=INT OUT 4=CTRL 7=INT IN;  EP1 IN doorbell target=3, EP0=1
#ifndef USB_XHCI_H
#define USB_XHCI_H

// ====================== 32 位端口与 MMIO 助手 ======================
static inline uint32_t inl(unsigned short port) {
    uint32_t v;
    __asm__ __volatile__("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl(unsigned short port, uint32_t v) {
    __asm__ __volatile__("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t mmio32(uint32_t a) { return *(volatile uint32_t *)a; }
static inline void mmio32w(uint32_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

// ====================== PCI 配置空间 ======================
static uint32_t pci_read32(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off) {
    uint32_t addr = 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFCu);
    outl(0xCF8, addr);
    return inl(0xCFC);
}
static void pci_write32(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off, uint32_t v) {
    uint32_t addr = 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFCu);
    outl(0xCF8, addr);
    outl(0xCFC, v);
}

// ====================== DMA 池 (分页已关, 线性=物理) ======================
#define USB_POOL_SIZE 0xC000u                 // 48KB
static uint8_t usb_pool[USB_POOL_SIZE] __attribute__((aligned(4096)));
static uint32_t usb_pool_top = 0;

// ====================== PAE 分页 (32位模式访问 4GB 以上的 xHCI MMIO) ======================
// 恒等映射 0..0xDFFFFFFF; 0xE0000000 起 64MB 为 MMIO 别名窗 (不可缓存)
#define MMIO_WIN_VA   0xE0000000u
#define MMIO_WIN_LEN  (64u * 1024u * 1024u)
#define MMIO_WIN_MASK (MMIO_WIN_LEN - 1u)
static uint64_t pae_pdpt[4]     __attribute__((aligned(4096)));
static uint64_t pae_pd[4][512]  __attribute__((aligned(4096)));
static int pae_on = 0;

static void paging_pae_enable(void) {
    if (pae_on) return;
    for (int i = 0; i < 4; i++) {
        pae_pdpt[i] = 0;
        for (int j = 0; j < 512; j++) pae_pd[i][j] = 0;
    }
    // PD0/PD1 全 512 项 (0..2GB), PD2 全 (2..3GB, 含 LFB), PD3 前 256 项 (3..3.5GB)
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 512; j++)
            pae_pd[i][j] = ((uint64_t)i << 30) + ((uint64_t)j << 21) | 0x87u; // P|RW|PS|G
    for (int j = 0; j < 256; j++)
        pae_pd[3][j] = (0xC0000000ull) + ((uint64_t)j << 21) | 0x87u;
    for (int i = 0; i < 4; i++)
        pae_pdpt[i] = (uint32_t)&pae_pd[i][0] | 0x1u;     // 物理=线性(恒等), P
    uint32_t pdpt_phys = (uint32_t)&pae_pdpt[0];
    __asm__ __volatile__(
        "movl %%cr4, %%eax\n"
        "orl $0x20, %%eax\n"
        "movl %%eax, %%cr4\n"          // CR4.PAE
        "movl %0, %%cr3\n"
        "movl %%cr0, %%eax\n"
        "orl $0x80000000, %%eax\n"
        "movl %%eax, %%cr0\n"          // CR0.PG
        "jmp 1f\n 1:\n"
        : : "r"(pdpt_phys) : "eax", "memory");
    pae_on = 1;
}

// 把 64 位物理 BAR 映射到 MMIO 窗, 返回虚拟基址。
// 多窗口: xHCI 与 LPSS I2C 的 BAR 可能落在不同 64MB 对齐区,
// 后映射者不得覆盖先映射者 (每窗独立 32 个 2MB PDE, 最多 4 窗)。
#define MMIO_WIN_MAX 4
static uint64_t g_mmio_win_base[MMIO_WIN_MAX];
static int g_mmio_win_n = 0;
static uint32_t paging_map_mmio(uint64_t bar_phys) {
    uint64_t aligned = bar_phys & ~(uint64_t)MMIO_WIN_MASK;
    int idx = -1;
    for (int k = 0; k < g_mmio_win_n; k++)
        if (g_mmio_win_base[k] == aligned) { idx = k; break; }
    if (idx < 0) {
        if (g_mmio_win_n >= MMIO_WIN_MAX) idx = MMIO_WIN_MAX - 1;   // 防御: 复用末窗
        else idx = g_mmio_win_n++;
        g_mmio_win_base[idx] = aligned;
        uint32_t pde0 = 256u + (uint32_t)idx * (MMIO_WIN_LEN >> 21);
        for (uint32_t k = 0; k < (MMIO_WIN_LEN >> 21); k++)
            pae_pd[3][pde0 + k] = (aligned + ((uint64_t)k << 21)) | 0x83u | 0x10u; // P|RW|PS|PCD
    }
    uint32_t off = (uint32_t)(bar_phys - aligned);
    return MMIO_WIN_VA + (uint32_t)idx * MMIO_WIN_LEN + off;
}

static uint32_t usb_dma(uint32_t size, uint32_t align) {
    usb_pool_top = (usb_pool_top + align - 1) & ~(align - 1);
    if (usb_pool_top + size > USB_POOL_SIZE) return 0;
    uint32_t p = (uint32_t)&usb_pool[0] + usb_pool_top;
    usb_pool_top += size;
    return p;
}

// ====================== 常量 ======================
#define USB_MAX_DEV 4
#define RING_N      16                        // TRB 环: 15 可用 + 1 LINK
#define ER_N        16                        // 事件环条目数

// TRB 类型
#define TRB_NORMAL 1u
#define TRB_SETUP 2u
#define TRB_DATA 3u
#define TRB_STATUS 4u
#define TRB_LINK 6u
#define TRB_ENABLE_SLOT 9u
#define TRB_ADDR_DEV 11u
#define TRB_CONFIG_EP 12u
#define TRB_EV_TRANSFER 32u
#define TRB_EV_COMPLETION 33u
#define TRB_EV_PORT 34u

// TRB 控制 dword 位
#define TRB_TC(b) ((b) << 10)                // type
#define TRB_CYC   (1u << 0)                  // cycle
#define TRB_IOC   (1u << 5)
#define TRB_IDT   (1u << 6)                  // SETUP TRB: Immediate Data (bit6)
#define TRB_TRT(b) ((b) << 16)               // setup: 0 无数据 2 IN 3 OUT
#define TRB_DIR_IN (1u << 16)                // data/status 方向
#define TRB_LINK_TC (1u << 1)                // link: toggle cycle
#define TRB_CH    (1u << 4)                  // chain bit

// Op regs
#define OP_USBCMD 0x00u
#define OP_USBSTS 0x04u
#define OP_CRCR   0x18u
#define OP_DCBAAP 0x30u
#define OP_CONFIG 0x38u
#define OP_PORTSC 0x400u                     // + (port-1)*0x10

#define USBCMD_RS 1u
#define USBCMD_HCRST 2u
#define USBSTS_HCH 1u
#define USBSTS_CNR (1u << 11)

// PORTSC 位
#define PS_CCS (1u << 0)
#define PS_PED (1u << 1)
#define PS_PR  (1u << 4)
#define PS_PP  (1u << 9)
#define PS_CSC (1u << 17)
#define PS_PEC (1u << 18)
#define PS_PRC (1u << 21)
#define PS_PLC (1u << 22)
#define PS_CHG (PS_CSC | PS_PEC | PS_PRC | PS_PLC)

typedef struct {
    uint32_t p0, p1, s, c;
} usb_trb_t;

typedef struct {
    int active, slot, speed, rhport;
    int is_kbd, is_mouse, is_digi, iface;
    int in_mps;                              // IN 端点最大包
    uint32_t vid, did;                       // 设备描述符 (指纹白名单/检测页)
    int digi_x_off, digi_y_off, digi_tip_off;    // 报告内位偏移 (-1=无)
    int digi_x_bits, digi_y_bits, digi_need;
    uint32_t digi_x_max, digi_y_max;
    uint32_t dev_ctx, input_ctx, ep0_ring, ein_ring, ein_buf;
    uint32_t ep0_i, ep0_cc, ein_i, ein_cc;
    uint8_t prev_keys[6];
    uint64_t reports, keys;
} usb_dev_t;

// ====================== 全局状态 ======================
static int g_xhci_ok = 0;
static int g_xhci_pci_b, g_xhci_pci_d, g_xhci_pci_f;
static uint32_t g_xhci_cap, g_xhci_op, g_xhci_rt, g_xhci_db;
static uint32_t g_xhci_maxports = 0, g_xhci_slots_en = 0;
static int g_xhci_csz = 32;
static uint32_t g_dcbaa, g_cmd_ring, g_evt_ring, g_erst;
static uint32_t g_cmd_i = 0, g_cmd_cc = 1;
static uint32_t g_evt_i = 0, g_evt_cc = 1;
static volatile int g_cmd_done = 0;
static volatile uint32_t g_cmd_ev_code = 0, g_cmd_ev_slot = 0;
static volatile int g_ep0_done = 0;
static volatile uint32_t g_ep0_ev_code = 0;
static uint32_t g_port_chg = 0;
static uint32_t g_port_fail = 0;       // 枚举失败的端口, 等 CSC(重插)再试
static usb_dev_t usb_dev[USB_MAX_DEV];
static int g_ps2_present = 1;

// 键盘 ASCII 队列
static uint8_t usb_keyq[16];
static int usb_keyq_h = 0, usb_keyq_t = 0;
// 鼠标
static int cur_x = 64, cur_y = 64, cur_drawn = 0, cur_btn = 0;
static volatile int g_btn_press = 0;       // 一次性按下边沿事件 (跨 TRB 锁存, ui_tick 消费)
static int s_raw_btn_prev = 0;             // 上次报告原始按键位 (跨 tick, 用于 0->1 边沿)
static uint32_t cur_bg[81];                  // 9x9 底图 (含 +1 投影, 实际字形 8x8)
static uint64_t g_usb_reports = 0;
static volatile int g_last_dx = 0, g_last_dy = 0;

// 触摸 digitizer / 指纹 / 摄像头识别 (阶段C)
static int usb_digi_present = 0;             // USB-HID 绝对指针触摸屏在线
static int usb_fp_present = 0;               // USB 指纹器 (VID 白名单)
static int usb_cam_present = 0;              // USB 摄像头 (UVC, class 0x0E)
// 报告描述符解析结果 (文件级, 枚举时拷入设备)
static int dg_ok, dg_x_off, dg_y_off, dg_tip_off;
static int dg_x_bits, dg_y_bits, dg_need;
static uint32_t dg_x_max, dg_y_max;

// HID 位域提取 (LSB-first)
static uint32_t dg_bits(const uint8_t *r, int off, int n) {
    uint32_t v = 0;
    for (int b = 0; b < n; b++) {
        int bit = off + b;
        v |= (uint32_t)((r[bit >> 3] >> (bit & 7)) & 1u) << b;
    }
    return v;
}
// 解析 HID 报告描述符: 提取绝对指针 X(0x30)/Y(0x31)/Tip(0x42) 位域。
// 兼容两类: digitizer 页 0x0D (真机触摸屏) 与桌面页 0x01 (QEMU usb-tablet)。
static void usb_parse_digitizer(const uint8_t *r, int len) {
    dg_ok = 0; dg_x_off = dg_y_off = 0; dg_tip_off = -1;
    dg_x_bits = dg_y_bits = dg_need = 0;
    dg_x_max = dg_y_max = 0;
    int pg = 0, us1 = 0, us2 = 0, sz = 0, ct = 0, bitpos = 0, absf = 0;
    uint32_t lmax = 0;
    int p = 0;
    while (p < len) {
        uint8_t b0 = r[p];
        if (b0 == 0xFE) {                       // 长条目: 跳过
            if (p + 2 >= len) break;
            p += 2 + r[p + 1];
            continue;
        }
        int n = (b0 & 3) == 3 ? 4 : (b0 & 3);
        if (p + 1 + n > len) break;
        int tag = (b0 >> 4) & 0xF;
        int typ = (b0 >> 2) & 3;                // 0=main 1=global 2=local
        const uint8_t *d = r + p + 1;
        if (typ == 1) {                         // global
            if (tag == 0) pg = d[0];            // usage page
            else if (tag == 2) {                // logical max
                lmax = (n == 1) ? d[0] : (n == 2) ? (uint32_t)(d[0] | (d[1] << 8))
                     : (uint32_t)(d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24));
            } else if (tag == 7) sz = d[0];     // report size
            else if (tag == 9) ct = d[0];       // report count
            else if (tag == 8) bitpos = 8;      // report id: 输入报告前 8 位
        } else if (typ == 2) {                  // local
            if (tag == 0) { us2 = us1; us1 = d[0]; }
        } else {                                // main
            if (tag == 8) {                     // input
                int v = d[0];
                int bits = sz * ct;
                if ((v & 0x03) == 0x02 && sz > 0 && (pg == 0x0D || pg == 0x01)) {
                    if (!(v & 0x04)) absf = 1;  // Data,Var,Abs
                    if ((us1 == 0x30 && us2 == 0x31) || (us1 == 0x31 && us2 == 0x30)) {
                        // X/Y 合用一条 input (QEMU usb-tablet: count=2)
                        if (us1 == 0x30) {
                            dg_x_off = bitpos; dg_y_off = bitpos + sz;
                        } else {
                            dg_y_off = bitpos; dg_x_off = bitpos + sz;
                        }
                        dg_x_bits = dg_y_bits = sz;
                        dg_x_max = dg_y_max = lmax;
                    } else if (us1 == 0x30) {   // X
                        dg_x_off = bitpos; dg_x_bits = sz; dg_x_max = lmax;
                    } else if (us1 == 0x31) {   // Y
                        dg_y_off = bitpos; dg_y_bits = sz; dg_y_max = lmax;
                    } else if (us1 == 0x42 && pg == 0x0D) {   // Tip switch
                        dg_tip_off = bitpos;
                    }
                }
                bitpos += bits;
            } else if (tag == 9 || tag == 0xB) {
                bitpos = 0;                     // output/feature: 输入报告位图另计
            }
            us1 = us2 = 0;                      // main 条目清空 local 栈
        }
        p += 1 + n;
    }
    if (dg_x_bits > 0 && dg_y_bits > 0 && absf) {
        if (dg_x_bits > 24) dg_x_bits = 24;
        if (dg_y_bits > 24) dg_y_bits = 24;
        if (dg_x_max == 0) dg_x_max = (1u << dg_x_bits) - 1u;
        if (dg_y_max == 0) dg_y_max = (1u << dg_y_bits) - 1u;
        int hi = dg_x_off > dg_y_off ? dg_x_off : dg_y_off;
        if (dg_tip_off > hi) hi = dg_tip_off;
        dg_need = (hi >> 3) + 2;
        if (dg_need > 64) dg_need = 64;
        dg_ok = 1;
    }
}

static const uint8_t cur_arrow[8] = {0x80, 0xC0, 0xA0, 0xD0, 0xB8, 0x9C, 0x8E, 0x07};

// ====================== 前向声明 ======================
static void usb_poll(void);
static uint8_t usb_usage_ascii(uint8_t u, int shift);

// ====================== 基础助手 ======================
static void usb_wait_wraps(int n) {
    uint16_t last = pit_read();
    while (n-- > 0) pit_wait_wrap(&last);
}

static usb_trb_t *ring_ptr(uint32_t ring, uint32_t i) {
    return (usb_trb_t *)(ring + i * 16u);
}

// 向 TRB 环推入并推进 producer 指针 (最后一条是 LINK, 环绕时翻转 cycle)
static void ring_push(uint32_t ring, uint32_t *i, uint32_t *cc,
                      uint64_t param, uint32_t status, uint32_t ctrl) {
    usb_trb_t *t = ring_ptr(ring, *i);
    t->p0 = (uint32_t)param;
    t->p1 = (uint32_t)(param >> 32);
    t->s = status;
    t->c = ctrl | (*cc);
    (*i)++;
    if (*i == RING_N - 1) { *i = 0; *cc ^= 1; }
}

static uint32_t portsc(int port) {          // port: 1..N
    return mmio32(g_xhci_op + OP_PORTSC + (uint32_t)(port - 1) * 0x10u);
}
static void portsc_set(int port, uint32_t bits) {
    mmio32w(g_xhci_op + OP_PORTSC + (uint32_t)(port - 1) * 0x10u, bits);
}

// 端口复位并返回速度 (0=失败, 1=FS 2=LS 3=HS)
static int usb_reset_port(int port) {
    uint32_t s = portsc(port);
    portsc_set(port, (s & ~PS_CHG) | PS_PR);
    int w = 0;
    while (w++ < 30) {                          // PIT wrap 可能只有 ~1ms (固件改过分频)
        usb_wait_wraps(1);
        s = portsc(port);
        portsc_set(port, s);                    // 写回清变化位
        if (s & PS_PED) break;
    }
    s = portsc(port);
    if (!(s & PS_PED)) return 0;
    return (int)((s >> 10) & 0xFu);
}

static usb_dev_t *usb_find_slot(uint32_t slot) {
    for (int i = 0; i < USB_MAX_DEV; i++)
        if (usb_dev[i].active && usb_dev[i].slot == (int)slot) return &usb_dev[i];
    return 0;
}
static usb_dev_t *usb_find_port(int port) {
    for (int i = 0; i < USB_MAX_DEV; i++)
        if (usb_dev[i].active && usb_dev[i].rhport == port) return &usb_dev[i];
    return 0;
}

// ====================== 命令提交 (cmd ring, 同步等待) ======================
static int usb_cmd(uint64_t param, uint32_t status, uint32_t ctrl, uint32_t *slot_out) {
    if (!g_xhci_ok) return -1;
    ring_push(g_cmd_ring, &g_cmd_i, &g_cmd_cc, param, status, ctrl);
    mmio32w(g_xhci_db, 0);                  // doorbell 0 = command ring
    g_cmd_done = 0;
    for (int k = 0; k < 40000 && !g_cmd_done; k++) usb_poll();   // 立即完成
    if (!g_cmd_done) {
        uint16_t last = pit_read();
        int wraps = 0;
        while (!g_cmd_done && wraps < 50) { pit_wait_wrap(&last); usb_poll(); wraps++; }
    }
    if (!g_cmd_done) {
        gprintfa(3, "[usb] CMD timeout trb0.c=%x cmd=%x sts=%x crcr=%x db=%x iman=%x erstsz=%x erdp=%x\n",
                 ring_ptr(g_cmd_ring, 0)->c & 0xFFFFu,
                 mmio32(g_xhci_op + OP_USBCMD),
                 mmio32(g_xhci_op + OP_USBSTS),
                 mmio32(g_xhci_op + OP_CRCR),
                 g_xhci_db,
                 mmio32(g_xhci_rt + 0x20u + 0x00u),
                 mmio32(g_xhci_rt + 0x20u + 0x08u),
                 mmio32(g_xhci_rt + 0x20u + 0x18u));
        return -2;             // 超时
    }
    if (slot_out) *slot_out = g_cmd_ev_slot;
    return (g_cmd_ev_code == 1) ? 0 : (int)(g_cmd_ev_code | 0x100);
}

// ====================== EP0 控制传输 ======================
static int usb_ctrl(uint32_t slot, uint8_t bm, uint8_t breq,
                    uint16_t wval, uint16_t widx, uint16_t wlen, uint32_t data_phys) {
    usb_dev_t *d = usb_find_slot(slot);
    if (!d) return -1;
    uint64_t setup = (uint64_t)bm | ((uint64_t)breq << 8) | ((uint64_t)wval << 16) |
                     ((uint64_t)widx << 32) | ((uint64_t)wlen << 48);
    int dir_in = (bm & 0x80) ? 1 : 0;
    uint32_t trt = wlen ? (dir_in ? 2u : 3u) : 0u;

    ring_push(d->ep0_ring, &d->ep0_i, &d->ep0_cc, setup, 8,
              TRB_TC(TRB_SETUP) | TRB_CH | TRB_IDT | TRB_TRT(trt));
    if (wlen && data_phys) {
        ring_push(d->ep0_ring, &d->ep0_i, &d->ep0_cc, data_phys, wlen,
                  TRB_TC(TRB_DATA) | TRB_CH | (dir_in ? TRB_DIR_IN : 0));
    }
    // 状态段: 方向与数据段相反
    uint32_t st_dir = dir_in ? 0 : TRB_DIR_IN;
    ring_push(d->ep0_ring, &d->ep0_i, &d->ep0_cc, 0, 0,
              TRB_TC(TRB_STATUS) | TRB_IOC | st_dir);

    g_ep0_done = 0;
    mmio32w(g_xhci_db + d->slot * 4u, 1);    // doorbell: slot, EP0(target=1)
    for (int k = 0; k < 40000 && !g_ep0_done; k++) usb_poll();
    if (!g_ep0_done) {
        uint16_t last = pit_read();
        int wraps = 0;
        while (!g_ep0_done && wraps < 50) { pit_wait_wrap(&last); usb_poll(); wraps++; }
    }
    if (!g_ep0_done) {
        gprintfa(3, "[usb] ep0 TIMEOUT slot=%u setup=%x%x trb0=%x db=%x sts=%x\n",
                 d->slot,
                 ring_ptr(d->ep0_ring, 0)->p1, ring_ptr(d->ep0_ring, 0)->p0,
                 ring_ptr(d->ep0_ring, 0)->c,
                 g_xhci_db + d->slot * 4u,
                 mmio32(g_xhci_op + OP_USBSTS));
        return -2;
    }
    if (g_ep0_ev_code != 1)
        gprintfa(3, "[usb] ep0 ccode=%u slot=%u\n", g_ep0_ev_code, d->slot);
    return (g_ep0_ev_code == 1) ? 0 : (int)(g_ep0_ev_code | 0x100);
}

// ====================== IN 中断端点: 挂一个 NORMAL TRB 收报告 ======================
static void usb_arm_in(usb_dev_t *d) {
    ring_push(d->ein_ring, &d->ein_i, &d->ein_cc, d->ein_buf, (uint32_t)d->in_mps,
              TRB_TC(TRB_NORMAL) | TRB_IOC);
    mmio32w(g_xhci_db + d->slot * 4u, 3u);   // doorbell: slot, EP1 IN(target=3)
}

// ====================== 事件环轮询 ======================
static void usb_poll(void) {
    if (!g_xhci_ok) return;
    usb_trb_t *er = ring_ptr(g_evt_ring, 0);
    for (int n = 0; n < 32; n++) {
        usb_trb_t *e = &er[g_evt_i];
        if ((e->c & 1u) != g_evt_cc) break;
        uint32_t type = (e->c >> 10) & 0x3Fu;
        if (type == TRB_EV_TRANSFER) {
            uint32_t slot = (e->c >> 24) & 0xFFu;
            uint32_t ep = (e->c >> 16) & 0x1Fu;
            uint32_t code = (e->s >> 24) & 0xFFu;
            uint32_t remain = e->s & 0x00FFFFFFu;
            usb_dev_t *d = usb_find_slot(slot);
            if (d && ep == 3 && code == 1) {
                int got = (int)d->in_mps - (int)remain;
                if (got < 0 || got > 64) got = 0;
                uint8_t *r = (uint8_t *)d->ein_buf;
                d->reports++;
                g_usb_reports++;
                if (d->is_kbd && got >= 2) {
                    uint8_t mod = r[0];
                    int shift = (mod & 0x22) ? 1 : 0;
                    for (int k = 0; k < 6; k++) {
                        uint8_t u = r[2 + k];
                        if (!u) continue;
                        int old = 0;
                        for (int j = 0; j < 6; j++) if (d->prev_keys[j] == u) { old = 1; break; }
                        if (old) continue;
                        if (u == 0x39) {                    // CapsLock
                            kb_caps = !kb_caps;
                        } else if (u >= 4 && u <= 0x38) {
                            uint8_t a = usb_usage_ascii(u, shift);
                            if (a) {
                                usb_keyq[usb_keyq_h] = a;
                                usb_keyq_h = (usb_keyq_h + 1) & 15;
                                if (usb_keyq_h == usb_keyq_t) usb_keyq_t = (usb_keyq_t + 1) & 15;
                                d->keys++;
                            }
                        }
                    }
                    for (int k = 0; k < 6; k++) d->prev_keys[k] = r[2 + k];
                } else if (d->is_mouse && got >= 3) {
                    int b = r[0] & 7;
                    // 逐 TRB 边沿锁存: 一个 tick 可能批量处理 down/up 多份报告,
                    // 直接覆盖 cur_btn 会丢按下; 任何一份报告出现 0->1 即锁存点击事件。
                    if ((b & 1) && !(s_raw_btn_prev & 1)) { g_btn_press = 1; }
                    s_raw_btn_prev = b;
                    cur_btn = b;
                    int mdx = (signed char)r[1], mdy = (signed char)r[2];
                    g_last_dx = mdx; g_last_dy = mdy;
                    cur_x += mdx * 2;           // x2 提高可见速度
                    cur_y += mdy * 2;
                    int sw = (int)g_fb_w, sh = (int)g_fb_h;
                    if (cur_x < 0) cur_x = 0;
                    if (cur_y < 0) cur_y = 0;
                    if (cur_x > sw - 9) cur_x = sw - 9;   // 含 +1 投影, 留 9px
                    if (cur_y > sh - 9) cur_y = sh - 9;
                } else if (d->is_digi && got >= d->digi_need) {
                    // 触摸屏/数位板: 绝对坐标直接落点 (触摸=移动+按下, 与鼠标共用光标)
                    int tip = (d->digi_tip_off >= 0)
                            ? (int)dg_bits(r, d->digi_tip_off, 1)
                            : (int)(r[0] & 1u);           // 无 Tip 位: 按键1 等效
                    uint32_t px = dg_bits(r, d->digi_x_off, d->digi_x_bits);
                    uint32_t py = dg_bits(r, d->digi_y_off, d->digi_y_bits);
                    if (tip && !(s_raw_btn_prev & 1)) g_btn_press = 1;
                    s_raw_btn_prev = tip;
                    cur_btn = tip ? 1 : 0;
                    g_last_dx = (int)px; g_last_dy = (int)py;
                    if (d->digi_x_max) cur_x = (int)((uint64_t)px * (uint32_t)(g_fb_w - 9) / d->digi_x_max);
                    if (d->digi_y_max) cur_y = (int)((uint64_t)py * (uint32_t)(g_fb_h - 9) / d->digi_y_max);
                    if (cur_x < 0) cur_x = 0;
                    if (cur_y < 0) cur_y = 0;
                    if (cur_x > (int)g_fb_w - 9) cur_x = (int)g_fb_w - 9;
                    if (cur_y > (int)g_fb_h - 9) cur_y = (int)g_fb_h - 9;
                }
                usb_arm_in(d);
            } else if (d && ep == 3) {
                usb_arm_in(d);                            // 出错也重新挂上
            } else if (ep == 1) {
                g_ep0_ev_code = code;
                g_ep0_done = 1;
            }
        } else if (type == TRB_EV_COMPLETION) {
            g_cmd_ev_code = (e->s >> 24) & 0xFFu;
            g_cmd_ev_slot = (e->c >> 24) & 0xFFu;
            g_cmd_done = 1;
        } else if (type == TRB_EV_PORT) {
            uint32_t p = (e->p0 >> 24) & 0xFFu;
            if (p >= 1 && p <= 32) g_port_chg |= 1u << (p - 1);
        }
        g_evt_i++;
        if (g_evt_i == ER_N) { g_evt_i = 0; g_evt_cc ^= 1; }
    }
    mmio32w(g_xhci_rt + 0x20u + 0x18u, (g_evt_ring + g_evt_i * 16u) | (1u << 3));
}

// ====================== HID usage -> ASCII ======================
static uint8_t usb_usage_ascii(uint8_t u, int shift) {
    if (u >= 4 && u <= 29) {                              // a..z
        uint8_t c = 'a' + (u - 4);
        if (shift ^ (kb_caps ? 1 : 0)) c = (uint8_t)(c - 'a' + 'A');
        return c;
    }
    if (u >= 30 && u <= 39) {                             // 1..9,0 (HID 0x1E..0x27)
        static const uint8_t dig[10] = {'1','2','3','4','5','6','7','8','9','0'};
        static const uint8_t dsh[10] = {'!','@','#','$','%','^','&','*','(',')'};
        uint8_t c = shift ? dsh[u - 30] : dig[u - 30];
        return c;
    }
    if (u == 40) return 0x0A;                             // Enter  (0x28)
    if (u == 41) return 0x1B;                             // Esc    (0x29)
    if (u == 42) return 0x08;                             // Backspace (0x2A)
    if (u == 43) return '\t';                             // Tab    (0x2B)
    if (u == 44) return ' ';                              // Space  (0x2C)
    switch (u) {                                          // 符号行 (0x2D..0x38)
        case 45: return shift ? '_' : '-';
        case 46: return shift ? '+' : '=';
        case 47: return shift ? '{' : '[';
        case 48: return shift ? '}' : ']';
        case 49: return shift ? '|' : '\\';
        case 50: return shift ? '~' : '#';
        case 51: return shift ? ':' : ';';
        case 52: return shift ? '"' : '\'';
        case 53: return shift ? '~' : '`';
        case 54: return shift ? '<' : ',';
        case 55: return shift ? '>' : '.';
        case 56: return shift ? '?' : '/';
        default: return 0;
    }
}

// ====================== 光标 (底图保存/恢复) ======================
static uint32_t cur_getpx(int x, int y) {
    volatile uint8_t *p = g_fb + (uint32_t)y * g_pitch + (uint32_t)x * g_bppb;
    if (g_bppb == 4) return *(volatile uint32_t *)p;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}
static void cur_putpx(int x, int y, uint32_t c) {
    volatile uint8_t *p = g_fb + (uint32_t)y * g_pitch + (uint32_t)x * g_bppb;
    if (g_bppb == 4) { *(volatile uint32_t *)p = c; }
    else { p[0] = (uint8_t)c; p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)(c >> 16); }
}
// LFB 恒等映射为 WB 回写缓存; 光标写入后需 CLFLUSH 回写, 硬件/抓屏才能看到
// TCC 内建汇编器不识别 clflush 助记符, 直接用操作码: clflush byte ptr [eax] = 0F AE /7 (ModRM 0x38)
static void cur_clflush(const volatile char *p) {
    __asm__ __volatile__(".byte 0x0F, 0xAE, 0x38" : : "a"((uint32_t)p) : "memory");
}
static void cur_sfence(void) {
    __asm__ __volatile__(".byte 0x0F, 0xAE, 0xF8" : : : "memory"); // sfence
}
static void cur_wbinvd(void) {
    __asm__ __volatile__("wbinvd" : : : "memory");
}
// 检测缓存行大小 (cpuid leaf 1, EBX[15:8] = 缓存行大小 in bytes, 需 *8)
static uint32_t cur_cacheline(void) {
    uint32_t a = 1, b = 0, c = 0, d = 0;
    __asm__ __volatile__("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    uint32_t cls = ((b >> 8) & 0xFFu) * 8u;
    if (cls < 32) cls = 64;
    return cls;
}
static void cur_flush_box(int x, int y) {
    // LFB 为 WB: 刷新光标盒覆盖的所有缓存行, 让 GPU 读到最新像素
    static uint32_t cls = 0;
    if (!cls) cls = cur_cacheline();
    cur_sfence();
    for (int r = 0; r < 9; r++) {
        uint32_t row = (uint32_t)(g_fb + (uint32_t)(y + r) * g_pitch + (uint32_t)x * g_bppb);
        uint32_t a = row & ~(cls - 1u);
        uint32_t end = row + (uint32_t)9 * g_bppb;
        for (; a < end; a += cls) cur_clflush((const volatile char *)a);
    }
    cur_sfence();
}
static void usb_cursor_erase_at(int x, int y) {
    if (!cur_drawn) return;
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++) cur_putpx(x + c, y + r, cur_bg[r * 9 + c]);
    cur_flush_box(x, y);
    cur_drawn = 0;
}
static void usb_cursor_stamp(void) {
    uint32_t col = cur_btn ? C_CYAN : 0xFFFFFFu;
    for (int r = 0; r < 8; r++) {
        uint8_t bits = cur_arrow[r];
        for (int c = 0; c < 8; c++)
            if (bits & (0x80u >> c)) {
                cur_putpx(cur_x + c + 1, cur_y + r + 1, 0x000000u);   // 投影
                cur_putpx(cur_x + c, cur_y + r, col);
            }
    }
    cur_flush_box(cur_x, cur_y);   // 回写光标缓存行, 非一致 GPU 立即可见
}
static void usb_cursor_draw(void) {
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++) cur_bg[r * 9 + c] = cur_getpx(cur_x + c, cur_y + r);
    usb_cursor_stamp();
    cur_drawn = 1;
}

// ====================== 端口扫描 / 设备枚举 ======================
static int usb_enumerate(int port, int speed);

static void usb_check_ports(void) {
    if (!g_xhci_ok) return;
    for (uint32_t p = 1; p <= g_xhci_maxports && p <= 32; p++) {
        if (g_port_chg & (1u << (p - 1))) {
            g_port_chg &= ~(1u << (p - 1));
            uint32_t s = portsc((int)p);
            portsc_set((int)p, s);                        // 写回清除变化位
            if (s & 0x2u) g_port_fail &= ~(1u << (p - 1));  // CSC: 重新插拔, 允许重试
            usb_dev_t *d = usb_find_port((int)p);
            if (d && !(s & PS_CCS)) {                     // 拔出
                d->active = 0;
                g_port_fail &= ~(1u << (p - 1));
                gprintfa(3, "[usb] port%u: device removed\n", p);
            } else if (!d && (s & PS_CCS) && !(s & PS_PED)
                       && !(g_port_fail & (1u << (p - 1)))) {
                int sp = usb_reset_port((int)p);
                if (sp >= 1 && sp <= 3) {
                    if (usb_enumerate((int)p, sp) != 0) g_port_fail |= 1u << (p - 1);
                }
            }
        }
    }
}

static int usb_dev_alloc(void) {
    for (int i = 0; i < USB_MAX_DEV; i++) if (!usb_dev[i].active) return i;
    return -1;
}

static int usb_enumerate(int port, int speed) {
    gprintf("[usb] port%d: speed=%d enumerating...\n", port, speed);
    uint32_t slot = 0;
    int di = usb_dev_alloc();
    if (di < 0) { gprintfa(3, "[usb] no free device slot\n"); return -1; }
    usb_dev_t *d = &usb_dev[di];
    for (int i = 0; i < (int)sizeof(usb_dev_t); i++) ((uint8_t *)d)[i] = 0;
    d->rhport = port; d->speed = speed; d->active = 1;

    if (usb_cmd(0, 0, TRB_TC(TRB_ENABLE_SLOT), &slot) != 0) {
        gprintfa(3, "[usb] enable slot failed\n"); d->active = 0; return -1;
    }
    d->slot = (int)slot;

    d->dev_ctx = usb_dma((uint32_t)(32 * g_xhci_csz), 64);
    d->input_ctx = usb_dma((uint32_t)(33 * g_xhci_csz), 64);
    d->ep0_ring = usb_dma(RING_N * 16u, 16);
    d->ein_ring = usb_dma(RING_N * 16u, 16);
    d->ein_buf = usb_dma(64, 16);
    uint32_t desc = usb_dma(512, 8);
    if (!d->dev_ctx || !d->input_ctx || !d->ep0_ring || !d->ein_ring || !d->ein_buf || !desc) {
        gprintfa(3, "[usb] dma pool exhausted\n"); d->active = 0; return -1;
    }
    for (uint32_t i = 0; i < (uint32_t)(32 * g_xhci_csz); i += 4) mmio32w(d->dev_ctx + i, 0);
    for (uint32_t i = 0; i < (uint32_t)(33 * g_xhci_csz); i += 4) mmio32w(d->input_ctx + i, 0);
    for (uint32_t i = 0; i < RING_N * 16u; i += 4) { mmio32w(d->ep0_ring + i, 0); mmio32w(d->ein_ring + i, 0); }
    // LINK TRB (两个环)
    ring_ptr(d->ep0_ring, RING_N - 1)->p0 = d->ep0_ring;
    ring_ptr(d->ep0_ring, RING_N - 1)->c = TRB_TC(TRB_LINK) | TRB_LINK_TC | 1u;
    ring_ptr(d->ein_ring, RING_N - 1)->p0 = d->ein_ring;
    ring_ptr(d->ein_ring, RING_N - 1)->c = TRB_TC(TRB_LINK) | TRB_LINK_TC | 1u;
    d->ep0_i = 0; d->ep0_cc = 1; d->ein_i = 0; d->ein_cc = 1;

    // DCBAAP[slot] = 设备上下文
    mmio32w(g_dcbaa + slot * 8u, d->dev_ctx);
    mmio32w(g_dcbaa + slot * 8u + 4u, 0);

    // 输入上下文: A0|A1, slot ctx(speed, port, CE=1), EP0 ctx(ctrl, MPS)
    uint32_t mps0 = (speed == 2) ? 8u : 64u;              // low speed=8, 其它=64
    mmio32w(d->input_ctx + 4u, 0x3u);                     // ctrl ctx dw1: add flags A0|A1
    uint32_t sctx = d->input_ctx + (uint32_t)g_xhci_csz;
    mmio32w(sctx, ((uint32_t)speed << 20) | (1u << 27));
    mmio32w(sctx + 4u, (uint32_t)port << 16);             // RH port (bits 23:16)
    uint32_t e0 = d->input_ctx + 2u * (uint32_t)g_xhci_csz;
    mmio32w(e0 + 4u, 3u | (4u << 3) | (mps0 << 16));      // CErr=3, type=4(ctrl), MPS
    mmio32w(e0 + 8u, d->ep0_ring | 1u);                   // dw2: TR deq ptr lo | DCS
    mmio32w(e0 + 12u, 0u);                                // dw3: TR deq ptr hi (环<4GB)
    mmio32w(e0 + 16u, 8u << 16);                          // dw4: avg TRB length

    if (usb_cmd(d->input_ctx, 0, TRB_TC(TRB_ADDR_DEV) | (slot << 24), 0) != 0) {
        gprintfa(3, "[usb] address device failed\n"); d->active = 0; return -1;
    }

    // GET_DESCRIPTOR(DEVICE, 18)
    if (usb_ctrl(d->slot, 0x80, 0x06, 0x0100, 0, 18, desc) != 0) {
        gprintfa(3, "[usb] get device descriptor failed\n"); d->active = 0; return -1;
    }
    uint8_t *dd = (uint8_t *)desc;
    uint8_t mps0b = dd[7];
    d->vid = (uint32_t)dd[8] | ((uint32_t)dd[9] << 8);
    d->did = (uint32_t)dd[10] | ((uint32_t)dd[11] << 8);
    if (mps0b && mps0b != mps0 && mps0b <= 64) {          // 重配 EP0 MPS
        for (uint32_t i = 0; i < (uint32_t)(33 * g_xhci_csz); i += 4) mmio32w(d->input_ctx + i, 0);
        mmio32w(d->input_ctx + 4u, 0x3u);                 // A0(slot) | A1(ep0)
        mmio32w(sctx, ((uint32_t)speed << 20) | (1u << 27));
        mmio32w(sctx + 4u, (uint32_t)port << 16);
        mmio32w(e0 + 4u, 3u | (4u << 3) | ((uint32_t)mps0b << 16));
        // dequeue 指向当前生产者位置 (前面 GET_DESC 已消耗 3 条)
        mmio32w(e0 + 8u, d->ep0_ring + d->ep0_i * 16u | (d->ep0_cc & 1u));
        mmio32w(e0 + 12u, 0u);                            // deq ptr hi = 0
        mmio32w(e0 + 16u, 8u << 16);                      // avg TRB length (dw4)
        // 非致命: QEMU 的 Configure Endpoint 拒绝 A1(返回 TRB 错误), 但其 EP0 仍按原 MPS 工作
        if (usb_cmd(d->input_ctx, 0, TRB_TC(TRB_CONFIG_EP) | (slot << 24), 0) != 0)
            gprintfa(3, "[usb] ep0 mps reconfig not accepted (continuing)\n");
    }

    // GET_DESCRIPTOR(CONFIG 9) -> 全长
    if (usb_ctrl(d->slot, 0x80, 0x06, 0x0200, 0, 9, desc) != 0) {
        gprintfa(3, "[usb] get config(9) failed\n"); d->active = 0; return -1;
    }
    uint32_t total = ((uint8_t *)desc)[2] | (((uint8_t *)desc)[3] << 8);
    if (total > 256) total = 256;
    if (usb_ctrl(d->slot, 0x80, 0x06, 0x0200, 0, (uint16_t)total, desc) != 0) {
        gprintfa(3, "[usb] get config(full) failed\n"); d->active = 0; return -1;
    }

    // 遍历描述符: 找 HID 接口(class=3) + 其中断 IN 端点; 顺带识别 UVC 摄像头
    int hid_proto = 0, hid_iface = -1, found = 0, iface_cls = -1;
    uint32_t ep_mps = 8, ep_iv = 8;
    uint8_t p = 0;
    while (p + 2 <= (uint8_t)total) {
        uint8_t len = dd[p], typ = dd[p + 1];
        if (len < 2 || p + len > (uint8_t)total) break;
        if (typ == 0x04) {                                 // interface
            iface_cls = dd[p + 5];
            if (dd[p + 5] == 3) { hid_iface = dd[p + 2]; hid_proto = dd[p + 7]; }
            else hid_iface = -1;
        } else if (typ == 0x05 && hid_iface >= 0) {        // endpoint
            uint8_t ea = dd[p + 2], at = dd[p + 3];
            if ((ea & 0x80u) && (at & 3u) == 3) {
                ep_mps = (uint32_t)dd[p + 4] | ((uint32_t)dd[p + 5] << 8);
                ep_iv = dd[p + 6];
                found = 1;
                break;
            }
        }
        p = (uint8_t)(p + len);
    }
    if (iface_cls == 0x0E) {                               // UVC 摄像头: 记录后跳过配置
        usb_cam_present = 1;
        gprintf("[usb] port%d: UVC camera %x:%x detected (skipped)\n",
                port, d->vid, d->did);
        d->active = 0;
        return 0;
    }
    // 指纹器 VID 白名单 (Goodix/Validity/Synaptics/FocalTech/LighTuning/Elan)
    if (d->vid == 0x27C6 || d->vid == 0x138A || d->vid == 0x06CB ||
        d->vid == 0x1C7A || d->vid == 0x10A5 || d->vid == 0x04F3) {
        usb_fp_present = 1;
        gprintf("[usb] port%d: fingerprint %x:%x detected\n", port, d->vid, d->did);
    }
    if (!found) {
        gprintf("[usb] port%d: no HID interrupt endpoint (cls=%d)\n", port, iface_cls);
        d->active = 0;
        return -1;
    }
    d->iface = hid_iface;
    d->is_kbd = (hid_proto == 1);
    d->is_mouse = (hid_proto == 2);
    d->in_mps = (ep_mps < 1 || ep_mps > 64) ? 64 : (int)ep_mps;

    if (!d->is_kbd && !d->is_mouse) {
        // proto=0 (触摸屏/数位板/非 boot HID): 读报告描述符判定 digitizer
        uint32_t rd = usb_dma(512, 8);
        if (!rd) { gprintfa(3, "[usb] dma pool exhausted (rdesc)\n"); d->active = 0; return -1; }
        if (usb_ctrl(d->slot, 0x81, 0x06, 0x2200, (uint16_t)d->iface, 256, rd) != 0) {
            gprintf("[usb] port%d: get report descriptor failed\n", port);
            d->active = 0;
            return -1;
        }
        usb_parse_digitizer((const uint8_t *)rd, 256);
        if (!dg_ok) {
            gprintf("[usb] port%d: HID proto=0 not digitizer %x:%x (skipped)\n",
                    port, d->vid, d->did);
            d->active = 0;
            return -1;
        }
        d->is_digi = 1;
        usb_digi_present = 1;
        d->digi_x_off = dg_x_off; d->digi_y_off = dg_y_off; d->digi_tip_off = dg_tip_off;
        d->digi_x_bits = dg_x_bits; d->digi_y_bits = dg_y_bits;
        d->digi_x_max = dg_x_max; d->digi_y_max = dg_y_max;
        d->digi_need = dg_need;
        gprintf("[usb] port%d: DIGITIZER online %x:%x x[%u@b%d max=%u] y[%u@b%d max=%u] tip@%d\n",
                port, d->vid, d->did, d->digi_x_bits, d->digi_x_off, d->digi_x_max,
                d->digi_y_bits, d->digi_y_off, d->digi_y_max, d->digi_tip_off);
    }

    // SET_CONFIGURATION(1) + HID SET_IDLE / SET_PROTOCOL(boot)
    usb_ctrl(d->slot, 0x00, 0x09, 0x0001, 0, 0, 0);
    if (d->is_kbd || d->is_mouse) {
        usb_ctrl(d->slot, 0x21, 0x0A, 0x0000, (uint16_t)d->iface, 0, 0);   // SET_IDLE
        usb_ctrl(d->slot, 0x21, 0x0B, 0x0000, (uint16_t)d->iface, 0, 0);   // SET_PROTOCOL boot
    }

    // CONFIG_EP: 增加 EP1 IN (索引 3)
    for (uint32_t i = 0; i < (uint32_t)(33 * g_xhci_csz); i += 4) mmio32w(d->input_ctx + i, 0);
    mmio32w(d->input_ctx + 4u, (1u << 0) | (1u << 3));    // A0 | A3
    mmio32w(sctx, ((uint32_t)d->speed << 20) | (3u << 27));  // speed, CE=3
    // 输入上下文: control@0, slot@1, DCI1(ep0)@2, DCI2(ep1out)@3, DCI3(ep1in)@4
    uint32_t e3 = d->input_ctx + 4u * (uint32_t)g_xhci_csz;
    mmio32w(e3, ((uint32_t)ep_iv & 0xFFu) << 16);         // interval
    mmio32w(e3 + 4u, 3u | (7u << 3) | (((uint32_t)d->in_mps) << 16));  // type=7 INT IN
    mmio32w(e3 + 8u, d->ein_ring | 1u);                   // dw2: TR deq ptr lo | DCS
    mmio32w(e3 + 12u, 0u);                                // dw3: TR deq ptr hi (环<4GB)
    mmio32w(e3 + 16u, 8u << 16);                          // dw4: avg TRB length
    if (usb_cmd(d->input_ctx, 0, TRB_TC(TRB_CONFIG_EP) | (slot << 24), 0) != 0) {
        gprintfa(3, "[usb] config ep failed\n"); d->active = 0; return -1;
    }
    usb_arm_in(d);
    if (d->is_digi)
        gprintfa(2, "[usb] port%d: TOUCH online (ep MPS=%u iv=%u)\n", port, d->in_mps, ep_iv);
    else if (d->is_kbd)
        gprintfa(2, "[usb] port%d: KEYBOARD online (ep MPS=%u iv=%u)\n", port, d->in_mps, ep_iv);
    else
        gprintfa(2, "[usb] port%d: MOUSE online (ep MPS=%u iv=%u)\n", port, d->in_mps, ep_iv);
    return 0;
}

// ====================== 控制器探测/复位/初始化 ======================
static void usb_input_init(void) {
    // LFB 维持 WB (整片渲染快); 光标小盒用 CLFLUSH 回写给 GPU (真机非一致必需)
    // PS/2 存在性探测: 真机 MateBook 无 i8042, 0x64 恒 0xFF
    int ff = 1;
    for (int i = 0; i < 4; i++) if (inb(0x64) != 0xFF) { ff = 0; break; }
    g_ps2_present = !ff;

    g_xhci_ok = 0;
    usb_pool_top = 0;
    for (uint32_t bus = 0; bus < 256 && !g_xhci_ok; bus++) {
        for (uint32_t dev = 0; dev < 32 && !g_xhci_ok; dev++) {
            uint32_t vd = pci_read32(bus, dev, 0, 0x00);
            if ((vd & 0xFFFFu) == 0xFFFFu || vd == 0) continue;
            for (uint32_t fn = 0; fn < 8; fn++) {
                uint32_t v2 = (fn == 0) ? vd : pci_read32(bus, dev, fn, 0x00);
                if ((v2 & 0xFFFFu) == 0xFFFFu) continue;
                uint32_t cc = pci_read32(bus, dev, fn, 0x08);
                if ((cc >> 8) != 0x0C0330u) continue;     // Serial USB3 xHCI
                g_xhci_pci_b = (int)bus; g_xhci_pci_d = (int)dev; g_xhci_pci_f = (int)fn;

                uint32_t bar0 = pci_read32(bus, dev, fn, 0x10);
                uint32_t bar0h = pci_read32(bus, dev, fn, 0x14);
                if (bar0 & 1u) continue;                  // IO BAR, 不是 xHCI
                uint64_t bar_phys = ((uint64_t)bar0h << 32) | (bar0 & ~0xFull);
                // 打开 memory space + bus master
                uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
                pci_write32(bus, dev, fn, 0x04, cmd | 0x6u);
                // 64 位 BAR: 开 PAE 分页, 映射进 0xE0000000 别名窗
                paging_pae_enable();
                uint32_t mmio = (bar0h) ? paging_map_mmio(bar_phys) : (uint32_t)(bar0 & ~0xFu);
                g_xhci_cap = mmio;

                uint8_t caplen = (uint8_t)(mmio32(mmio) & 0xFFu);
                uint32_t hcs1 = mmio32(mmio + 0x04u);
                uint32_t hcs2 = mmio32(mmio + 0x08u);
                uint32_t hcc1 = mmio32(mmio + 0x10u);
                uint32_t dboff = mmio32(mmio + 0x14u);
                uint32_t rtsoff = mmio32(mmio + 0x18u);
                g_xhci_op = mmio + caplen;
                g_xhci_rt = mmio + (rtsoff & ~0x1Fu);   // 字段即字节偏移(低位保留)
                g_xhci_db = mmio + (dboff & ~0x3u);
                g_xhci_maxports = (hcs1 >> 24) & 0xFFu;
                uint32_t maxslots = hcs1 & 0xFFu;
                g_xhci_csz = (hcc1 & (1u << 2)) ? 64 : 32;
                uint32_t n_scratch = (hcs2 >> 27) & 0x1Fu;

                gprintf("[usb] xHCI %x:%x.%u bar=%x:%x va=0x%x ports=%u slots=%u csz=%d\n",
                        bus, dev, fn, bar0h, (bar0 & ~0xFu), mmio,
                        g_xhci_maxports, maxslots, g_xhci_csz);

                // 停止 + 复位控制器
                uint32_t c = mmio32(g_xhci_op + OP_USBCMD);
                mmio32w(g_xhci_op + OP_USBCMD, c & ~USBCMD_RS);
                int w = 0;
                while (!(mmio32(g_xhci_op + OP_USBSTS) & USBSTS_HCH) && w++ < 30) usb_wait_wraps(1);
                mmio32w(g_xhci_op + OP_USBCMD, USBCMD_HCRST);
                w = 0;
                while ((mmio32(g_xhci_op + OP_USBCMD) & USBCMD_HCRST) && w++ < 30) usb_wait_wraps(1);
                w = 0;
                while ((mmio32(g_xhci_op + OP_USBSTS) & USBSTS_CNR) && w++ < 30) usb_wait_wraps(1);
                if (mmio32(g_xhci_op + OP_USBSTS) & USBSTS_CNR) {
                    gprintfa(3, "[usb] controller reset timeout\n"); break;
                }

                // 结构体内存
                g_xhci_slots_en = maxslots < 4 ? maxslots : 4;
                if (g_xhci_slots_en < 1) g_xhci_slots_en = 1;
                g_dcbaa = usb_dma(8u * (g_xhci_slots_en + 1u), 64);
                g_cmd_ring = usb_dma(RING_N * 16u, 64);   // CRCR 指针 [63:6], 必须 64B 对齐
                g_evt_ring = usb_dma(ER_N * 16u, 64);
                g_erst = usb_dma(16, 64);
                if (!g_dcbaa || !g_cmd_ring || !g_evt_ring || !g_erst) {
                    gprintfa(3, "[usb] dma pool exhausted (init)\n"); break;
                }
                for (uint32_t i = 0; i < 8u * (g_xhci_slots_en + 1u); i += 4) mmio32w(g_dcbaa + i, 0);
                for (uint32_t i = 0; i < RING_N * 16u; i += 4) mmio32w(g_cmd_ring + i, 0);
                for (uint32_t i = 0; i < ER_N * 16u; i += 4) mmio32w(g_evt_ring + i, 0);
                if (n_scratch) {
                    uint32_t sa = usb_dma(8u * n_scratch, 64);
                    for (uint32_t i = 0; i < n_scratch; i++) {
                        uint32_t b = usb_dma(4096, 4096);
                        mmio32w(sa + i * 8u, b);
                        mmio32w(sa + i * 8u + 4u, 0);
                    }
                    mmio32w(g_dcbaa, sa); mmio32w(g_dcbaa + 4u, 0);
                }
                ring_ptr(g_cmd_ring, RING_N - 1)->p0 = g_cmd_ring;
                ring_ptr(g_cmd_ring, RING_N - 1)->c = TRB_TC(TRB_LINK) | TRB_LINK_TC | 1u;

                // ERST 单段
                mmio32w(g_erst, g_evt_ring);
                mmio32w(g_erst + 4u, 0);
                mmio32w(g_erst + 8u, ER_N);
                mmio32w(g_erst + 12u, 0);
                // 运行时: IR0
                mmio32w(g_xhci_rt + 0x20u + 0x00u, 1u);        // IMAN: 清 IP, IE=0(轮询)
                mmio32w(g_xhci_rt + 0x20u + 0x08u, 1);         // ERSTSZ=1
                mmio32w(g_xhci_rt + 0x20u + 0x10u, g_erst);    // ERSTBA
                mmio32w(g_xhci_rt + 0x20u + 0x14u, 0);
                mmio32w(g_xhci_rt + 0x20u + 0x18u, g_evt_ring | (1u << 3));  // ERDP
                // 运行配置
        mmio32w(g_xhci_op + OP_DCBAAP, g_dcbaa);
        mmio32w(g_xhci_op + OP_DCBAAP + 4u, 0);
        mmio32w(g_xhci_op + OP_CRCR, g_cmd_ring | 1u);     // CRCR 低 + RCS
        mmio32w(g_xhci_op + OP_CRCR + 4u, 0);              // CRCR 高: 写入才提交环初始化
        mmio32w(g_xhci_op + OP_CONFIG, g_xhci_slots_en);
                mmio32w(g_xhci_op + OP_USBCMD, USBCMD_RS);     // 跑起来 (INTE=0)
                for (int k = 0; k < 40000; k++)
                    if (!(mmio32(g_xhci_op + OP_USBSTS) & USBSTS_HCH)) break;

                // 端口上电
                for (uint32_t p = 1; p <= g_xhci_maxports && p <= 32; p++) {
                    uint32_t s = portsc((int)p);
                    if (!(s & PS_PP)) portsc_set((int)p, s | PS_PP);
                }
                usb_wait_wraps(2);
                g_xhci_ok = 1;
                break;
            }
        }
    }
    if (!g_xhci_ok) {
        gprintfa(3, "[usb] no xHCI controller found\n");
        return;
    }
    // 初次扫描: 已连接设备复位+枚举
    for (uint32_t p = 1; p <= g_xhci_maxports && p <= 32; p++) {
        uint32_t s = portsc((int)p);
        portsc_set((int)p, s);                                 // 清变化位
        if ((s & PS_CCS) && !(s & PS_PED)) {
            int sp = usb_reset_port((int)p);
            if (sp >= 1 && sp <= 3) {
                if (usb_enumerate((int)p, sp) != 0) g_port_fail |= 1u << (p - 1);
                usb_wait_wraps(1);
            }
        }
    }
    cur_x = (int)g_fb_w / 2; cur_y = (int)g_fb_h / 2;
}

// ====================== 键盘取出 / 鼠标 tick ======================
static int usb_kb_getc(void) {
    if (usb_keyq_h == usb_keyq_t) return -1;
    int c = usb_keyq[usb_keyq_t];
    usb_keyq_t = (usb_keyq_t + 1) & 15;
    return c;
}

// 主循环每 tick 调用: 轮询事件 + 移动 + 顶层绘制光标
// 持久光标: 仅在位置/按键变化时擦旧画新; 静止时每帧补盖字形(幂等, 抵消背景重绘)
static int s_cx = -1, s_cy = -1, s_cb = -1;
static void usb_cursor_tick(void) {
    usb_poll();
    usb_check_ports();
    if (cur_x != s_cx || cur_y != s_cy || cur_btn != s_cb) {
        if (cur_drawn) usb_cursor_erase_at(s_cx, s_cy);   // 在上一帧位置恢复底图
        usb_cursor_draw();
        s_cx = cur_x; s_cy = cur_y; s_cb = cur_btn;
    } else {
        usb_cursor_stamp();
    }
}

// 兼容保留 (现主循环不再每帧擦除, 光标持久驻留)
static void usb_cursor_erase_frame(void) {
}

static void usb_status(void) {
    if (!g_xhci_ok) { gprintfa(3, "[usb] xHCI not initialized\n"); return; }
    gprintf("[usb] xHCI %x:%x.%u va=0x%x ports=%u csz=%d ps2=%d pae=%d\n",
            g_xhci_pci_b, g_xhci_pci_d, g_xhci_pci_f, g_xhci_cap,
            g_xhci_maxports, g_xhci_csz, g_ps2_present, pae_on);
    gprintf("[usb] reports=%u pool=%u/%uKB cursor=(%d,%d) btn=%d last=(%d,%d)\n",
            (uint32_t)g_usb_reports,
            usb_pool_top / 1024u, USB_POOL_SIZE / 1024u,
            cur_x, cur_y, cur_btn, g_last_dx, g_last_dy);
    gprintf("[usb] touch=%d finger=%d camera=%d\n",
            usb_digi_present, usb_fp_present, usb_cam_present);
    for (int i = 0; i < USB_MAX_DEV; i++) {
        if (!usb_dev[i].active) continue;
        gprintf("[usb] dev%d slot=%d port=%d %s%s%s %x:%x reports=%u keys=%u\n",
                i, usb_dev[i].slot, usb_dev[i].rhport,
                usb_dev[i].is_kbd ? "KEYBOARD " : "",
                usb_dev[i].is_mouse ? "MOUSE " : "",
                usb_dev[i].is_digi ? "TOUCH " : "",
                usb_dev[i].vid, usb_dev[i].did,
                (uint32_t)usb_dev[i].reports, (uint32_t)usb_dev[i].keys);
    }
}

#endif // USB_XHCI_H
