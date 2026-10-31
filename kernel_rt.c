// kernel_rt.c — 裸机 C 运行时
// 提供 printf / memset / memcpy 等基础函数，替代 stdio/string
// VGA 文本模式 0xB8000，80x25，直接写显存
#ifndef KERNEL_RT_C
#define KERNEL_RT_C

#include <stdint.h>

// VGA 文本模式显存基地址
#define VGA_BUF   ((uint16_t*)0xB8000)
#define VGA_COLS   80
#define VGA_ROWS   25

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_attr = 0x07;  // 灰底黑字

static void vga_scroll(void) {
    for (int r = 0; r < VGA_ROWS - 1; r++) {
        for (int c = 0; c < VGA_COLS; c++)
            VGA_BUF[r * VGA_COLS + c] = VGA_BUF[(r + 1) * VGA_COLS + c];
    }
    for (int c = 0; c < VGA_COLS; c++)
        VGA_BUF[(VGA_ROWS - 1) * VGA_COLS + c] = (uint16_t)vga_attr << 8;
}

static void vga_putc(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_ROWS) { vga_row = VGA_ROWS - 1; vga_scroll(); }
        return;
    }
    if (c == '\r') { vga_col = 0; return; }
    if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_COLS) { vga_col = 0; vga_row++; }
        return;
    }
    VGA_BUF[vga_row * VGA_COLS + vga_col] = (uint16_t)vga_attr << 8 | (uint8_t)c;
    vga_col++;
    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_ROWS) { vga_row = VGA_ROWS - 1; vga_scroll(); }
    }
}

static void vga_puts(const char *s) {
    while (*s) vga_putc(*s++);
}

static void vga_clear(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_BUF[i] = (uint16_t)vga_attr << 8 | ' ';
    vga_row = 0; vga_col = 0;
}

// 简易 printf（支持 %d %x %s %c %%）
static void kvprintf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { vga_putc(*fmt++); continue; }
        fmt++;
        switch (*fmt) {
            case 'd': {
                int v = __builtin_va_arg(ap, int);
                if (v < 0) { vga_putc('-'); v = -v; }
                char buf[12]; int i = 0;
                if (v == 0) buf[i++] = '0';
                while (v > 0) { buf[i++] = '0' + v % 10; v /= 10; }
                while (i > 0) vga_putc(buf[--i]);
                break;
            }
            case 'x': case 'X': {
                unsigned int v = __builtin_va_arg(ap, unsigned int);
                char buf[12]; int i = 0;
                if (v == 0) buf[i++] = '0';
                while (v > 0) { int d = v % 16; buf[i++] = d < 10 ? '0'+d : 'A'+d-10; v /= 16; }
                while (i > 0) vga_putc(buf[--i]);
                break;
            }
            case 'u': {
                unsigned int v = __builtin_va_arg(ap, unsigned int);
                char buf[12]; int i = 0;
                if (v == 0) buf[i++] = '0';
                while (v > 0) { buf[i++] = '0' + v % 10; v /= 10; }
                while (i > 0) vga_putc(buf[--i]);
                break;
            }
            case 's': {
                char *s = __builtin_va_arg(ap, char*);
                vga_puts(s ? s : "(null)");
                break;
            }
            case 'c': {
                int c = __builtin_va_arg(ap, int);
                vga_putc((char)c);
                break;
            }
            case 'l': {
                fmt++;
                if (*fmt == 'd' || *fmt == 'u') {
                    unsigned long v = __builtin_va_arg(ap, unsigned long);
                    char buf[24]; int i = 0;
                    if (v == 0) buf[i++] = '0';
                    while (v > 0) { buf[i++] = '0' + (int)(v % 10); v /= 10; }
                    while (i > 0) vga_putc(buf[--i]);
                } else if (*fmt == 'x') {
                    unsigned long v = __builtin_va_arg(ap, unsigned long);
                    char buf[24]; int i = 0;
                    if (v == 0) buf[i++] = '0';
                    while (v > 0) { int d = (int)(v % 16); buf[i++] = d<10?'0'+d:'A'+d-10; v /= 16; }
                    while (i > 0) vga_putc(buf[--i]);
                }
                break;
            }
            case '%': vga_putc('%'); break;
            default: vga_putc('?'); break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}

#define printf kvprintf

// string.h 替代
void* kmemset(void *dst, int c, int n) {
    unsigned char *d = (unsigned char*)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#define memset kmemset

void* kmemcpy(void *dst, const void *src, int n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}
#define memcpy kmemcpy

int kstrlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
#define strlen kstrlen

int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
#define strcmp kstrcmp

#endif // KERNEL_RT_C
