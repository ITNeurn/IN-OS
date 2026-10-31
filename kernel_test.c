// kernel_test.c — 最小 32 位保护模式 C 内核
// 加载到 0x10200 执行，直接写 VGA 显存
// 用 TCC -m32 编译为 flat binary
//
// 注意: kernel_main 必须是 binary 文件的第一个函数
// 因为 PM Loader 直接 jmp 到加载地址 (0x10200) 执行

// VGA 文本模式
#define VGA_BUF ((volatile unsigned short*)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

static int vga_row = 0;
static int vga_col = 0;

// 前向声明 (让 kernel_main 可以放在第一个)
static void vga_clear(void);
static void vga_putc(char c);
static void vga_puts(const char *s);
static void vga_mark(char c, int pos);

// 入口点 — 必须是 binary 文件的第一个函数，被 PM Loader 跳转调用
void kernel_main(void) {
    // 调试标记：写 'K' 到 VGA 第 10 字符 (偏移 20)
    vga_mark('K', 10);
    vga_mark('1', 11);

    vga_clear();

    // 调试标记：清屏后写 'C' 到第 12 字符
    vga_mark('C', 12);

    vga_puts("============================================\n");
    vga_puts("  IN OS (c) 2026\n");
    vga_puts("  32-bit Protected Mode Active\n");
    vga_puts("  C kernel compiled with TCC\n");
    vga_puts("============================================\n\n");
    vga_puts("VGA text mode OK.\n");
    vga_puts("Kernel running at 0x10200.\n\n");
    vga_puts("> ");

    // 调试标记：banner 后写 'D' 到第 13 字符
    vga_mark('D', 13);

    // hlt 循环
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

static void vga_mark(char c, int pos) {
    ((volatile unsigned char*)0xB8000)[pos*2] = (unsigned char)c;
    ((volatile unsigned char*)0xB8000)[pos*2+1] = 0x07;
}

static void vga_clear(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_BUF[i] = (0x07 << 8) | ' ';
    vga_row = 0;
    vga_col = 0;
}

static void vga_putc(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        return;
    }
    if (c == '\r') { vga_col = 0; return; }
    VGA_BUF[vga_row * VGA_COLS + vga_col] = (0x07 << 8) | (unsigned char)c;
    vga_col++;
    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
    }
}

static void vga_puts(const char *s) {
    while (*s) vga_putc(*s++);
}
