// gui_cn.h - 中文字库公共层 (Noto Sans SC, gen_font.py 生成数据+渲染函数)
// 三档: 16px 小字 / 22px 正文标题 / 160px 时钟大数字; 4bpp 抗锯齿, UTF-8
// 依赖: g_fb/g_pitch/g_fb_w/g_fb_h/g_bppb, fb_map
#ifndef GUI_CN_H
#define GUI_CN_H

// ---- UTF-8 解码 (非法/截断=前进1) ----
static int utf8_dec(const unsigned char *s, unsigned *cp) {
    unsigned c = s[0];
    if (c < 0x80) { *cp = c; return 1; }
    if ((c >> 5) == 0x6 && (s[1] & 0xC0) == 0x80) {
        *cp = ((c & 0x1F) << 6) | (s[1] & 0x3F); return 2;
    }
    if ((c >> 4) == 0xE && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); return 3;
    }
    if ((c >> 3) == 0x1E && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80
                       && (s[3] & 0xC0) == 0x80) {
        *cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12)
            | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    *cp = c; return 1;
}

// ---- 内存序 alpha 混合单像素 (颜色均为帧缓冲内存序) ----
static void fcn_blend(int x, int y, unsigned a, uint32_t fg) {
    if ((uint32_t)x >= g_fb_w || (uint32_t)y >= g_fb_h || a == 0) return;
    volatile uint8_t *p = g_fb + (uint32_t)y * g_pitch + (uint32_t)x * g_bppb;
    if (a >= 15) {
        if (g_bppb == 4) *(volatile uint32_t *)p = fg;
        else { p[0] = (uint8_t)fg; p[1] = (uint8_t)(fg >> 8); p[2] = (uint8_t)(fg >> 16); }
        return;
    }
    uint32_t ia = 16u - a;
    if (g_bppb == 4) {
        uint32_t d = *(volatile uint32_t *)p;
        uint32_t r = (((fg & 0xFF) * a + (d & 0xFF) * ia) >> 4) & 0xFF;
        uint32_t g2= ((((fg >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia) >> 4) & 0xFF;
        uint32_t b = ((((fg >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * ia) >> 4) & 0xFF;
        *(volatile uint32_t *)p = r | (g2 << 8) | (b << 16);
    } else {
        for (int k = 0; k < 3; k++) {
            uint32_t d = p[k];
            p[k] = (uint8_t)((((fg >> (k * 8)) & 0xFF) * a + d * ia) >> 4);
        }
    }
}

// 字库档位 (各头自带 fcn{px}_find/glyph/text/w/center)
#include "font_cn12.h"
#include "font_cn16.h"
#include "font_cn22.h"
#include "font_cn104.h"
#include "font_cn128.h"
#include "font_cn160.h"

#endif // GUI_CN_H
