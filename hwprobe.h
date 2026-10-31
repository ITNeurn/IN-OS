// hwprobe.h - 硬件检测: PCI 全域扫描 + ACPI 表签名/DSDT _HID + 外设状态汇总
// 依赖: usb_xhci.h / i2c_hid.h 先于本文件引入
//       (pci_read32, g_fb_w/h, usb_dev[USB_MAX_DEV], usb_digi/fp/cam_present, i2c_* 状态)
// 真机流程: efi64.asm 把 RSDP 物理地址写 0x510 -> 这里读出并走 XSDT/RSDT。
#ifndef HWPROBE_H
#define HWPROBE_H

// ---------------- 探测结果 (文件级, 供检测屏渲染) ----------------
#define HW_PCI_MAX 256
static unsigned int hw_pci_loc[HW_PCI_MAX];   // (bus<<8)|(dev<<3)|fn
static unsigned int hw_pci_id [HW_PCI_MAX];   // (ven<<16)|devid
static unsigned int hw_pci_cls[HW_PCI_MAX];   // 24bit class code
static int hw_pci_n = 0;
static unsigned int hw_rsdp = 0;              // 0 = 无 ACPI RSDP
static int hw_xsdt = 0;                       // 1 = 走 XSDT (ACPI 2.0+)
static char hw_acpi_sig[8][5];                // 表签名 (RSDT/XSDT 项)
static int hw_acpi_n = 0;
static char hw_hid_id[12][12];                // DSDT "_HID" 字符串型设备 ID
static int hw_hid_n = 0;
static int hw_touch_hint = 0;                 // DSDT 出现触摸屏厂商 ID (WCOM/ELAN/SYNA/...)
static int hw_fp_hint = 0;                    // DSDT 出现指纹厂商 ID (FPC...)
static int hw_cam_hint = 0;                   // DSDT 出现 MIPI 摄像头/ISP ID (GCTI/INT3470/OVTI)
static int hw_acpi_done = 0;                  // hw_acpi 幂等门 (I2C 初始化前会提前调一次)
static int hw_kb_n = 0, hw_ms_n = 0;          // USB 键盘/鼠标汇总
static int hw_gop_w = 0, hw_gop_h = 0;        // GOP 面板分辨率

// ---------------- 字符串/数字助手 (TCC 无 libc) ----------------
static void hw_itoa(char *d, int v) {
    char t[12]; int n = 0;
    if (v == 0) { d[0] = '0'; d[1] = 0; return; }
    if (v < 0) { *d++ = '-'; v = -v; }
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *d++ = t[--n];
    *d = 0;
}
static void hw_hex2(char *d, unsigned v) {
    const char *h = "0123456789ABCDEF";
    d[0] = h[(v >> 4) & 0xF]; d[1] = h[v & 0xF]; d[2] = 0;
}
static void hw_hex4(char *d, unsigned v) {
    const char *h = "0123456789ABCDEF";
    for (int i = 0; i < 4; i++) d[i] = h[(v >> ((3 - i) * 4)) & 0xF];
    d[4] = 0;
}
static void hw_hex6(char *d, unsigned v) {
    const char *h = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) d[i] = h[(v >> ((5 - i) * 4)) & 0xF];
    d[6] = 0;
}
static void hw_strcat(char *d, const char *s) {
    while (*d) d++;
    while ((*d++ = *s++)) { }
}

// ---------------- DSDT AML 抓 "_HID" 字符串型设备 ID (限次防御) ----------------
// 只做字节扫描不解析 AML 语义: "_HID" 之后 12 字节内找 0x0D (StringOp),
// 收 ASCII 至 0x00/非可打印。真实触摸/指纹在 DSDT 里几乎都是字符串 ID。
static void hw_scan_dsdt(uint32_t dsdt_phys, uint32_t dsdt_len) {
    if (!dsdt_phys || dsdt_phys > 0xD0000000u || dsdt_len < 64) return;
    if (dsdt_len > 512u * 1024u) dsdt_len = 512u * 1024u;
    const uint8_t *p = (const uint8_t *)dsdt_phys;   // 恒等映射 (0..3.5GB)
    for (uint32_t i = 0; i + 16 < dsdt_len && hw_hid_n < 12; i++) {
        if (p[i] != '_' || p[i + 1] != 'H' || p[i + 2] != 'I' || p[i + 3] != 'D')
            continue;
        for (uint32_t j = i + 4; j < i + 12 && j + 2 < dsdt_len; j++) {
            if (p[j] != 0x0D) continue;              // StringPrefixOp
            uint32_t k = j + 1, c = 0;
            while (k < dsdt_len && p[k] >= 0x20 && p[k] < 0x7F && c < 11)
                hw_hid_id[hw_hid_n][c++] = (char)p[k++];
            hw_hid_id[hw_hid_n][c] = 0;
            if (c >= 3) {
                const char *s = hw_hid_id[hw_hid_n];
                if (s[0]=='E' && s[1]=='L' && s[2]=='A') hw_touch_hint = 1;  // ELAN
                if (s[0]=='S' && s[1]=='Y' && s[2]=='N') hw_touch_hint = 1;  // SYNA
                if (s[0]=='P' && s[1]=='I' && s[2]=='X') hw_touch_hint = 1;  // PIXA
                if (s[0]=='W' && s[1]=='C' && s[2]=='O') hw_touch_hint = 1;  // WCOM (Wacom I2C)
                if (s[0]=='M' && s[1]=='S' && s[2]=='F') hw_touch_hint = 1;  // MSFT0001
                if (s[0]=='G' && s[1]=='X' && s[2]=='T') hw_touch_hint = 1;  // GXTP (Goodix)
                if (s[0]=='F' && s[1]=='T' && s[2]=='S') hw_touch_hint = 1;  // FTS (Focaltech)
                if (s[0]=='N' && s[1]=='V' && s[2]=='T') hw_touch_hint = 1;  // NVT (Novatek)
                if (s[0]=='F' && s[1]=='P' && s[2]=='C') hw_fp_hint = 1;     // FPC 指纹
                if (s[0]=='G' && s[1]=='C' && s[2]=='T' && s[3]=='I') hw_cam_hint = 1; // GC sensor
                if (s[0]=='O' && s[1]=='V' && s[2]=='T' && s[3]=='I') hw_cam_hint = 1; // OmniVision
                if (s[0]=='I' && s[1]=='N' && s[2]=='T' && s[3]=='3'
                              && s[4]=='4' && s[5]=='7') hw_cam_hint = 1;    // INT347x camera
                hw_hid_n++;
            }
            break;
        }
    }
}

// 在一张 AML 表 (DSDT/SSDT) 上跑全部扫描
static void hw_scan_aml(uint32_t phys, uint32_t len) {
    hw_scan_dsdt(phys, len);
    i2c_acpi_note((const uint8_t *)phys, len);
}

// ---------------- ACPI: RSDP(0x510) -> RSDT/XSDT -> FACP -> DSDT ----------------
static void hw_acpi(void) {
    if (hw_acpi_done) return;
    hw_acpi_done = 1;
    hw_rsdp = *(volatile unsigned int *)0x510;       // efi64.asm 约定地址
    if (!hw_rsdp || hw_rsdp > 0xD0000000u) { hw_rsdp = 0; return; }
    const uint8_t *r = (const uint8_t *)hw_rsdp;
    if (r[0]!='R' || r[1]!='S' || r[2]!='D' || r[3]!=' ' ||
        r[4]!='P' || r[5]!='T' || r[6]!='R' || r[7]!=' ') { hw_rsdp = 0; return; }
    uint8_t rev = r[15];
    uint64_t tb = 0;
    if (rev >= 2) { tb = *(const uint64_t *)(r + 24); hw_xsdt = 1; }
    if (!tb || (tb >> 32) || tb > 0xD0000000ull) {   // 高于恒等映射窗则退回 RSDT
        tb = *(const unsigned int *)(r + 16);
        hw_xsdt = 0;
        if (!tb || tb > 0xD0000000ull) return;
    }
    const uint8_t *h = (const uint8_t *)(uint32_t)tb;
    uint32_t tlen = *(const unsigned int *)(h + 4);
    int esz = hw_xsdt ? 8 : 4;
    int cnt = (int)((tlen - 36) / esz);
    if (cnt < 0 || cnt > 32) cnt = 32;
    for (int i = 0; i < cnt; i++) {
        uint64_t ep = hw_xsdt
            ? *(const uint64_t *)(h + 36 + (uint32_t)i * 8u)
            : (uint64_t)*(const unsigned int *)(h + 36 + (uint32_t)i * 4u);
        if (!ep || ep > 0xD0000000ull) continue;
        const uint8_t *th = (const uint8_t *)(uint32_t)ep;
        if (hw_acpi_n < 8) {
            for (int c = 0; c < 4; c++) hw_acpi_sig[hw_acpi_n][c] = (char)th[c];
            hw_acpi_sig[hw_acpi_n][4] = 0;
            hw_acpi_n++;
        }
        if (th[0]=='F' && th[1]=='A' && th[2]=='C' && th[3]=='P') {
            uint32_t dp = *(const unsigned int *)(th + 40);      // DSDT (32bit)
            if ((!dp || dp > 0xD0000000u) && rev >= 2) {
                uint64_t dx = *(const uint64_t *)(th + 140);     // X_DSDT
                if (dx && !(dx >> 32) && dx <= 0xD0000000ull) dp = (uint32_t)dx;
            }
            if (dp && dp <= 0xD0000000u) {
                uint32_t dl = *(const unsigned int *)((const uint8_t *)dp + 4);
                hw_scan_aml(dp, dl);
            }
        }
        if (th[0]=='S' && th[1]=='S' && th[2]=='D' && th[3]=='T') {
            // WCOM485B 等设备可能放在 SSDT 而不是主 DSDT
            uint32_t sl = *(const unsigned int *)(th + 4);
            if (sl >= 64u && sl <= 2u * 1024u * 1024u)
                hw_scan_aml((uint32_t)ep, sl);
        }
    }
}

// ---------------- PCI 全域扫描 (空总线跳过, 最多 256 条) ----------------
static void hw_pci_scan(void) {
    for (unsigned bus = 0; bus < 256 && hw_pci_n < HW_PCI_MAX; bus++) {
        uint32_t v0 = pci_read32(bus, 0, 0, 0x00);
        if ((v0 & 0xFFFFu) == 0xFFFFu) continue;     // 约定: 无 0.0 设备即空总线
        for (unsigned dev = 0; dev < 32 && hw_pci_n < HW_PCI_MAX; dev++) {
            for (unsigned fn = 0; fn < 8 && hw_pci_n < HW_PCI_MAX; fn++) {
                uint32_t vd = pci_read32(bus, dev, fn, 0x00);
                if ((vd & 0xFFFFu) == 0xFFFFu) {
                    if (fn == 0) break;              // 单功能设备
                    continue;
                }
                hw_pci_loc[hw_pci_n] = (bus << 8) | (dev << 3) | fn;
                hw_pci_id [hw_pci_n] = vd;
                hw_pci_cls[hw_pci_n] = pci_read32(bus, dev, fn, 0x08) >> 8;
                hw_pci_n++;
            }
        }
    }
}

// ---------------- class code -> 短名 (ASCII, 避免字库缺字) ----------------
static const char *hw_cls_name(unsigned c) {
    unsigned b = (c >> 16) & 0xFFu, sc = (c >> 8) & 0xFFu;
    if (b == 0x03) return "Display";
    if (b == 0x02) return "Network";
    if (b == 0x0C && sc == 0x03) return "USB xHCI";
    if (b == 0x0C && sc == 0x05) return "SMBus/I2C";
    if (b == 0x0C) return "SerialBus";
    if (b == 0x01) return "Storage";
    if (b == 0x06 && sc == 0x04) return "PCIe Bridge";
    if (b == 0x06) return "Bridge";
    if (b == 0x04) return "Multimedia";
    if (b == 0x0B) return "Processor";
    if (b == 0x05) return "Memory";
    if (b == 0x08) return "Generic";
    return "Other";
}

// ---------------- 汇总探测 (boot 一次) ----------------
static void hw_probe(void) {
    hw_gop_w = (int)g_fb_w;
    hw_gop_h = (int)g_fb_h;
    for (int i = 0; i < USB_MAX_DEV; i++) {
        if (!usb_dev[i].active) continue;
        if (usb_dev[i].is_kbd)   hw_kb_n++;
        if (usb_dev[i].is_mouse) hw_ms_n++;
    }
    hw_acpi();
    hw_pci_scan();
    gprintf("[hw] gop=%dx%d rsdp=%x xsdt=%d acpi=%d pci=%d dsdt_hid=%d kb=%d ms=%d\n",
            hw_gop_w, hw_gop_h, hw_rsdp, hw_xsdt, hw_acpi_n,
            hw_pci_n, hw_hid_n, hw_kb_n, hw_ms_n);
}

#endif // HWPROBE_H
