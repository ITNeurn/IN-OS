// ui_shell.h - HarmonyOS 风格平板 UI 外壳 (静态数据, USB 键鼠交互)
// 逻辑分辨率 2160x1400 (MateBook E 设计稿), 运行时整数缩放到 GOP 实际分辨率
// 依赖: gui_cn.h (三档中文字库), usb_xhci.h (cur_x/cur_y/cur_btn/usb_kb_getc),
//       fb_fill/fb_frame/fb_hline/fb_vline, inb
#ifndef UI_SHELL_H
#define UI_SHELL_H

#define UI_LW 2160
#define UI_LH 1400

extern void rtc_now(int *h, int *m, int *s);
static char ui_tm_buf[8];
static const char *ui_time_hhmm(void) {
    int h, m, s; rtc_now(&h, &m, &s);
    ui_tm_buf[0] = '0' + (h / 10); ui_tm_buf[1] = '0' + (h % 10);
    ui_tm_buf[2] = ':';
    ui_tm_buf[3] = '0' + (m / 10); ui_tm_buf[4] = '0' + (m % 10);
    ui_tm_buf[5] = 0;
    return ui_tm_buf;
}

// ---------------- 配色 (0x00RRGGBB, 取自设计稿采样) ----------------
#define UC_BG       0xEEF2F8u   // 桌面浅灰底
#define UC_WHITE    0xFFFFFFu
#define UC_BLACK    0x1A1F28u
#define UC_BLUE     0x2563E7u   // 主蓝
#define UC_BLUE2    0x3E7BEEu
#define UC_TEXT     0x1D2A40u
#define UC_SUB      0x7A8699u   // 次要灰字
#define UC_CARDLINE 0xE4E9F1u
#define UC_SEL      0xDCE6FAu   // 设置选中浅蓝
// 锁屏渐变 (顶 -> 底)
#define UC_LK0      0x0E1A33u
#define UC_LK1      0x16305Cu
#define UC_LK2      0x2E5E9Au
#define UC_LKCARD   0x213055u   // 锁屏卡片深蓝
#define UC_LKCARD2  0x2B3C66u

static int ui_page = 0;          // 0 锁屏 1 桌面 2 控制中心 3 设置
                                 // 4 负一屏 5 多任务 6 硬件检测 7 消息
static int ui_tiles[8] = {1,1,0,0,0,1,1,0};   // 控制中心开关态
// 多任务页状态
static int mt_open[4] = {1,1,1,1};            // 4 张应用卡开关
static int mt_mode = 0;                        // 0 全屏 1 分屏 2 悬浮窗
// 底部页面圆点: 5 点对应主层级页
static const int DOT_TGT[5] = {1,4,7,5,2};

// ---------------- 逻辑坐标 -> 物理 (等比缩放 + 居中 letterbox) ----------------
// s 为千分比等比缩放, 取宽高较小者; 真机 2160x1440 时 s=1000, 几乎点对点
static int g_ux_s = 1000, g_ux_ox = 0, g_ux_oy = 0;
static int UX(int x) { return g_ux_ox + (int)((long)x * g_ux_s / 1000); }
static int UY(int y) { return g_ux_oy + (int)((long)y * g_ux_s / 1000); }

static int ui_isqrt(long v) {
    if (v <= 0) return 0;
    int x = (int)(v >> 1);
    if (x < 1) x = 1;
    for (int i = 0; i < 12; i++) {
        int n = (x + (int)(v / x)) >> 1;
        if (n >= x) return x;
        x = n;
    }
    return x;
}

// 实心圆角矩形 (逻辑坐标)
static void ui_rrect(int x, int y, int w, int h, int r, uint32_t c) {
    int x0 = UX(x), y0 = UY(y), x1 = UX(x + w) - 1, y1 = UY(y + h) - 1;
    int rr = UX(r);
    int rmax = (x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0);
    if (rr > rmax) rr = rmax;
    if (rr < 1) { fb_fill(x0, y0, x1, y1, c); return; }
    for (int py = y0; py <= y1; py++) {
        int dt = py - y0, db = y1 - py;
        int dy = dt < rr ? rr - dt : (db < rr ? rr - db : 0);
        int inset = 0;
        if (dy > 0) {
            int q = ui_isqrt((long)rr * rr - (long)dy * dy);
            inset = rr - q;
        }
        fb_fill(x0 + inset, py, x1 - inset, py, c);
    }
}

// 垂直渐变
static void ui_vgrad(int x, int y, int w, int h, uint32_t c0, uint32_t c1) {
    int x0 = UX(x), y0 = UY(y), x1 = UX(x + w) - 1, y1 = UY(y + h) - 1;
    int r0 = (c0 >> 16) & 255, g0 = (c0 >> 8) & 255, b0 = c0 & 255;
    int r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
    int rows = y1 - y0 + 1;
    for (int py = y0; py <= y1; py++) {
        int t = rows > 1 ? ((py - y0) * 255 / (rows - 1)) : 0;
        uint32_t r = (uint32_t)(r0 + (r1 - r0) * t / 255);
        uint32_t g = (uint32_t)(g0 + (g1 - g0) * t / 255);
        uint32_t b = (uint32_t)(b0 + (b1 - b0) * t / 255);
        fb_fill(x0, py, x1, py, fb_map((r << 16) | (g << 8) | b));
    }
}

// 实心圆
static void ui_disc(int cx, int cy, int rad, uint32_t c) {
    int px0 = UX(cx - rad), px1 = UX(cx + rad), py0 = UY(cy - rad), py1 = UY(cy + rad);
    int rx = UX(cx + rad) - UX(cx - rad), ry = UY(cy + rad) - UY(cy - rad);
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    int ccx = (px0 + px1) >> 1, ccy = (py0 + py1) >> 1;
    int ax = rx >> 1, ay = ry >> 1;          // rx/ry 是物理直径, 方程要用半轴
    if (ax < 1) ax = 1;
    if (ay < 1) ay = 1;
    long ar = (long)ax * ax * ay * ay;
    for (int py = py0; py <= py1; py++) {
        for (int pxx = px0; pxx <= px1; pxx++) {
            long dx = pxx - ccx, dy = py - ccy;
            if (dx * dx * (long)ay * ay + dy * dy * (long)ax * ax <= ar) {
                if (pxx >= 0 && pxx < g_fb_w && py >= 0 && py < g_fb_h) {
                    volatile uint8_t *p = g_fb + (uint32_t)py * g_pitch + (uint32_t)pxx * g_bppb;
                    uint32_t mc = fb_map(c);
                    if (g_bppb == 4) *(volatile uint32_t *)p = mc;
                    else { p[0] = (uint8_t)mc; p[1] = (uint8_t)(mc >> 8); p[2] = (uint8_t)(mc >> 16); }
                }
            }
        }
    }
}

// 实心等腰三角 (dir: 0 向右, 1 向左), 用于播放按钮
static void ui_tri(int cx, int cy, int s, int dir, uint32_t c) {
    int pmx = UX(cx), pmy = UY(cy);
    int psx = UX(cx + s) - UX(cx);
    int psy = UY(cy + s) - UY(cy);
    if (psx < 1) psx = 1;
    if (psy < 1) psy = 1;
    int xm = pmx - (psx * 7 / 10) / 2;      // 竖边基准 (中心微偏让视觉居中)
    int xt = pmx + (dir ? -psx * 7 / 10 : psx * 7 / 10);
    xm = pmx + (dir ? psx * 7 / 20 : -psx * 7 / 20);
    uint32_t mc = fb_map(c);
    for (int py = -psy; py <= psy; py++) {
        int t = (py < 0 ? -py : py) * 10 / psy;
        int sloped = xt + (xm - xt) * t / 10;
        int l = dir ? sloped : xm;
        int r2 = dir ? xm : sloped;
        if (r2 < l) { int z = l; l = r2; r2 = z; }
        for (int pxx = l; pxx <= r2; pxx++) {
            int yy = pmy + py;
            if (pxx >= 0 && pxx < g_fb_w && yy >= 0 && yy < g_fb_h) {
                volatile uint8_t *p = g_fb + (uint32_t)yy * g_pitch + (uint32_t)pxx * g_bppb;
                if (g_bppb == 4) *(volatile uint32_t *)p = mc;
                else { p[0] = (uint8_t)mc; p[1] = (uint8_t)(mc >> 8); p[2] = (uint8_t)(mc >> 16); }
            }
        }
    }
}

// 实心三角 (顶点朝上, cx 底边中点, by 底边 y), 用于山形/气泡尾巴
static void ui_tri_up(int cx, int by, int hw, int hh, uint32_t c) {
    int pby = UY(by), phw = UX(cx + hw) - UX(cx), phh = UY(by) - UY(by - hh);
    if (phw < 1) phw = 1;
    if (phh < 1) phh = 1;
    if (phh > 4096) phh = 4096;
    uint32_t mc = fb_map(c);
    int pcx = UX(cx);
    for (int py = 0; py <= phh; py++) {
        int w2 = phw * py / phh;
        int x0 = pcx - w2, x1 = pcx + w2, yy = pby - py;
        if (x0 < 0) x0 = 0;
        if (x1 >= g_fb_w) x1 = g_fb_w - 1;
        if (yy >= 0 && yy < g_fb_h && x1 >= x0) fb_fill(x0, yy, x1, yy, mc);
    }
}

// 任意直线 (Bresenham, 逻辑坐标, lw=逻辑粗度近似 2 物理像素)
static void ui_line(int x0, int y0, int x1, int y1, uint32_t c) {
    int ax = UX(x0), ay = UY(y0), bx = UX(x1), by = UY(y1);
    int dx = bx - ax, dy = by - ay;
    int sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int err = dx - dy;
    uint32_t mc = fb_map(c);
    for (;;) {
        if (ax >= 0 && ax < g_fb_w && ay >= 0 && ay < g_fb_h) {
            volatile uint8_t *p = g_fb + (uint32_t)ay * g_pitch + (uint32_t)ax * g_bppb;
            if (g_bppb == 4) *(volatile uint32_t *)p = mc;
            else { p[0] = (uint8_t)mc; p[1] = (uint8_t)(mc >> 8); p[2] = (uint8_t)(mc >> 16); }
        }
        if (ax == bx && ay == by) break;
        int e2 = err << 1;
        if (e2 > -dy) { err -= dy; ax += sx; }
        if (e2 < dx)  { err += dx; ay += sy; }
    }
}

// 进度条
static void ui_bar(int x, int y, int w, int hh, int pct, uint32_t bg, uint32_t fg) {
    ui_rrect(x, y, w, hh, hh / 2, bg);
    int fw = w * pct / 100;
    if (fw > hh) ui_rrect(x, y, fw, hh, hh / 2, fg);
}

// ---------------- 文字辅助 (按等比缩放选最近字库档, 位置走缩放) ----------------
static int g_f16 = 16, g_f22 = 22, g_f160 = 160;   // 运行时选中的实际档位
static void T16 (int x, int y, const char *s, uint32_t c) {
    int px = UX(x), py = UY(y);
    if (g_f16 == 12) fcn12_text(px, py, s, c);
    else             fcn16_text(px, py, s, c);
}
static void T22 (int x, int y, const char *s, uint32_t c) {
    int px = UX(x), py = UY(y);
    if      (g_f22 == 12) fcn12_text(px, py, s, c);
    else if (g_f22 == 16) fcn16_text(px, py, s, c);
    else                  fcn22_text(px, py, s, c);
}
static void T16C(int cx, int y, const char *s, uint32_t c) {
    int px = UX(cx), py = UY(y);
    if (g_f16 == 12) fcn12_center(px, py, s, c);
    else             fcn16_center(px, py, s, c);
}
static void T22C(int cx, int y, const char *s, uint32_t c) {
    int px = UX(cx), py = UY(y);
    if      (g_f22 == 12) fcn12_center(px, py, s, c);
    else if (g_f22 == 16) fcn16_center(px, py, s, c);
    else                  fcn22_center(px, py, s, c);
}
static void T16R(int rx, int y, const char *s, uint32_t c) {
    int px = UX(rx), py = UY(y);
    if (g_f16 == 12) { px -= fcn12_w(s); fcn12_text(px, py, s, c); }
    else             { px -= fcn16_w(s); fcn16_text(px, py, s, c); }
}
static void T22R(int rx, int y, const char *s, uint32_t c) {
    int px = UX(rx), py = UY(y);
    if      (g_f22 == 12) { px -= fcn12_w(s); fcn12_text(px, py, s, c); }
    else if (g_f22 == 16) { px -= fcn16_w(s); fcn16_text(px, py, s, c); }
    else                  { px -= fcn22_w(s); fcn22_text(px, py, s, c); }
}
static void T160(int x, int y, const char *s, uint32_t c) {
    int px = UX(x), py = UY(y);
    if      (g_f160 == 104) fcn104_text(px, py, s, c);
    else if (g_f160 == 128) fcn128_text(px, py, s, c);
    else                    fcn160_text(px, py, s, c);
}
// 方块中心放一个汉字 (跟随 22 档)
static void T22B(int cx, int cy, const char *s, uint32_t c) {
    int px = UX(cx), py = UY(cy) - g_f22 / 2;
    if      (g_f22 == 12) fcn12_center(px, py, s, c);
    else if (g_f22 == 16) fcn16_center(px, py, s, c);
    else                  fcn22_center(px, py, s, c);
}
// 中号数字 (负一屏气温), 直接用 104 数字档
static void T104(int x, int y, const char *s, uint32_t c) {
    fcn104_text(UX(x), UY(y), s, c);
}

// ---------------- 开关 (椭圆轨道 + 圆钮) ----------------
static void ui_switch(int x, int y, int on) {
    ui_rrect(x, y, 96, 54, 27, on ? UC_BLUE : 0xC9D2DFu);
    if (on) ui_disc(x + 69, y + 27, 21, UC_WHITE);
    else    ui_disc(x + 27, y + 27, 21, UC_WHITE);
}

// ---------------- 状态栏 ----------------
static void ui_statusbar(int dark) {
    uint32_t tc = dark ? UC_WHITE : UC_BLACK;
    T22(70, 18, ui_time_hhmm(), tc);
    T16R(1895, 22, "中国移动 5G", tc);
    // 信号格 (x 1915..1975)
    uint32_t mc = fb_map(tc);
    int sbx = UX(1915);
    int sstep = UX(1926) - UX(1915);
    if (sstep < 2) sstep = 2;
    int hmin = UY(44) - UY(34), hmax = UY(44) - UY(14), base = UY(44);
    for (int i = 0; i < 4; i++) {
        int h = hmin + (hmax - hmin) * i / 3;
        fb_fill(sbx + i * sstep, base - h, sbx + i * sstep + (sstep - 2), base, mc);
    }
    // 电池 (1995..2075): 外框 + 凸头 + 内部电量 (fb_frame 参数为 x,y,宽,高)
    fb_frame(UX(1995), UY(28), UX(2062) - UX(1995), UY(44) - UY(28), mc);
    fb_fill(UX(2066), UY(33), UX(2072), UY(39), mc);
    fb_fill(UX(2000), UY(32), UX(2052), UY(40), mc);
}

// ---------------- 应用图标 ----------------
// 18 个: 彩色圆角方块 + 白色汉字 (简洁中文风), 下方标签
static const uint32_t ui_icon_col[18] = {
    0xFF7A45u,0x27364Au,0xFF5A5Fu,0x8A97A8u,0x1B2B45u,0xFF4D6Du,
    0xFFC236u,0x4D87F5u,0x0D48E7u,0x667280u,0x4D56E7u,0xFF6245u,
    0xE53C36u,0x57B55Fu,0xF5A805u,0x3EC17Bu,0x5F6666u,0xC35DD0u };
static const char *ui_icon_name[18] = {
    "图库","时钟","日历","设置","相机","音乐",
    "备忘录","天气","浏览器","计算器","邮件","视频",
    "应用市场","健康","钱包","地图","文件管理","主题" };
static const char *ui_icon_g[18] = {
    "图","钟","历","置","相","音","录","气","览","算","件","视","市","康","包","地","档","题" };

static void ui_icon_at(int i, int cx, int cy) {
    ui_rrect(cx - 65, cy - 65, 130, 130, 30, ui_icon_col[i]);
    T22B(cx, cy, ui_icon_g[i], UC_WHITE);
    T16C(cx, cy + 78, ui_icon_name[i], UC_TEXT);
}

// ---------------- 四 tab dock (主页/发现/消息/个人中心) ----------------
static void ui_draw_dock(int page) {
    ui_rrect(500, 1230, 1160, 120, 60, UC_WHITE);
    static const char *lb[4] = {"主页","发现","消息","个人中心"};
    int act = (page == 4) ? 1 : (page == 7) ? 2 : (page == 3 || page == 6) ? 3 : 0;
    for (int i = 0; i < 4; i++) {
        int cx = 645 + i * 290;
        uint32_t ic = (i == act) ? UC_WHITE : 0x8A97A8u;
        uint32_t tx = (i == act) ? UC_WHITE : 0x556275u;
        if (i == act) ui_rrect(cx - 125, 1245, 250, 90, 45, UC_BLUE);
        if (i == 0) {                       // 房子
            ui_line(cx - 14, 1286, cx, 1266, ic);
            ui_line(cx, 1266, cx + 14, 1286, ic);
            int bx0 = UX(cx - 9), bx1 = UX(cx + 9);
            fb_fill(bx0, UY(1286), bx1, UY(1299), fb_map(ic));
        } else if (i == 1) {                // 指南针
            uint32_t bg2 = (i == act) ? UC_BLUE : UC_WHITE;
            ui_disc(cx, 1274, 15, ic);
            ui_disc(cx, 1274, 11, bg2);
            ui_line(cx - 5, 1279, cx + 5, 1269, ic);
        } else if (i == 2) {                // 气泡
            ui_rrect(cx - 14, 1262, 28, 20, 9, ic);
            ui_disc(cx - 7, 1286, 4, ic);
        } else {                            // 人
            ui_disc(cx, 1269, 8, ic);
            ui_rrect(cx - 13, 1281, 26, 13, 6, ic);
        }
        T22C(cx, 1298, lb[i], tx);
    }
}
static int ui_dock_hit(int mx, int my) {    // 命中返回目标页, 否则 -1
    if (my < 1230 || my > 1350 || mx < 500 || mx > 1660) return -1;
    static const int tgt[4] = {1,4,7,3};
    return tgt[(mx - 500) / 290];
}
// ---------------- 底部页面圆点 ----------------
static void ui_draw_dots(int page) {
    for (int i = 0; i < 5; i++) {
        int on = (DOT_TGT[i] == page);
        ui_disc(1010 + i * 36, 1392, on ? 9 : 7, on ? UC_BLUE : 0xC2CCDBu);
    }
}
static int ui_dot_hit(int mx, int my) {
    if (my < 1368 || my > 1400) return -1;
    for (int i = 0; i < 5; i++) {
        int cx = 1010 + i * 36;
        if (mx >= cx - 18 && mx <= cx + 18) return DOT_TGT[i];
    }
    return -1;
}

// ---------------- 锁屏 ----------------
static void ui_render_lock(void) {
    fb_fill(0, 0, g_fb_w - 1, g_fb_h - 1, fb_map(UC_LK1));  // letterbox 边带同色
    ui_vgrad(0, 0, UI_LW, UI_LH, UC_LK0, UC_LK1);
    // 底部加深
    ui_vgrad(0, 950, UI_LW, 450, 0x12244Cu, UC_LK2);
    ui_statusbar(1);
    // 大时钟
    T160(70, 470, ui_time_hhmm(), UC_WHITE);
    T22(78, 740, "星期一 · 10 月 14 日", 0xC9D8F2u);
    // 天气行
    ui_disc(96, 812, 22, 0xFFD166u);
    T22(132, 792, "22°  多云 · 空气质量优 · 适合户外运动", 0xC9D8F2u);
    // 指纹解锁提示
    ui_rrect(78, 872, 56, 56, 28, 0x33446Cu);
    T22B(106, 900, "锁", 0xD7E3F7u);
    T22(156, 884, (usb_fp_present || hw_fp_hint) ? "轻触指纹或面容解锁" : "上滑或点击解锁", 0xD7E3F7u);
    // 右侧 2x2 卡片
    int cx0 = 1180, cy0 = 380, cw = 430, ch = 265, gap = 30;
    // 卡 1 天气
    ui_rrect(cx0, cy0, cw, ch, 28, UC_LKCARD);
    T22(cx0 + 36, cy0 + 34, "北京 · 星期三", 0xB9C8E6u);
    T160(cx0 + 32, cy0 + 78, "22", UC_WHITE);
    T22(cx0 + 265, cy0 + 150, "°多云", UC_WHITE);
    T16(cx0 + 36, cy0 + 214, "最高 24°   最低 15°", 0x9FB2D8u);
    // 卡 2 播放
    int cx2 = cx0 + cw + gap;
    ui_rrect(cx2, cy0, cw, ch, 28, UC_LKCARD);
    T22(cx2 + 36, cy0 + 34, "正在播放", 0xB9C8E6u);
    T22(cx2 + 36, cy0 + 84, "星河入梦", UC_WHITE);
    T16(cx2 + 36, cy0 + 128, "陈晓 · 单曲循环", 0x9FB2D8u);
    ui_bar(cx2 + 36, cy0 + 180, cw - 72, 10, 42, 0x3C4E78u, UC_WHITE);
    T16(cx2 + 36, cy0 + 200, "03:12", 0x9FB2D8u);
    T16R(cx2 + cw - 36, cy0 + 200, "07:26", 0x9FB2D8u);
    // 卡 3 日程
    int cy3 = cy0 + ch + gap;
    ui_rrect(cx0, cy3, cw, ch, 28, UC_LKCARD);
    T22(cx0 + 36, cy3 + 34, "今日日程", 0xB9C8E6u);
    ui_rrect(cx0 + 36, cy3 + 92, 8, 120, 4, 0x5B8CFFu);
    T22(cx0 + 62, cy3 + 92, "10:30  产品评审会", UC_WHITE);
    T16(cx0 + 62, cy3 + 132, "会议室 A301 · 准备评审材料", 0x9FB2D8u);
    T22(cx0 + 62, cy3 + 168, "14:00  设计走查 · 线上", UC_WHITE);
    // 卡 4 健康
    ui_rrect(cx2, cy3, cw, ch, 28, UC_LKCARD);
    T22(cx2 + 36, cy3 + 34, "运动健康", 0xB9C8E6u);
    T22(cx2 + 36, cy3 + 86, "8,246 步", UC_WHITE);
    T16(cx2 + 36, cy3 + 132, "目标完成 82%", 0x9FB2D8u);
    ui_bar(cx2 + 36, cy3 + 182, cw - 72, 12, 82, 0x3C4E78u, 0x43D68Au);
    T16(cx2 + 36, cy3 + 206, "今日还需 1,754 步", 0x9FB2D8u);
    // 底部上滑提示
    ui_line(1050, 1340, 1080, 1308, 0x9FB2D8u);
    ui_line(1110, 1340, 1080, 1308, 0x9FB2D8u);
    T16C(1080, 1352, "上滑或点击任意处进入桌面", 0x9FB2D8u);
}

// ---------------- 桌面 ----------------
static void ui_render_home(void) {
    fb_fill(0, 0, g_fb_w - 1, g_fb_h - 1, fb_map(UC_BG));
    ui_statusbar(0);
    // 搜索栏
    ui_rrect(60, 92, 2040, 72, 36, UC_WHITE);
    ui_disc(128, 128, 16, 0x9AA7BBu);
    ui_line(140, 140, 158, 158, 0x9AA7BBu);
    T22(176, 106, "搜索应用、万能卡片与设置", 0x9AA7BBu);
    // 天气大卡
    ui_rrect(60, 200, 620, 440, 28, UC_BLUE);
    T22(100, 244, "北京 · 星期三", 0xD8E4FDu);
    T160(96, 300, "22", UC_WHITE);
    T22(292, 380, "° 多云转晴", UC_WHITE);
    T16(100, 520, "空气优 · 体感 21° · 东北风 2 级", 0xD8E4FDu);
    ui_rrect(100, 566, 150, 44, 22, 0x5E8DF0u);
    T16C(175, 576, "湿度 45%", UC_WHITE);
    ui_rrect(266, 566, 150, 44, 22, 0x5E8DF0u);
    T16C(341, 576, "紫外线弱", UC_WHITE);
    // 18 图标网格
    int gx0 = 755, gy0 = 250, ddx = 215, ddy = 260;
    for (int i = 0; i < 18; i++) {
        int col = i % 6, row = i / 6;
        ui_icon_at(i, gx0 + 65 + col * ddx, gy0 + 65 + row * ddy);
    }
    // 音乐卡
    ui_rrect(60, 680, 620, 190, 24, UC_WHITE);
    ui_rrect(96, 714, 122, 122, 20, 0x3A2B5Au);
    T22B(157, 775, "音", 0xE8D9FFu);
    T22(252, 724, "星河入梦", UC_TEXT);
    T16(252, 766, "陈晓 · 正在播放 03:12 / 07:26", UC_SUB);
    ui_bar(252, 812, 380, 8, 42, 0xE4E9F1u, UC_BLUE);
    // 效率工具卡
    ui_rrect(60, 900, 620, 230, 24, UC_WHITE);
    T22(100, 934, "效率工具", UC_TEXT);
    static const char *tn[4] = {"扫","译","印","录"};
    static const char *tl[4] = {"扫一扫","翻译","打印","录音"};
    for (int i = 0; i < 4; i++) {
        int cx = 150 + i * 140;
        ui_rrect(cx - 44, 990, 88, 88, 24, 0xEEF3FCu);
        T22B(cx, 1034, tn[i], UC_BLUE);
        T16C(cx, 1086, tl[i], 0x8996A8u);
    }
    // dock + 页面圆点
    ui_draw_dock(1);
    ui_draw_dots(1);
}

// ---------------- 控制中心 ----------------
static void ui_ctrl_tile(int i, int x, int y) {
    int on = ui_tiles[i];
    static const char *tt[8] = {"WLAN","蓝牙","移动数据","个人热点","飞行模式","免打扰","深色模式","手电筒"};
    static const char *ts[8] = {"IN_OS_Net","已连接","已关闭","可被发现","已关闭","1 小时后","跟随日落","点击开启"};
    static const char *tg[8] = {"网","牙","数","点","飞","扰","夜","电"};
    ui_rrect(x, y, 310, 150, 22, on ? UC_BLUE : UC_WHITE);
    uint32_t fg = on ? UC_WHITE : UC_TEXT;
    T22(x + 26, y + 24, tg[i], fg);
    T22(x + 88, y + 26, tt[i], fg);
    T16(x + 30, y + 96, ts[i], on ? 0xD3E1FCu : UC_SUB);
}
static void ui_render_ctrl(void) {
    fb_fill(0, 0, g_fb_w - 1, g_fb_h - 1, fb_map(UC_BG));
    ui_statusbar(0);
    T22(60, 96, "控制中心", UC_TEXT);
    T16(60, 138, "网络 · 媒体 · 快捷开关", UC_SUB);
    // 播放卡
    ui_rrect(60, 200, 650, 300, 26, 0x1B222Du);
    ui_rrect(100, 244, 130, 130, 22, 0x5A3E86u);
    T22B(165, 309, "音", 0xE8D9FFu);
    T22(260, 250, "星河入梦", UC_WHITE);
    T16(260, 292, "陈晓 · 单曲循环", 0x93A0B5u);
    ui_bar(260, 340, 410, 8, 42, 0x3A4456u, UC_WHITE);
    T16(260, 360, "03:12", 0x93A0B5u);
    T16R(670, 360, "07:26", 0x93A0B5u);
    // 播放按钮: 上一首 |◀  播放(白圆深三角)  ▶| 下一首
    fb_fill(UX(296), UY(420), UX(304), UY(460), fb_map(0xC9D3E2u));
    ui_tri(326, 440, 16, 1, 0xC9D3E2u);
    ui_disc(400, 440, 34, UC_WHITE);
    ui_tri(406, 440, 18, 0, 0x1B222Du);
    ui_tri(474, 440, 16, 0, 0xC9D3E2u);
    fb_fill(UX(504), UY(420), UX(512), UY(460), fb_map(0xC9D3E2u));
    // 开关网格
    int tx0 = 750, ty0 = 200, tw = 310, th = 150, tg = 20;
    for (int i = 0; i < 8; i++) {
        int col = i % 4, row = i / 4;
        ui_ctrl_tile(i, tx0 + col * (tw + tg), ty0 + row * (th + 30));
    }
    // 亮度音量卡
    ui_rrect(60, 540, 650, 280, 26, UC_WHITE);
    T22(100, 580, "显示", UC_TEXT);
    ui_bar(100, 640, 560, 16, 70, 0xE4E9F1u, UC_BLUE);
    ui_disc(100 + 560 * 70 / 100, 648, 18, UC_WHITE);
    T22(100, 700, "音量", UC_TEXT);
    ui_bar(100, 760, 560, 16, 45, 0xE4E9F1u, UC_WHITE);
    ui_disc(100 + 560 * 45 / 100, 768, 18, UC_WHITE);
    // 超级终端
    ui_rrect(60, 860, 650, 300, 26, UC_BLUE);
    T22(100, 900, "超级终端", UC_WHITE);
    T16(100, 942, "拖一拉  协同连接周边设备", 0xD3E1FCu);
    ui_disc(240, 1050, 46, 0xFFFFFFu);
    T22B(240, 1050, "本", UC_BLUE);
    for (int i = 0; i < 3; i++) {
        static const char *g2[3] = {"屏","耳","表"};
        int ex = 420 + i * 95;
        ui_line(280, 1050, ex - 30, 1050, 0x9FBEF7u);
        ui_disc(ex, 1050, 34, 0x5E8DF0u);
        T22B(ex, 1050, g2[i], UC_WHITE);
    }
    // 已连接设备
    ui_rrect(750, 540, 1350, 620, 26, UC_WHITE);
    T22(790, 580, "已连接设备", UC_TEXT);
    static const char *dnm[4] = {"HUAWEI Watch 3","FreeBuds Pro","MateView 显示器","MateBook 协同"};
    static const char *dst2[4] = {"电量 78%","电量 64% · 降噪中","已投屏","已连接 · 可跨设备编辑"};
    static const char *dg[4] = {"表","耳","屏","本"};
    for (int i = 0; i < 4; i++) {
        int yy = 650 + i * 125;
        ui_disc(836, yy + 30, 30, 0xEEF3FCu);
        T22B(836, yy + 30, dg[i], UC_BLUE);
        T22(892, yy + 6, dnm[i], UC_TEXT);
        T16(892, yy + 50, dst2[i], UC_SUB);
        if (i < 3) fb_fill(UX(790), UY(yy + 100), UX(2060), UY(yy + 101), fb_map(UC_CARDLINE));
    }
    // dock + 圆点
    ui_draw_dock(2);
    ui_draw_dots(2);
}

// ---------------- 设置 ----------------
static void ui_render_set(void) {
    fb_fill(0, 0, g_fb_w - 1, g_fb_h - 1, fb_map(0xF1F4F8u));
    fb_fill(0, 0, UX(620), g_fb_h - 1, fb_map(UC_WHITE));
    // 用户区
    ui_disc(110, 120, 38, 0x9FB7E8u);
    T22B(110, 120, "李", UC_WHITE);
    T22(170, 92, "李明", UC_TEXT);
    T16(170, 132, "华为账号 · 已登录", UC_SUB);
    // 搜索
    ui_rrect(60, 200, 500, 60, 30, 0xF1F4F8u);
    T16(110, 218, "搜索设置项", 0x9AA7BBu);
    // 菜单 (13 行: 末三行 = 多任务与分屏 / 硬件检测 / 任务管理器)
    static const char *mi[13] = {"网络与连接","显示与亮度","声音与振动","通知与状态栏","应用和服务",
                                 "隐私与安全","生物识别与密码","存储","辅助功能","系统和更新",
                                 "多任务与分屏","硬件检测","任务管理器"};
    static const char *mg[13] = {"网","显","声","知","用","隐","锁","存","辅","统","任","检","调"};
    for (int i = 0; i < 13; i++) {
        int yy = 292 + i * 76;
        if (i == 1) ui_rrect(40, yy - 8, 540, 72, 20, UC_SEL);
        ui_rrect(76, yy + 8, 40, 40, 12, i == 1 ? UC_BLUE : 0xEEF3FCu);
        T22B(96, yy + 28, mg[i], i == 1 ? UC_WHITE : UC_BLUE);
        T22(136, yy + 6, mi[i], i == 1 ? UC_BLUE : UC_TEXT);
        T22(540, yy + 2, "›", 0xB6C0CFu);
    }
    // 右区
    T22(700, 96, "显示与亮度", UC_TEXT);
    T16(700, 138, "个性化屏幕显示效果", UC_SUB);
    ui_rrect(700, 200, 1400, 570, 24, UC_WHITE);
    static const char *ri[6] = {"深色模式","自动亮度调节","护眼模式","智能旋转屏幕","屏幕刷新率","字体大小与粗细"};
    static const char *rs[6] = {"晚上自动切换深色","根据环境光自动调节","全天开启  22:00 - 07:00","根据握持方向旋转","智能切换 60/120 Hz","默认"};
    static int ron[6] = {0,1,0,1,0,0};
    for (int i = 0; i < 6; i++) {
        int yy = 236 + i * 92;
        T22(744, yy, ri[i], UC_TEXT);
        T16(744, yy + 34, rs[i], UC_SUB);
        ui_switch(1980, yy - 2, ron[i]);
        if (i < 5) fb_fill(UX(744), UY(yy + 78), UX(2056), UY(yy + 79), fb_map(UC_CARDLINE));
    }
    // 关于本机
    ui_rrect(700, 800, 1400, 360, 24, UC_WHITE);
    T22(744, 840, "关于本机", UC_TEXT);
    T16(744, 900, "设备名称  MateBook E · IN OS", 0x556275u);
    T16(744, 940, "处理器    Intel Core m3-7Y30", 0x556275u);
    T16(744, 980, "显卡      Intel HD Graphics 615", 0x556275u);
    T16(744, 1020, "屏幕      2160 x 1440 (3:2) · Wacom 触控", 0x556275u);
    T16(744, 1060, "系统版本  IN OS 0.1  (裸机 xHCI 图形版)", 0x556275u);
    {   // 可用外设: 按 hwprobe 真实探测结果拼接
        static char per[112];
        int np = 0;
        per[0] = 0;
        if (hw_kb_n)          { if (np) hw_strcat(per, " · "); hw_strcat(per, "USB 键盘"); np++; }
        if (hw_ms_n)          { if (np) hw_strcat(per, " · "); hw_strcat(per, "USB 鼠标"); np++; }
        if (usb_digi_present) { if (np) hw_strcat(per, " · "); hw_strcat(per, "USB 触摸"); np++; }
        if (i2c_present)      { if (np) hw_strcat(per, " · "); hw_strcat(per, "I2C 触摸"); np++; }
        if (usb_fp_present || hw_fp_hint) { if (np) hw_strcat(per, " · "); hw_strcat(per, "指纹"); np++; }
        if (usb_cam_present || hw_cam_hint) { if (np) hw_strcat(per, " · "); hw_strcat(per, "摄像头"); np++; }
        if (np == 0) hw_strcat(per, "未检测到外设");
        T16(744, 1100, per, np ? UC_BLUE : UC_SUB);
    }
    // dock + 圆点
    ui_draw_dock(3);
    ui_draw_dots(3);
}

// ---------------- 负一屏 · 智能中心 (设计稿 03) ----------------
static void ui_render_hub(void) {
    fb_fill(0, 0, g_fb_w - 1, g_fb_h - 1, fb_map(UC_BG));
    ui_statusbar(0);
    T22(60, 84, "智能中心", UC_TEXT);
    T16(60, 140, "基于时间、地点与使用习惯，为你推荐当下最需要的服务", UC_SUB);
    // 小艺推荐 (右上, 星点 + 蓝字)
    ui_disc(1902, 104, 3, UC_BLUE);
    ui_disc(1912, 96, 5, UC_BLUE);
    ui_disc(1920, 106, 3, UC_BLUE);
    T16R(2100, 96, "小艺推荐", UC_BLUE);
    // 蓝色渐变横幅 (圆角 = 渐变矩形 + 四角同色圆盖)
    ui_vgrad(60, 200, 2040, 200, 0x4E86F0u, 0x2563E7u);
    ui_disc(88, 228, 28, UC_BG); ui_disc(2072, 228, 28, UC_BG);
    ui_disc(88, 372, 28, UC_BG); ui_disc(2072, 372, 28, UC_BG);
    ui_rrect(92, 228, 150, 40, 20, 0x6B9AF3u);
    T16C(167, 238, "今日推荐", UC_WHITE);
    T22(92, 282, "通勤路上，听点轻松的", UC_WHITE);
    T16(92, 332, "为你精选 12 首，小艺根据你的通勤时间生成", 0xD8E4FDu);
    ui_disc(2010, 300, 46, UC_WHITE);
    ui_tri(2022, 300, 20, 0, UC_BLUE);

    // 三列卡片: x = 60 / 752 / 1444, 宽 656
    // 左列 1: 天气 + 日程
    ui_rrect(60, 440, 656, 380, 24, UC_WHITE);
    T22(100, 468, "北京 · 星期三", UC_TEXT);
    ui_disc(122, 566, 24, 0xFFC236u);
    T104(170, 496, "22", UC_TEXT);
    T22(348, 560, "° 多云转晴", UC_TEXT);
    T16(348, 600, "24° / 15°", UC_SUB);
    T22(100, 636, "10:30", UC_BLUE);  T22(196, 636, "产品评审会", UC_TEXT);
    T16(196, 672, "会议室 A3 · 1 小时", UC_SUB);
    T22(100, 704, "14:00", UC_BLUE);  T22(196, 704, "设计走查", UC_TEXT);
    T16(196, 740, "线上会议 · 45 分钟", UC_SUB);
    ui_rrect(100, 768, 576, 44, 22, 0xDCE6FAu);
    T16C(388, 780, "下午 5 点后有雨，记得带伞", UC_BLUE);
    // 左列 2: 通勤模式
    ui_rrect(60, 860, 656, 374, 24, UC_WHITE);
    ui_disc(112, 918, 26, 0xE3ECFCu);
    T22B(112, 918, "导", UC_BLUE);
    T22(160, 894, "通勤模式", UC_TEXT);
    T16(100, 972, "到公司约 32 分钟 · 地铁 2 号线", UC_SUB);
    ui_line(120, 1060, 340, 1060, 0xC9D2DFu);
    ui_disc(120, 1060, 10, UC_BLUE);
    ui_disc(340, 1060, 10, 0xC9D2DFu);
    T16(366, 1046, "2 号线 · 12 站", UC_SUB);
    ui_rrect(100, 1120, 240, 72, 36, UC_BLUE);
    T22C(220, 1145, "开始导航", UC_WHITE);
    ui_rrect(370, 1120, 240, 72, 36, 0xEEF2F8u);
    T22C(490, 1145, "换乘路线", UC_TEXT);
    // 中列 1: 常用服务
    ui_rrect(752, 440, 656, 380, 24, UC_WHITE);
    T22(792, 468, "常用服务", UC_TEXT);
    static const char *qs_g[6] = {"扫","付","乘","递","卡","更"};
    static const char *qs_n[6] = {"扫码","付款","乘车码","快递","卡包","更多"};
    for (int i = 0; i < 6; i++) {
        int col = i % 3, row = i / 3;
        int cx = 861 + col * 219;
        int iy = 536 + row * 152;
        ui_rrect(cx - 44, iy, 88, 88, 24, 0xE3ECFCu);
        T22B(cx, iy + 44, qs_g[i], UC_BLUE);
        T16C(cx, iy + 98, qs_n[i], UC_SUB);
    }
    // 中列 2: 最近使用
    ui_rrect(752, 852, 656, 190, 24, UC_WHITE);
    T22(792, 878, "最近使用", UC_TEXT);
    static const char *ru_g[5] = {"图","录","地","览","件"};
    static const char *ru_n[5] = {"图库","备忘录","地图","浏览器","邮件"};
    static const uint32_t ru_c[5] = {0xFF7A45u,0xFFC236u,0x3EC17Bu,0x0D48E7u,0x4D56E7u};
    for (int i = 0; i < 5; i++) {
        int cx = 818 + i * 131;
        ui_rrect(cx - 36, 922, 72, 72, 20, ru_c[i]);
        T22B(cx, 958, ru_g[i], UC_WHITE);
        T16C(cx, 1002, ru_n[i], UC_SUB);
    }
    // 中列 3: 晚安模式 (深色卡)
    ui_rrect(752, 1054, 656, 180, 24, 0x1F2A3Eu);
    T22(792, 1086, "晚安模式", UC_WHITE);
    T16(792, 1130, "23:00 自动开启免打扰与深色模式", 0x93A0B5u);
    ui_rrect(792, 1170, 160, 44, 22, 0x33415Cu);
    T16C(872, 1182, "立即开启", UC_WHITE);
    ui_disc(1350, 1096, 18, 0xC9D3E2u);
    ui_disc(1357, 1090, 15, 0x1F2A3Eu);
    // 右列 1: 办公场景
    ui_rrect(1444, 440, 656, 280, 24, UC_WHITE);
    T22(1484, 468, "办公场景", UC_TEXT);
    ui_rrect(2000, 466, 52, 42, 10, UC_BLUE);
    fb_fill(UX(2016), UY(458), UX(2036), UY(470), fb_map(UC_BLUE));
    T16(1484, 512, "会议中自动静音，投屏一键连接", UC_SUB);
    ui_disc(1516, 586, 18, 0xE3ECFCu);
    T22B(1516, 586, "投", UC_BLUE);
    T16(1552, 572, "10:30 投屏到会议室 A3", UC_TEXT);
    ui_disc(1516, 650, 18, 0xE3ECFCu);
    T22B(1516, 650, "铃", UC_BLUE);
    T16(1552, 636, "12:00 午餐时段自动免打扰", UC_TEXT);
    // 右列 2: 出行助手
    ui_rrect(1444, 760, 656, 240, 24, UC_WHITE);
    T22(1484, 788, "出行助手", UC_TEXT);
    ui_disc(2020, 796, 18, 0xE3ECFCu);
    T22B(2020, 796, "出", UC_BLUE);
    T22(1484, 840, "2 号线", UC_TEXT);
    T16(1604, 848, "预计 6 分钟到站", UC_BLUE);
    ui_rrect(1484, 902, 576, 10, 5, 0xE4E9F1u);
    ui_rrect(1484, 902, 230, 10, 5, 0x43D68Au);
    T16(1484, 934, "末班车 23:26 · 当前客流较少", UC_SUB);
    // 右列 3: 健康提醒
    ui_rrect(1444, 1040, 656, 194, 24, UC_WHITE);
    T22(1484, 1068, "健康提醒", UC_TEXT);
    ui_disc(2020, 1078, 18, 0x43D68Au);
    T22B(2020, 1078, "心", UC_WHITE);
    T22(1484, 1120, "8,246 步 · 目标 82%", UC_TEXT);
    T16(1484, 1166, "已连续 3 天达成 8000 步", UC_SUB);
    // dock + 圆点 (发现 tab 活动)
    ui_draw_dock(4);
    ui_draw_dots(4);
}

// ---------------- 多任务 (设计稿 04) ----------------
static const char *mt_nm[4] = {"图库","浏览器","视频","备忘录"};
static const char *mt_sb[4] = {"刚刚使用","8 分钟前","缓存播放中","昨天 22:14"};
static const char *mt_gg[4] = {"图","览","视","录"};
static const uint32_t mt_cl[4] = {0xFF7A45u, 0x2563E7u, 0xFF5A5Fu, 0x27364Au};
static int mt_count(void) {
    int n = 0;
    for (int i = 0; i < 4; i++) n += mt_open[i];
    return n;
}
// 矢量缩略图: 0 山湖图库 1 城市夜景 2 红色剧场 3 木质备忘录
static void mt_thumb(int x, int y, int i) {
    if (i == 0) {                                    // 图库: 山与湖
        ui_vgrad(x, y, 480, 443, 0x9FC6E8u, 0xE2F0FAu);
        ui_disc(x + 360, y + 90, 40, 0xFFFFFFu);
        ui_tri_up(x + 150, y + 320, 190, 200, 0x517797u);
        ui_tri_up(x + 340, y + 330, 170, 170, 0x7FA3C4u);
        ui_vgrad(x, y + 330, 480, 113, 0xBADCEEu, 0x9CC8E4u);
    } else if (i == 1) {                             // 浏览器: 城市夜景
        ui_vgrad(x, y, 480, 443, 0x1C2A4Au, 0x45568Au);
        int bh[5] = {180, 260, 220, 300, 200};
        for (int k = 0; k < 5; k++) {
            int bx = x + 20 + k * 92;
            fb_fill(UX(bx), UY(y + 443 - bh[k]), UX(bx + 72), UY(y + 443), fb_map(0x24344Fu));
            for (int wy = y + 443 - bh[k] + 24; wy < y + 420; wy += 44)
                for (int wx = bx + 12; wx < bx + 60; wx += 24)
                    fb_fill(UX(wx), UY(wy), UX(wx + 8), UY(wy + 12), fb_map(0xF5C542u));
        }
    } else if (i == 2) {                             // 视频: 剧场
        ui_vgrad(x, y, 480, 443, 0x43101Cu, 0x8A2338u);
        for (int k = 0; k < 3; k++)
            fb_fill(UX(x + 40 + k * 40), UY(y + 40), UX(x + 64 + k * 40), UY(y + 380),
                    fb_map(0xA8324Au));
        ui_rrect(x + 200, y + 90, 240, 150, 10, 0xF2E9ECu);
        fb_fill(UX(x + 60), UY(y + 400), UX(x + 420), UY(y + 443), fb_map(0x35080Fu));
    } else {                                         // 备忘录: 木桌笔记本
        ui_vgrad(x, y, 480, 443, 0xA8743Eu, 0xC98A4Au);
        ui_rrect(x + 90, y + 40, 300, 363, 14, 0xF7F5F0u);
        for (int k = 0; k < 7; k++)
            fb_fill(UX(x + 120), UY(y + 90 + k * 40), UX(x + 360), UY(y + 96 + k * 40),
                    fb_map(0xD8D4CCu));
        ui_line(x + 60, y + 380, x + 200, y + 300, 0x3A2B20u);
    }
}
static void ui_render_multi(void) {
    fb_fill(0, 0, g_fb_w - 1, g_fb_h - 1, fb_map(UC_BG));
    ui_statusbar(0);
    T22(60, 84, "多任务", UC_TEXT);
    char cnt[2];
    cnt[0] = (char)('0' + mt_count());
    cnt[1] = 0;
    int ex = fcn16_text(UX(60), UY(140), cnt, UC_SUB);
    fcn16_text(ex, UY(140), " 个应用在后台运行 · 长按卡片可关闭", UC_SUB);
    // 右上模式 chips: 全屏 / 分屏 / 悬浮窗 / 全部关闭
    ui_rrect(1380, 88, 150, 56, 28, mt_mode == 0 ? UC_BLUE : UC_WHITE);
    T16C(1455, 106, "全屏", mt_mode == 0 ? UC_WHITE : UC_TEXT);
    ui_rrect(1550, 88, 150, 56, 28, mt_mode == 1 ? UC_BLUE : UC_WHITE);
    T16C(1625, 106, "分屏", mt_mode == 1 ? UC_WHITE : UC_TEXT);
    ui_rrect(1720, 88, 170, 56, 28, mt_mode == 2 ? UC_BLUE : UC_WHITE);
    T16C(1805, 106, "悬浮窗", mt_mode == 2 ? UC_WHITE : UC_TEXT);
    ui_rrect(1910, 88, 190, 56, 28, UC_WHITE);
    T16C(2005, 106, "全部关闭", 0xE5484Du);
    // 应用卡片
    if (mt_count() == 0) {
        T22C(1080, 480, "没有后台应用", UC_SUB);
        T16C(1080, 540, "全部已关闭，点击下方圆点返回桌面", UC_SUB);
    } else {
        int k = 0;
        for (int i = 0; i < 4; i++) {
            if (!mt_open[i]) continue;
            int x = 60 + k * 520;
            k++;
            mt_thumb(x, 211, i);
            ui_rrect(x, 650, 480, 173, 20, UC_WHITE);
            ui_rrect(x + 30, 684, 56, 56, 16, mt_cl[i]);
            T22B(x + 58, 712, mt_gg[i], UC_WHITE);
            T22(x + 106, 692, mt_nm[i], UC_TEXT);
            T16(x + 106, 734, mt_sb[i], UC_SUB);
            T22(x + 424, 700, "✕", UC_SUB);
        }
    }
    // 分屏组合卡 + 蓝色提示卡
    ui_rrect(60, 860, 1434, 364, 24, UC_WHITE);
    T22(100, 900, "分屏组合", UC_TEXT);
    T16(100, 944, "拖动应用卡片到左右区域即可分屏", UC_SUB);
    static const char *sp_nm[2] = {"图库", "浏览器"};
    static const char *sp_g[2]  = {"图", "览"};
    static const uint32_t sp_c[2] = {0xFF7A45u, 0x2563E7u};
    for (int p = 0; p < 2; p++) {
        int px = 370 + p * 564;
        ui_rrect(px, 916, 530, 270, 20, 0xF2F5FAu);
        ui_rrect(px + 30, 944, 44, 44, 12, sp_c[p]);
        T22B(px + 52, 966, sp_g[p], UC_WHITE);
        T22(px + 90, 952, sp_nm[p], UC_TEXT);
        for (int l = 0; l < 3; l++)
            ui_rrect(px + 30, 1030 + l * 46, 470, 14, 7, 0xE4E9F1u);
    }
    fb_fill(UX(898), UY(946), UX(900), UY(1156), fb_map(UC_CARDLINE));
    ui_vgrad(1546, 860, 554, 364, 0x4E86F0u, 0x2563E7u);
    ui_disc(1574, 888, 28, UC_BG); ui_disc(2072, 888, 28, UC_BG);
    ui_disc(1574, 1196, 28, UC_BG); ui_disc(2072, 1196, 28, UC_BG);
    ui_disc(1823, 960, 26, 0x6B9AF3u);
    ui_line(1797, 960, 1849, 960, UC_WHITE);
    ui_line(1823, 934, 1823, 986, UC_WHITE);
    ui_tri_up(1823, 934, 8, 12, UC_WHITE);
    T22C(1823, 1030, "长按应用卡片", UC_WHITE);
    T16C(1823, 1078, "拖到屏幕左右两侧，即可创建分屏组合", 0xD8E4FDu);
    // dock + 圆点 (主页 tab 活动)
    ui_draw_dock(5);
    ui_draw_dots(5);
}

// ---------------- 消息 ----------------
static void ui_render_msg(void) {
    fb_fill(0, 0, g_fb_w - 1, g_fb_h - 1, fb_map(UC_BG));
    ui_statusbar(0);
    T22(60, 84, "消息", UC_TEXT);
    T16(60, 140, "2 条未读会话", UC_SUB);
    static const char *ms_nm[4] = {"设计走查群","小艺","李娜","IN OS 订阅号"};
    static const char *ms_pv[4] = {"张工: 方案我更新到共享盘了","今日通勤提醒与歌单已为你准备好",
                                   "明天上午十点会议室见","本周开发进展: 触摸驱动已就绪"};
    static const char *ms_tm[4] = {"10:24","09:41","昨天","昨天"};
    static const uint32_t ms_cl[4] = {0x4D87F5u, 0x2563E7u, 0x43D68Au, 0x8A97A8u};
    static const char *ms_g[4] = {"设","艺","李","订"};
    for (int i = 0; i < 4; i++) {
        int cy = 260 + i * 230;
        ui_rrect(60, cy, 2040, 190, 24, UC_WHITE);
        ui_disc(160, cy + 95, 44, ms_cl[i]);
        T22B(160, cy + 95, ms_g[i], UC_WHITE);
        T22(240, cy + 40, ms_nm[i], UC_TEXT);
        T16(240, cy + 96, ms_pv[i], UC_SUB);
        T16R(2040, cy + 40, ms_tm[i], UC_SUB);
        if (i < 2) ui_disc(2010, cy + 132, 10, 0xE5484Du);
    }
    // dock + 圆点 (消息 tab 活动)
    ui_draw_dock(7);
    ui_draw_dots(7);
}

// ---------------- 硬件检测 (hwprobe.h 真实探测数据, 概览 + PCI 分页) ----------------
static int hw_hwpage = 0;                       // 0 = 概览, 1.. = PCI 设备表
static int hw_pages_max = 1;
#define HW_ROWS_PG 12

static void ui_hw_row(int y, const char *k, const char *v, uint32_t cv) {
    T16(100, y, k, UC_TEXT);
    T16R(2040, y, v, cv);
}

static void ui_render_hw(void) {
    fb_fill(0, 0, g_fb_w - 1, g_fb_h - 1, fb_map(0xF1F4F8u));
    ui_statusbar(0);
    T22(60, 84, "硬件检测", UC_TEXT);
    hw_pages_max = 1 + (hw_pci_n + HW_ROWS_PG - 1) / HW_ROWS_PG;

    char t[12], b[56];
    if (hw_hwpage == 0) {
        // ---- 概览页 ----
        T16(60, 140, "系统 · 外设 · ACPI 状态 (启动时探测)", UC_SUB);
        ui_rrect(60, 200, 2040, 560, 24, UC_WHITE);
        T22(100, 236, "系统概览", UC_TEXT);
        int y = 300;
        // 屏幕
        hw_itoa(t, hw_gop_w); b[0] = 0;
        hw_strcat(b, t); hw_strcat(b, " x ");
        hw_itoa(t, hw_gop_h); hw_strcat(b, t);
        ui_hw_row(y, "屏幕 GOP", b, UC_BLUE); y += 64;
        // 触摸屏
        const char *tp;
        int tp_ok = (usb_digi_present || i2c_present || hw_touch_hint);
        if (usb_digi_present)    tp = "USB 数字化笔 在线";
        else if (i2c_present)    tp = "I2C-HID 触摸 在线";
        else if (hw_touch_hint)  tp = "ACPI 枚举到 I2C 触摸 (WCOM)";
        else if (i2c_ctrl_found) tp = "LPSS I2C 在线, 无 HID 设备";
        else                     tp = "未检测到";
        ui_hw_row(y, "触摸屏", tp, tp_ok ? UC_BLUE : UC_SUB); y += 64;
        // 指纹
        tp = usb_fp_present ? "USB 指纹器 在线"
             : hw_fp_hint   ? "FPC1021 (I2C/WBDI, 仅检测)" : "未检测到";
        ui_hw_row(y, "指纹", tp, (usb_fp_present || hw_fp_hint) ? UC_BLUE : UC_SUB); y += 64;
        // 摄像头
        tp = usb_cam_present ? "UVC 摄像头 在线"
             : hw_cam_hint   ? "MIPI/IPU3 摄像头 (GC5024, 仅检测)"
             : "未检测到 (MIPI 需 ISP 固件)";
        ui_hw_row(y, "摄像头", tp, (usb_cam_present || hw_cam_hint) ? UC_BLUE : UC_SUB); y += 64;
        // USB 汇总
        b[0] = 0;
        hw_itoa(t, hw_kb_n); hw_strcat(b, "键盘 x"); hw_strcat(b, t);
        hw_strcat(b, " · ");
        hw_itoa(t, hw_ms_n); hw_strcat(b, "鼠标 x"); hw_strcat(b, t);
        ui_hw_row(y, "USB 设备", b, UC_SUB);
    } else {
        // ---- PCI 设备表 (每页 12 行) ----
        T16(60, 140, "PCI 总线设备表", UC_SUB);
        ui_rrect(60, 200, 2040, 900, 24, UC_WHITE);
        T22(100, 236, "PCI 设备", UC_TEXT);
        int base = (hw_hwpage - 1) * HW_ROWS_PG;
        int n = hw_pci_n - base;
        if (n > HW_ROWS_PG) n = HW_ROWS_PG;
        for (int i = 0; i < n; i++) {
            int idx = base + i;
            unsigned loc = hw_pci_loc[idx];
            char h2[8];
            char line[64];
            line[0] = 0;
            hw_hex2(h2, (loc >> 8) & 0xFFu);  hw_strcat(line, h2); hw_strcat(line, ":");
            hw_hex2(h2, (loc >> 3) & 0x1Fu);  hw_strcat(line, h2); hw_strcat(line, ".");
            hw_hex2(h2, loc & 7u);            hw_strcat(line, h2);
            hw_strcat(line, "  ");
            hw_hex4(h2, (hw_pci_id[idx] >> 16) & 0xFFFFu); hw_strcat(line, h2);
            hw_strcat(line, ":");
            hw_hex4(h2, hw_pci_id[idx] & 0xFFFFu);         hw_strcat(line, h2);
            hw_strcat(line, "  c");
            hw_hex6(h2, hw_pci_cls[idx]);                  hw_strcat(line, h2);
            T16(100, 300 + i * 64, line, UC_TEXT);
            T16R(2040, 300 + i * 64, hw_cls_name(hw_pci_cls[idx]), UC_SUB);
        }
        if (n == 0) T16(100, 300, "PCI 总线无设备", UC_SUB);
    }

    // ---- 底部翻页条 ----
    if (hw_hwpage > 0) {
        ui_rrect(60, 1140, 300, 90, 20, UC_WHITE);
        T22(120, 1165, "上一页", UC_TEXT);
    } else {
        b[0] = 0;
        hw_itoa(t, hw_pci_n);
        hw_strcat(b, "PCI 设备 x"); hw_strcat(b, t);
        hw_strcat(b, " · ACPI x");
        hw_itoa(t, hw_acpi_n); hw_strcat(b, t);
        T16(80, 1168, b, UC_SUB);
    }
    if (hw_hwpage < hw_pages_max - 1) {
        ui_rrect(1800, 1140, 300, 90, 20, UC_BLUE);
        T22(1860, 1165, "下一页", 0xFFFFFFu);
    }
    b[0] = 0;
    hw_itoa(t, hw_hwpage + 1); hw_strcat(b, t); hw_strcat(b, " / ");
    hw_itoa(t, hw_pages_max);  hw_strcat(b, t);
    T16C(1080, 1168, b, UC_SUB);

    // dock + 圆点 (个人中心 tab 活动)
    ui_draw_dock(6);
    ui_draw_dots(6);
}

// ---------------- 任务管理器 (page 8, 实时调度视图, ui_tick 周期重绘) ----------------
static int tm_page = 0;
static int tm_pages_max = 1;
static unsigned tm_last_sig = 0xFFFFFFFFu;        // 上次渲染时的调度状态签名
#define TM_ROWS_PG 12

// 调度状态签名: 任务池状态/重试/标志 + 统计计数任一变化 => 签名变 => 重绘
static unsigned tm_sig(void) {
    unsigned h = 2166136261u;
    for (int i = 0; i < MAX_TASK; i++) {
        task_t *tp = &task_pool[i];
        h = (h ^ (unsigned)tp->state) * 16777619u;
        h = (h ^ (unsigned)tp->retry_times) * 31u;
        h = (h ^ (unsigned)(tp->is_timeout | (tp->is_stuck << 1))) * 31u;
    }
    h = (h ^ (unsigned)sched_count_active()) * 31u;
    h = (h ^ (unsigned)g_stat.total_create) * 31u;
    h = (h ^ (unsigned)g_stat.total_finish) * 31u;
    h = (h ^ (unsigned)g_stat.total_crash) * 31u;
    h = (h ^ (unsigned)g_stat.total_retry) * 31u;
    h = (h ^ (unsigned)g_stat.total_stuck) * 31u;
    return h ^ (unsigned)tm_page;
}

static const char *tm_state_cn(int s) {
    switch (s) {
        case TASK_WAIT_IR:        return "等待指令";
        case TASK_COMPILE_ARM:    return "ARM编译";
        case TASK_TRANS_X86:      return "X86翻译";
        case TASK_CACHE_STORE:    return "缓存入库";
        case TASK_FORWARD_SCHED:  return "转发调度";
        case TASK_RUNNING:        return "运行中";
        case TASK_CRASH_RECOVER:  return "崩溃恢复";
        case TASK_FINISH:         return "已完成";
        case TASK_STUCK_DETECT:   return "卡死检测";
        case TASK_RETRY_QUEUE:    return "重试排队";
        default:                  return "空闲";
    }
}
static const char *tm_prio_cn(int p) {
    switch (p) {
        case PRIORITY_LOW:       return "低";
        case PRIORITY_NORMAL:    return "普通";
        case PRIORITY_HIGH:      return "高";
        case PRIORITY_REAL_TIME: return "实时";
        default:                 return "空闲";
    }
}
static const char *tm_num(int v) {                // 每帧复用一个静态缓冲逐项取值
    static char nb[8][16];
    static int ni = 0;
    char *d = nb[ni & 7]; ni++;
    hw_itoa(d, v);
    return d;
}

static void ui_render_tm(void) {
    fb_fill(0, 0, g_fb_w - 1, g_fb_h - 1, fb_map(0xF1F4F8u));
    ui_statusbar(0);
    T22(60, 84, "任务管理器", UC_TEXT);
    T16(60, 140, "实时调度视图 · 调度状态变化自动刷新", UC_SUB);
    int act = sched_count_active();
    char t[12], b[64];

    // ---- 统计卡 (两行三列, TCC 不支持函数内数组初始化, 展开绘制) ----
    ui_rrect(60, 200, 2040, 190, 24, UC_WHITE);
    T22B(140, 236, tm_num(act), UC_BLUE);           T16(260, 244, "活动任务", UC_SUB);
    T22B(800, 236, tm_num((int)g_stat.total_create), UC_TEXT);   T16(920, 244, "总创建", UC_SUB);
    T22B(1460, 236, tm_num((int)g_stat.total_finish), UC_TEXT);  T16(1580, 244, "已完成", UC_SUB);
    T22B(140, 324, tm_num((int)g_stat.total_crash), UC_TEXT);    T16(260, 332, "崩溃", UC_SUB);
    T22B(800, 324, tm_num((int)g_stat.total_retry), UC_TEXT);    T16(920, 332, "重试", UC_SUB);
    T22B(1460, 324, tm_num((int)g_stat.total_stuck), UC_TEXT);   T16(1580, 332, "卡死", UC_SUB);

    // ---- 任务表 ----
    tm_pages_max = (act + TM_ROWS_PG - 1) / TM_ROWS_PG;
    if (tm_pages_max < 1) tm_pages_max = 1;
    if (tm_page >= tm_pages_max) tm_page = 0;
    ui_rrect(60, 420, 2040, 690, 24, UC_WHITE);
    T16(100, 456, "槽位", UC_SUB); T16(220, 456, "进程号", UC_SUB);
    T16(400, 456, "状态", UC_SUB); T16(640, 456, "优先级", UC_SUB);
    T16(880, 456, "段号", UC_SUB); T16(1060, 456, "重试", UC_SUB);
    T16(1240, 456, "标志", UC_SUB);
    fb_fill(UX(100), UY(504), UX(2060), UY(505), fb_map(UC_CARDLINE));

    int base = tm_page * TM_ROWS_PG, shown = 0;
    for (int i = 0; i < MAX_TASK && shown < TM_ROWS_PG; i++) {
        task_t *tp = &task_pool[i];
        if (tp->state == TASK_IDLE) continue;
        if (base > 0) { base--; continue; }           // 跳过前几页的活动任务
        int yy = 540 + shown * 48;
        uint32_t cv = (tp->state == TASK_RUNNING) ? UC_BLUE : UC_TEXT;
        hw_itoa(t, i);                     T16(100, yy, t, cv);
        hw_itoa(t, (int)tp->pid);          T16(220, yy, t, cv);
        T16(400, yy, tm_state_cn(tp->state), cv);
        T16(640, yy, tm_prio_cn(tp->priority), cv);
        hw_itoa(t, (int)tp->seg_id);       T16(880, yy, t, cv);
        hw_itoa(t, (int)tp->retry_times);  T16(1060, yy, t, cv);
        b[0] = 0;
        if (tp->is_timeout) hw_strcat(b, "超时 ");
        if (tp->is_stuck)   hw_strcat(b, "卡死");
        if (b[0] == 0)      hw_strcat(b, "-");
        T16(1240, yy, b, (tp->is_stuck || tp->is_timeout) ? 0xE5484Du : UC_SUB);
        shown++;
    }
    if (shown == 0) T16(100, 540, "本页无活动任务 · 提权后在控制台用 创建 命令建立任务", UC_SUB);

    // ---- 底部翻页条 ----
    if (tm_page > 0) {
        ui_rrect(60, 1140, 300, 90, 20, UC_WHITE);
        T22(120, 1165, "上一页", UC_TEXT);
    } else {
        b[0] = 0;
        hw_itoa(t, act); hw_strcat(b, "活动任务 x"); hw_strcat(b, t);
        hw_strcat(b, " · 池容量 ");
        hw_itoa(t, MAX_TASK); hw_strcat(b, t);
        T16(80, 1168, b, UC_SUB);
    }
    if (tm_page < tm_pages_max - 1) {
        ui_rrect(1800, 1140, 300, 90, 20, UC_BLUE);
        T22(1860, 1165, "下一页", 0xFFFFFFu);
    }
    b[0] = 0;
    hw_itoa(t, tm_page + 1); hw_strcat(b, t); hw_strcat(b, " / ");
    hw_itoa(t, tm_pages_max);  hw_strcat(b, t);
    T16C(1080, 1168, b, UC_SUB);

    ui_draw_dock(6);
    ui_draw_dots(6);
}

static void ui_render(void) {
    if (ui_page == 0) ui_render_lock();
    else if (ui_page == 1) ui_render_home();
    else if (ui_page == 2) ui_render_ctrl();
    else if (ui_page == 3) ui_render_set();
    else if (ui_page == 4) ui_render_hub();
    else if (ui_page == 5) ui_render_multi();
    else if (ui_page == 6) ui_render_hw();
    else if (ui_page == 8) ui_render_tm();
    else ui_render_msg();
}

// ---------------- 交互 ----------------
static void ui_goto(int p) {
    ui_page = p;
    ui_render();
}

void ui_enter(void) {
    // 等比缩放 + 居中, 并按缩放选最近字库档
    int sx = (int)((long)g_fb_w * 1000 / UI_LW);
    int sy = (int)((long)g_fb_h * 1000 / UI_LH);
    g_ux_s = sx < sy ? sx : sy;
    if (g_ux_s < 60) g_ux_s = 60;                 // 极端小屏防御
    int vw = (int)((long)UI_LW * g_ux_s / 1000);
    int vh = (int)((long)UI_LH * g_ux_s / 1000);
    g_ux_ox = (g_fb_w - vw) / 2; if (g_ux_ox < 0) g_ux_ox = 0;
    g_ux_oy = (g_fb_h - vh) / 2; if (g_ux_oy < 0) g_ux_oy = 0;
    // 文字档位: 理想 px = 逻辑 px * s, 四舍五入选最近档
    int i16 = (16 * g_ux_s + 500) / 1000;
    int i22 = (22 * g_ux_s + 500) / 1000;
    int i160 = (160 * g_ux_s + 500) / 1000;
    g_f16 = (i16 < 14) ? 12 : 16;
    if      (i22 < 14) g_f22 = 12;
    else if (i22 < 19) g_f22 = 16;
    else               g_f22 = 22;
    if      (i160 < 116) g_f160 = 104;
    else if (i160 < 144) g_f160 = 128;
    else                 g_f160 = 160;

    ui_page = 0;
    ui_tiles[0] = 1; ui_tiles[1] = 1; ui_tiles[2] = 0; ui_tiles[3] = 0;
    ui_tiles[4] = 0; ui_tiles[5] = 1; ui_tiles[6] = 1; ui_tiles[7] = 0;
    mt_open[0] = 1; mt_open[1] = 1; mt_open[2] = 1; mt_open[3] = 1;
    mt_mode = 0;
    ui_render();
}

void ui_tick(void) {
    i2c_hid_poll();                          // I2C-HID 触摸非阻塞轮询 (未在线立即返回)
    usb_cursor_tick();                       // USB 轮询 + 光标
    // 使用 USB/I2C 层锁存的一次性按下事件 (避免 down/up 同 tick 被批量处理丢边沿)
    int click = g_btn_press;
    g_btn_press = 0;
    int keyp = 0;
    // Esc = 退出图形界面, 返回中文控制台 (最高优先级, 先于一切页面交互)
    int ukc = usb_kb_getc();
    if (ukc == 0x1B) { g_ui_exit = 1; return; }
    if (ukc >= 0) keyp = 1;
    int psc = 0;
    if ((inb(0x64) & 1) != 0) { psc = inb(0x60); keyp = 1; }
    if (psc == 0x01) { g_ui_exit = 1; return; }   // PS/2 Esc 通码
    if (!click && !keyp) {
        // 任务管理器页: 调度状态签名变化才重绘 (静止不闪, 变化即刷新)
        if (ui_page == 8) {
            unsigned sg = tm_sig();
            if (sg != tm_last_sig) { tm_last_sig = sg; ui_render(); }
        }
        return;
    }

    // 物理 -> 逻辑逆映射 (letterbox: 先减去居中偏移再按千分比还原)
    int mx = (int)(((long)(cur_x - g_ux_ox) * 1000) / g_ux_s);
    int my = (int)(((long)(cur_y - g_ux_oy) * 1000) / g_ux_s);

    if (ui_page == 0) {
        if (click || keyp) ui_goto(1);
        return;
    }
    // 键盘快捷键: 桌面按 t 开任务管理器, 任务管理器按 t 回桌面 (PS/2 set-1 T=0x14 通码)
    if ((ukc == 't' || psc == 0x14) && ui_page == 1) { ui_goto(8); return; }
    if ((ukc == 't' || psc == 0x14) && ui_page == 8) { ui_goto(1); return; }
    // 通用导航: 四 tab dock + 页面圆点 (锁屏外所有页)
    if (click) {
        int d = ui_dock_hit(mx, my);
        if (d >= 0 && d != ui_page) { ui_goto(d); return; }
        d = ui_dot_hit(mx, my);
        if (d >= 0 && d != ui_page) { ui_goto(d); return; }
    }

    if (ui_page == 1) {
        if (click && my < 80 && mx > 1800) ui_goto(2);        // 状态栏右侧 -> 控制中心
        else if (click && my >= 250 && my <= 420 && mx >= 1395 && mx <= 1545) ui_goto(3); // 设置图标
    } else if (ui_page == 2) {
        if (click) {
            // 8 个开关 tile
            int tx0 = 750, ty0 = 200, tw = 310, th = 150, tgap = 20;
            for (int i = 0; i < 8; i++) {
                int col = i % 4, row = i / 4;
                int x = tx0 + col * (tw + tgap), y = ty0 + row * (th + 30);
                if (mx >= x && mx < x + tw && my >= y && my < y + th) {
                    ui_tiles[i] ^= 1;
                    ui_render();
                    break;
                }
            }
        }
    } else if (ui_page == 3) {
        if (click && mx >= 40 && mx < 580 && my >= 284 && my <= 1280) {
            int idx = (my - 284) / 76;
            if (idx == 10) ui_goto(5);              // 多任务与分屏
            else if (idx == 11) ui_goto(6);          // 硬件检测
            else if (idx == 12) ui_goto(8);          // 任务管理器
        }
    } else if (ui_page == 5) {
        if (!click) return;
        // 关闭按钮 (第 k 张开着的卡)
        int k = 0;
        for (int i = 0; i < 4; i++) {
            if (!mt_open[i]) continue;
            int x = 60 + k * 520;
            k++;
            if (mx >= x + 390 && mx <= x + 470 && my >= 680 && my <= 760) {
                mt_open[i] = 0;
                ui_render();
                return;
            }
        }
        // 模式 chips + 全部关闭
        if (my >= 88 && my <= 144) {
            if (mx >= 1380 && mx < 1530) { mt_mode = 0; ui_render(); }
            else if (mx >= 1550 && mx < 1680) { mt_mode = 1; ui_render(); }
            else if (mx >= 1720 && mx < 1870) { mt_mode = 2; ui_render(); }
            else if (mx >= 1910 && mx <= 2100) {
                mt_open[0] = mt_open[1] = mt_open[2] = mt_open[3] = 0;
                ui_render();
            }
        }
    } else if (ui_page == 6) {
        if (!click) return;
        // 硬件检测: 底部翻页条 (概览 <-> PCI 设备表)
        if (my >= 1140 && my <= 1230) {
            if (mx >= 60 && mx <= 360 && hw_hwpage > 0) {
                hw_hwpage--;
                ui_render();
            } else if (mx >= 1800 && mx <= 2100 && hw_hwpage < hw_pages_max - 1) {
                hw_hwpage++;
                ui_render();
            }
        }
    } else if (ui_page == 8) {
        if (!click) return;
        // 任务管理器: 底部翻页条
        if (my >= 1140 && my <= 1230) {
            if (mx >= 60 && mx <= 360 && tm_page > 0) {
                tm_page--;
                ui_render();
            } else if (mx >= 1800 && mx <= 2100 && tm_page < tm_pages_max - 1) {
                tm_page++;
                ui_render();
            }
        }
    }
}

#endif // UI_SHELL_H
