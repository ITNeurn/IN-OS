// kernel_full.c — 完整可启动微内核系统
// 集成：VGA 文本输出 + 键盘驱动 + 任务调度器 + CLI 交互终端
// 加载到 0x300200 执行，用 TCC -m32 -nostdlib -ffreestanding -Wl,-Ttext,0x300200 编译
//
// 注意: kernel_main 必须是 binary 文件的第一个函数定义，
// 因为 PM Loader 直接 jmp 到加载地址 (0x300200) 执行。
//
// 设计参考: super_scheduler.c (任务池/优先级/状态机/看门狗/统计)
//          user_cli.c (命令解析/执行/终端循环)
//          kernel_rt.c (VGA printf/memset/memcpy/strlen/strcmp)

// TCC 0.9.27 的 include 目录没有 stdint.h，自定义固定宽度整数类型
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long long uint64_t;
#define NULL ((void*)0)

// 8x16 ASCII 点阵字库 (提取自项目"字库"模块 ascii_font.c)
#include "font_fb.h"

// ====================== 类型与常量 ======================
#define VGA_COLS 80
#define VGA_ROWS 25
#define MAX_TASK 64
#define IR_MAX_BUF 1024
#define CLI_BUF_SIZE 256
#define TIMEOUT_TICK 5000
#define DEADLOCK_TICK 80000
#define WATCHDOG_INTERVAL 1000

typedef enum {
    PRIORITY_IDLE = 0, PRIORITY_LOW = 1, PRIORITY_NORMAL = 2,
    PRIORITY_HIGH = 3, PRIORITY_REAL_TIME = 4
} task_prio_t;

typedef enum {
    TASK_IDLE, TASK_WAIT_IR, TASK_COMPILE_ARM, TASK_TRANS_X86,
    TASK_CACHE_STORE, TASK_FORWARD_SCHED, TASK_RUNNING,
    TASK_CRASH_RECOVER, TASK_FINISH, TASK_STUCK_DETECT, TASK_RETRY_QUEUE
} task_state_t;

typedef struct {
    uint32_t pid;
    task_prio_t priority;
    task_state_t state;
    uint64_t create_tick;
    uint64_t last_run_tick;
    uint32_t seg_id;
    uint32_t ir_len;
    unsigned char ir_data[IR_MAX_BUF];
    uint8_t retry_times;
    uint8_t is_timeout;
    uint8_t is_stuck;
} task_t;

typedef struct {
    uint64_t total_create, total_finish, total_crash, total_retry;
    uint64_t total_stuck, total_forward_byte, total_snap_byte;
} sched_stat_t;

// ====================== 全局变量（.bss/.data，不影响 .text 顺序）======================
static volatile uint16_t *const VGA_BUF = (volatile uint16_t*)0xB8000;
static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_attr = 0x07;
static uint64_t g_kernel_tick = 0;
static uint64_t g_watchdog_tick = 0;
static int g_sched_inited = 0;
static task_t task_pool[MAX_TASK];
static sched_stat_t g_stat;
static char cli_buf[CLI_BUF_SIZE];
static int kb_shift = 0;
static int kb_caps = 0;

// ====================== 端口 I/O ======================
static inline unsigned char inb(unsigned short port) {
    unsigned char v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(unsigned short port, unsigned char v) {
    __asm__ __volatile__("outb %0, %1" : : "a"(v), "Nd"(port));
}

// ====================== 键盘扫描码表（const，放 .rodata）======================
// 索引 = 扫描码 (0x00-0x7F)，值 = ASCII（0 表示无 ASCII 输出）
// 每行 16 项，共 8 行 = 128 项
static const unsigned char scancode_to_ascii[128] = {
    /* 0x00-0x0F */ 0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',
                    '7',  '8',  '9',  '0',  '-',  '=',  0x08, '\t',
    /* 0x10-0x1F */ 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
                    'o',  'p',  '[',  ']',  0x0A, 0,    'a',  's',
    /* 0x20-0x2F */ 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
                    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    /* 0x30-0x3F */ 'b',  'n',  'm',  ',',  '.',  '/',  0,    0,
                    0,    ' ',  0,    0,    0,    0,    0,    0,
    /* 0x40-0x4F */ 0,    0,    0,    0,    0,    0,    0,    '7',
                    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    /* 0x50-0x5F */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
                    0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x60-0x6F */ 0,    0,    0,    0,    0,    0,    0,    0,
                    0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x70-0x7F */ 0,    0,    0,    0,    0,    0,    0,    0,
                    0,    0,    0,    0,    0,    0,    0,    0
};
static const unsigned char scancode_to_ascii_shift[128] = {
    /* 0x00-0x0F */ 0,    0x1B, '!',  '@',  '#',  '$',  '%',  '^',
                    '&',  '*',  '(',  ')',  '_',  '+',  0x08, '\t',
    /* 0x10-0x1F */ 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
                    'O',  'P',  '{',  '}',  0x0A, 0,    'A',  'S',
    /* 0x20-0x2F */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
                    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    /* 0x30-0x3F */ 'B',  'N',  'M',  '<',  '>',  '?',  0,    0,
                    0,    ' ',  0,    0,    0,    0,    0,    0,
    /* 0x40-0x4F */ 0,    0,    0,    0,    0,    0,    0,    '7',
                    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    /* 0x50-0x5F */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
                    0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x60-0x6F */ 0,    0,    0,    0,    0,    0,    0,    0,
                    0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x70-0x7F */ 0,    0,    0,    0,    0,    0,    0,    0,
                    0,    0,    0,    0,    0,    0,    0,    0
};

// ====================== 前向声明（让 kernel_main 可放第一个）======================
static void vga_scroll(void);
static void vga_putc(char c);
static void vga_puts(const char *s);
static void vga_clear(void);
static void kvprintf(const char *fmt, ...);
static void* kmemset(void *dst, int c, int n);
static void* kmemcpy(void *dst, const void *src, int n);
static int kstrcmp(const char *a, const char *b);
static int katoi(const char *s);
static unsigned long kstrtoul(const char *s, int base);
static unsigned char kb_read_scancode(void);
static int kb_getc(void);
static void tick_advance(void);
static uint64_t sys_get_tick(void);
static const char* state_name(task_state_t s);
static const char* prio_name(task_prio_t p);
static void sched_reset(void);
static int sched_watchdog(void);
static int sched_create_task(uint32_t pid, task_prio_t prio, uint32_t seg_id,
                             const unsigned char *ir, uint32_t ir_len);
static int sched_run_task(int idx);
static int sched_run_all(void);
static int sched_snapshot(uint32_t pid, uint32_t vaddr, uint32_t len);
static int sched_recover_crash(void);
static int sched_count_active(void);
static void sched_init(void);
static void cli_readline(const char *prompt);
static int cli_tokenize(char *buf, char *args[], int max_args);
static void cli_exec(char *args[], int argc);
static void print_banner(void);
static void gui_main(void);
static void text_main(void);
static void render_monitor(void);

// ====================== 入口点（必须是第一个函数定义）======================
// PM Loader 切换到 32 位保护模式后 jmp 0x300200 = kernel_main
void kernel_main(void) {
    // loader VBE 成功则进入图形桌面 (围绕超级调度器的 GUI);
    // 否则回退到 VGA 文本 CLI
    volatile uint32_t *vbe_magic = (volatile uint32_t*)0x7000;
    if (*vbe_magic == 0x31454256u) {      // 'VBE1'
        gui_main();
        return;
    }
    text_main();
}

// ====================== 文本模式主流程 (VBE 不可用时兜底) ======================
static void text_main(void) {
    // 调试标记 'K' (VGA 第 10 字符)
    ((volatile unsigned char*)0xB8000)[20] = 'K';
    ((volatile unsigned char*)0xB8000)[21] = 0x07;

    vga_clear();

    // 调试标记 'C' (清屏后)
    ((volatile unsigned char*)0xB8000)[24] = 'C';
    ((volatile unsigned char*)0xB8000)[25] = 0x07;

    print_banner();
    sched_init();

    vga_puts("Type 'help' for commands, 'about' for system info.\n\n");
    for (;;) {
        cli_readline("sys_sched > ");
        char *args[16];
        int argc = cli_tokenize(cli_buf, args, 16);
        cli_exec(args, argc);
    }
}

// ====================== VGA 文本模式 ======================
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
        vga_col = 0; vga_row++;
        if (vga_row >= VGA_ROWS) { vga_row = VGA_ROWS - 1; vga_scroll(); }
        return;
    }
    if (c == '\r') { vga_col = 0; return; }
    if (c == '\b') {
        if (vga_col > 0) { vga_col--; VGA_BUF[vga_row*VGA_COLS+vga_col] = ((uint16_t)vga_attr<<8)|' '; }
        return;
    }
    if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_COLS) { vga_col = 0; vga_row++; }
        return;
    }
    VGA_BUF[vga_row * VGA_COLS + vga_col] = ((uint16_t)vga_attr << 8) | (uint8_t)c;
    vga_col++;
    if (vga_col >= VGA_COLS) {
        vga_col = 0; vga_row++;
        if (vga_row >= VGA_ROWS) { vga_row = VGA_ROWS - 1; vga_scroll(); }
    }
}

static void vga_puts(const char *s) { while (*s) vga_putc(*s++); }

static void vga_clear(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_BUF[i] = ((uint16_t)vga_attr << 8) | ' ';
    vga_row = 0; vga_col = 0;
}

// ====================== libc 替代 ======================
static void* kmemset(void *dst, int c, int n) {
    unsigned char *d = (unsigned char*)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
static void* kmemcpy(void *dst, const void *src, int n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}
static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static int katoi(const char *s) {
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v * sign;
}
static unsigned long kstrtoul(const char *s, int base) {
    unsigned long v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (base == 0 || base == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
        else if (base == 0) base = 10;
    }
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    return v;
}

// ====================== 键盘驱动 ======================
static unsigned char kb_read_scancode(void) {
    while ((inb(0x64) & 0x01) == 0) {
        __asm__ __volatile__("hlt");
    }
    return inb(0x60);
}

static int kb_getc(void) {
    for (;;) {
        unsigned char sc = kb_read_scancode();
        if (sc == 0xE0) { kb_read_scancode(); continue; }
        if (sc == 0xE1) {
            for (int i = 0; i < 5; i++) kb_read_scancode();
            continue;
        }
        if (sc & 0x80) {
            unsigned char mk = sc & 0x7F;
            if (mk == 0x2A || mk == 0x36) kb_shift = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) { kb_shift = 1; continue; }
        if (sc == 0x3A) { kb_caps = !kb_caps; continue; }

        unsigned char ascii = kb_shift ? scancode_to_ascii_shift[sc]
                                       : scancode_to_ascii[sc];
        if (kb_caps) {
            if (ascii >= 'a' && ascii <= 'z') ascii -= 32;
            else if (ascii >= 'A' && ascii <= 'Z') ascii += 32;
        }
        return (int)ascii;
    }
}

// ====================== 系统 tick ======================
static void tick_advance(void) { g_kernel_tick++; }
static uint64_t sys_get_tick(void) { return g_kernel_tick; }

// ====================== 调度器 ======================
static const char* state_name(task_state_t s) {
    switch (s) {
        case TASK_IDLE: return "IDLE";
        case TASK_WAIT_IR: return "WAIT_IR";
        case TASK_COMPILE_ARM: return "COMPILE";
        case TASK_TRANS_X86: return "TRANS";
        case TASK_CACHE_STORE: return "CACHE";
        case TASK_FORWARD_SCHED: return "FORWARD";
        case TASK_RUNNING: return "RUNNING";
        case TASK_CRASH_RECOVER: return "CRASH";
        case TASK_FINISH: return "FINISH";
        case TASK_STUCK_DETECT: return "STUCK";
        case TASK_RETRY_QUEUE: return "RETRY";
        default: return "?";
    }
}
static const char* prio_name(task_prio_t p) {
    switch (p) {
        case PRIORITY_IDLE: return "IDLE";
        case PRIORITY_LOW: return "LOW";
        case PRIORITY_NORMAL: return "NORMAL";
        case PRIORITY_HIGH: return "HIGH";
        case PRIORITY_REAL_TIME: return "RT";
        default: return "?";
    }
}

static void sched_reset(void) {
    kmemset(task_pool, 0, sizeof(task_pool));
    kmemset(&g_stat, 0, sizeof(g_stat));
    g_watchdog_tick = 0;
}

static int sched_watchdog(void) {
    uint64_t now = sys_get_tick();
    if (now - g_watchdog_tick < WATCHDOG_INTERVAL) return 0;
    g_watchdog_tick = now;
    int stuck_cnt = 0;
    for (int i = 0; i < MAX_TASK; i++) {
        task_t *t = &task_pool[i];
        if (t->state == TASK_IDLE || t->state == TASK_FINISH) continue;
        if (now - t->last_run_tick > DEADLOCK_TICK) {
            t->is_stuck = 1;
            t->state = TASK_STUCK_DETECT;
            g_stat.total_stuck++;
            stuck_cnt++;
        }
        if (now - t->last_run_tick > TIMEOUT_TICK) {
            t->is_timeout = 1;
            t->state = TASK_CRASH_RECOVER;
        }
    }
    return stuck_cnt;
}

static int sched_create_task(uint32_t pid, task_prio_t prio, uint32_t seg_id,
                             const unsigned char *ir, uint32_t ir_len) {
    for (int i = 0; i < MAX_TASK; i++) {
        if (task_pool[i].state == TASK_IDLE || task_pool[i].state == TASK_FINISH) {
            task_t *t = &task_pool[i];
            kmemset(t, 0, sizeof(*t));
            t->pid = pid;
            t->priority = prio;
            t->state = TASK_WAIT_IR;
            t->seg_id = seg_id;
            t->create_tick = sys_get_tick();
            t->last_run_tick = t->create_tick;
            t->ir_len = ir_len > IR_MAX_BUF ? IR_MAX_BUF : ir_len;
            if (ir && t->ir_len > 0) kmemcpy(t->ir_data, ir, t->ir_len);
            g_stat.total_create++;
            return i;
        }
    }
    return -1;
}

static int sched_run_task(int idx) {
    if (idx < 0 || idx >= MAX_TASK) return 0;
    task_t *t = &task_pool[idx];
    if (t->state != TASK_WAIT_IR && t->state != TASK_RETRY_QUEUE) return 0;
    tick_advance();
    int x86_len = (int)t->ir_len;
    if (x86_len <= 0) x86_len = 64;
    t->state = TASK_COMPILE_ARM;
    tick_advance(); t->state = TASK_TRANS_X86;
    tick_advance(); t->state = TASK_CACHE_STORE;
    tick_advance(); t->state = TASK_FORWARD_SCHED;
    g_stat.total_forward_byte += x86_len;
    t->state = TASK_RUNNING;
    t->last_run_tick = sys_get_tick();
    t->state = TASK_FINISH;
    g_stat.total_finish++;
    return 1;
}

static int sched_run_all(void) {
    sched_watchdog();
    int succ = 0;
    for (int p = PRIORITY_REAL_TIME; p >= PRIORITY_LOW; p--) {
        for (int i = 0; i < MAX_TASK; i++) {
            task_t *t = &task_pool[i];
            if (t->priority == p &&
                (t->state == TASK_WAIT_IR || t->state == TASK_RETRY_QUEUE)) {
                if (sched_run_task(i)) succ++;
            }
        }
    }
    return succ;
}

static int sched_snapshot(uint32_t pid, uint32_t vaddr, uint32_t len) {
    (void)pid; (void)vaddr;
    g_stat.total_snap_byte += len;
    return (int)len;
}

static int sched_recover_crash(void) {
    int cnt = 0;
    for (int i = 0; i < MAX_TASK; i++) {
        task_t *t = &task_pool[i];
        if (t->state == TASK_CRASH_RECOVER || t->state == TASK_STUCK_DETECT) {
            g_stat.total_crash++;
            t->state = TASK_IDLE;
            cnt++;
        }
    }
    return cnt;
}

static int sched_count_active(void) {
    int n = 0;
    for (int i = 0; i < MAX_TASK; i++) {
        if (task_pool[i].state != TASK_IDLE && task_pool[i].state != TASK_FINISH) n++;
    }
    return n;
}

static void sched_init(void) {
    sched_reset();
    g_sched_inited = 1;
    kvprintf("[sched] super_scheduler initialized (pool=%d)\n", MAX_TASK);
}

// ====================== printf ======================
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
                while (v > 0) { int d = v % 16; buf[i++] = d<10?'0'+d:'A'+d-10; v /= 16; }
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
                if (*fmt == 'u' || *fmt == 'd') {
                    unsigned long v = __builtin_va_arg(ap, unsigned long);
                    char buf[24]; int i = 0;
                    if (v == 0) buf[i++] = '0';
                    while (v > 0) { buf[i++] = '0' + (int)(v % 10); v /= 10; }
                    while (i > 0) vga_putc(buf[--i]);
                } else if (*fmt == 'x') {
                    unsigned long v = __builtin_va_arg(ap, unsigned long);
                    char buf[24]; int i = 0;
                    if (v == 0) buf[i++] = '0';
                    while (v > 0) { int d=(int)(v%16); buf[i++]=d<10?'0'+d:'A'+d-10; v/=16; }
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

// ====================== CLI 终端 ======================
static void cli_readline(const char *prompt) {
    vga_puts(prompt);
    int n = 0;
    for (;;) {
        int c = kb_getc();
        if (c == 0) continue;
        if (c == 0x1B) {
            while (n > 0) { vga_putc('\b'); n--; }
            continue;
        }
        if (c == 0x08) {
            if (n > 0) { n--; vga_putc('\b'); }
            continue;
        }
        if (c == 0x0A) {
            vga_putc('\n');
            cli_buf[n] = 0;
            return;
        }
        if (c >= 0x20 && c < 0x7F && n < CLI_BUF_SIZE - 1) {
            cli_buf[n++] = (char)c;
            vga_putc((char)c);
        }
    }
}

static int cli_tokenize(char *buf, char *args[], int max_args) {
    int n = 0;
    char *p = buf;
    while (*p && n < max_args) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        args[n++] = p;
        while (*p && *p != ' ') p++;
    }
    if (*p) *p = 0;
    return n;
}

static void cli_help(void) {
    vga_puts("Available commands:\n");
    vga_puts("  help              - show this help\n");
    vga_puts("  about             - about this kernel\n");
    vga_puts("  init              - init super_scheduler\n");
    vga_puts("  create <pid> <seg> <prio> - create a task (prio: 1=low 2=normal 3=high 4=rt)\n");
    vga_puts("  runall            - run all queued tasks (priority order)\n");
    vga_puts("  snap <pid> <vaddr> <len> - take memory snapshot\n");
    vga_puts("  recover           - recover crashed/stuck tasks\n");
    vga_puts("  stat              - show scheduler statistics\n");
    vga_puts("  tasks             - list task pool state\n");
    vga_puts("  clear             - clear screen\n");
    vga_puts("  echo <text>       - echo text\n");
    vga_puts("  reboot            - triple-fault reset\n");
    vga_puts("  exit              - halt CPU\n");
}

static void cli_about(void) {
    vga_puts("==================================================\n");
    vga_puts("  IN OS  (c) 2026\n");
    vga_puts("  32-bit Protected Mode  Interactive Kernel\n");
    vga_puts("  Built with TCC (freestanding)\n");
    vga_puts("  Components:\n");
    vga_puts("    * VGA text mode driver (0xB8000, 80x25)\n");
    vga_puts("    * PS/2 keyboard driver (port 0x60)\n");
    vga_puts("    * Micro scheduler (pool=64/priority/watchdog)\n");
    vga_puts("    * Interactive CLI terminal\n");
    vga_puts("  Entry: 0x300200   Loaded via PM Loader\n");
    vga_puts("==================================================\n");
}

static void cli_show_tasks(void) {
    vga_puts("idx  pid   state      prio    seg  retry timeout stuck\n");
    int shown = 0;
    for (int i = 0; i < MAX_TASK; i++) {
        task_t *t = &task_pool[i];
        if (t->state == TASK_IDLE) continue;
        shown++;
        printf("%d    %-5u %-10s %-6s %-4u %-5u %-7d %-7d\n",
               i, t->pid, state_name(t->state), prio_name(t->priority),
               t->seg_id, t->retry_times, t->is_timeout, t->is_stuck);
    }
    if (!shown) vga_puts("(no active tasks)\n");
}

static void cli_show_stat(void) {
    vga_puts("===== Scheduler Statistics =====\n");
    printf("total_create   : %lu\n", g_stat.total_create);
    printf("total_finish   : %lu\n", g_stat.total_finish);
    printf("total_crash    : %lu\n", g_stat.total_crash);
    printf("total_retry    : %lu\n", g_stat.total_retry);
    printf("total_stuck    : %lu\n", g_stat.total_stuck);
    printf("total_forward  : %lu bytes\n", g_stat.total_forward_byte);
    printf("total_snapshot : %lu bytes\n", g_stat.total_snap_byte);
    printf("active_tasks   : %d / %d\n", sched_count_active(), MAX_TASK);
    printf("kernel_tick    : %lu\n", g_kernel_tick);
}

static void cli_exec(char *args[], int argc) {
    if (argc == 0) return;
    const char *cmd = args[0];

    if (kstrcmp(cmd, "help") == 0 || kstrcmp(cmd, "?") == 0) { cli_help(); return; }
    if (kstrcmp(cmd, "about") == 0) { cli_about(); return; }
    if (kstrcmp(cmd, "clear") == 0 || kstrcmp(cmd, "cls") == 0) { vga_clear(); return; }
    if (kstrcmp(cmd, "init") == 0) { sched_init(); vga_puts("[ok] scheduler initialized\n"); return; }
    if (kstrcmp(cmd, "create") == 0 || kstrcmp(cmd, "create_task") == 0) {
        if (argc < 4) { vga_puts("usage: create <pid> <seg_id> <prio 1-4>\n"); return; }
        uint32_t pid = (uint32_t)katoi(args[1]);
        uint32_t seg = (uint32_t)katoi(args[2]);
        int p = katoi(args[3]);
        if (p < 1) p = 1; if (p > 4) p = 4;
        unsigned char sample[64];
        for (int i = 0; i < 64; i++) sample[i] = (unsigned char)(i & 0xFF);
        int tid = sched_create_task(pid, (task_prio_t)p, seg, sample, 64);
        if (tid >= 0)
            printf("[ok] task created: idx=%d pid=%u seg=%u prio=%s\n",
                   tid, pid, seg, prio_name((task_prio_t)p));
        else
            vga_puts("[err] task pool full\n");
        return;
    }
    if (kstrcmp(cmd, "runall") == 0 || kstrcmp(cmd, "run_all") == 0) {
        int ok = sched_run_all();
        printf("[ok] run complete: %d task(s) succeeded\n", ok);
        return;
    }
    if (kstrcmp(cmd, "snap") == 0) {
        if (argc < 4) { vga_puts("usage: snap <pid> <vaddr(hex)> <len>\n"); return; }
        uint32_t pid = (uint32_t)katoi(args[1]);
        uint32_t vaddr = (uint32_t)kstrtoul(args[2], 16);
        uint32_t len = (uint32_t)katoi(args[3]);
        int got = sched_snapshot(pid, vaddr, len);
        printf("[ok] snapshot: pid=%u vaddr=0x%x bytes=%d\n", pid, vaddr, got);
        return;
    }
    if (kstrcmp(cmd, "recover") == 0) {
        int n = sched_recover_crash();
        printf("[ok] recovered %d crashed/stuck task(s)\n", n);
        return;
    }
    if (kstrcmp(cmd, "stat") == 0) { cli_show_stat(); return; }
    if (kstrcmp(cmd, "tasks") == 0 || kstrcmp(cmd, "ps") == 0) { cli_show_tasks(); return; }
    if (kstrcmp(cmd, "watchdog") == 0) {
        int stuck = sched_watchdog();
        printf("[ok] watchdog scan: %d stuck\n", stuck);
        return;
    }
    if (kstrcmp(cmd, "exit") == 0 || kstrcmp(cmd, "halt") == 0) {
        vga_puts("\n[ok] halting CPU. Press power button to restart.\n");
        __asm__ __volatile__("cli");
        for (;;) __asm__ __volatile__("hlt");
        return;
    }
    if (kstrcmp(cmd, "reboot") == 0) {
        vga_puts("\n[ok] rebooting via triple-fault...\n");
        __asm__ __volatile__(
            "cli\n"
            "lidt 0\n"
            "int $0x03\n"
        );
        return;
    }
    if (kstrcmp(cmd, "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            vga_puts(args[i]);
            if (i < argc - 1) vga_putc(' ');
        }
        vga_putc('\n');
        return;
    }
    printf("[err] unknown command: %s  (try 'help')\n", cmd);
}

// ====================== 启动横幅 ======================
static void print_banner(void) {
    vga_puts("==================================================\n");
    vga_puts("   IN OS  (c) 2026  Booting...\n");
    vga_puts("   32-bit Protected Mode Active\n");
    vga_puts("   Interactive Micro-Scheduler + CLI Terminal\n");
    vga_puts("==================================================\n\n");
    vga_puts("[boot] kernel_main entered at 0x300200\n");
    vga_puts("[boot] VGA text mode initialized (80x25)\n");
    vga_puts("[boot] PS/2 keyboard driver online (port 0x60)\n");
    vga_puts("[boot] super_scheduler: pool=64, watchdog=on\n");
    vga_puts("[ok ] system fully booted. Entering CLI.\n\n");
}

// ======================================================================
//                     裸机图形桌面 GUI (围绕超级调度器)
//   loader VBE: 32bpp LFB, 参数块在物理 0x7000
//   - 帧缓冲驱动: 像素/矩形/线 + 8x16 点阵字体
//   - 桌面 + 任务栏(RTC时钟/tick)
//   - 超级调度器监控窗: 统计/64槽热力图/流水线状态行
//   - 图形终端: 非阻塞键盘, 命令直接驱动调度器
//   - PIT 轮询节拍 (~55ms/tick, 无需中断)
// ======================================================================

// ----- loader VBE 参数块 -----
typedef struct {
    uint32_t magic;
    uint32_t fb;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  rsvd;
    uint16_t mode;
    uint8_t  red_pos;       // RedFieldPosition: 16=BGR序直写, 0=需交换 R/B
    uint8_t  pad2;
    uint16_t pad3;
} vbe_block_t;
static volatile vbe_block_t *const g_vbe = (volatile vbe_block_t*)0x7000;

static volatile uint8_t *g_fb;
static uint32_t g_fb_w, g_fb_h, g_pitch;
static uint32_t g_bppb = 4;             // 每像素字节数 (由 pitch 推出: 3=packed24, 4=XRGB32)
static int g_swaprb = 0;                // 帧缓冲 R/B 通道顺序与 0x00RRGGBB 相反

static inline uint32_t fb_map(uint32_t c) {
    if (!g_swaprb) return c;
    return (c & 0x0000FF00u) | ((c & 0x000000FFu) << 16) | ((c >> 16) & 0x000000FFu);
}

// ----- 配色 (0x00RRGGBB) -----
#define C_DESKTOP   0x0B1020u
#define C_TASKBAR   0x091628u
#define C_ACCENT    0x3FA7E0u
#define C_WINBG     0x16202Eu
#define C_WINFRAME  0x324359u
#define C_TITLEBAR  0x21436Bu
#define C_TITLETX   0xEAf2FAu
#define C_TEXT      0xCBD6E2u
#define C_DIM       0x778699u
#define C_CYAN      0x4FC3F7u
#define C_CYAN2     0x8BE9FDu
#define C_BLUE      0x64B5F6u
#define C_GREEN     0x5BD17Au
#define C_YELLOW    0xFFD54Fu
#define C_RED       0xFF5D5Du
#define C_ORANGE    0xFF8C5Bu
#define C_PURPLE    0xB48BFFu
#define C_PINK      0xFF7AD9u
#define C_GRAY      0x5F6E80u
#define C_EMPTY     0x1D2837u

// ----- 窗口布局 -----
static int mon_x, mon_y, mon_w, mon_h;
static int term_x, term_y, term_w, term_h;
static int term_rows, term_cols;

// ----- 图形终端网格 -----
#define TMAX_ROWS 16
#define TMAX_COLS 132
static unsigned char term_ch[TMAX_ROWS][TMAX_COLS];
static unsigned char term_at[TMAX_ROWS][TMAX_COLS];
static int term_r, term_c;
static int in_len;

// ----- 演示流 -----
static int g_auto = 0;
static uint32_t g_demo_pid = 1;
static uint32_t g_rng = 0x12345678u;

// ----- 图形 UI 退出标志 (中文控制台 "启动UI" 进入, UI 内 Esc 置位返回) -----
int g_ui_exit = 0;

// ====================== 帧缓冲原语 ======================
static void fb_putpx(int x, int y, uint32_t c) {
    if ((uint32_t)x >= g_fb_w || (uint32_t)y >= g_fb_h) return;
    c = fb_map(c);
    volatile uint8_t *p = g_fb + (uint32_t)y * g_pitch + (uint32_t)x * g_bppb;
    if (g_bppb == 4) { *(volatile uint32_t*)p = c; }
    else { p[0] = (uint8_t)c; p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)(c >> 16); }
}

static void fb_fill(int x0, int y0, int x1, int y1, uint32_t c) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)g_fb_w - 1) x1 = (int)g_fb_w - 1;
    if (y1 > (int)g_fb_h - 1) y1 = (int)g_fb_h - 1;
    if (x1 < x0 || y1 < y0) return;
    c = fb_map(c);
    for (int y = y0; y <= y1; y++) {
        volatile uint8_t *row = g_fb + (uint32_t)y * g_pitch;
        if (g_bppb == 4) {
            volatile uint32_t *p = (volatile uint32_t*)row + x0;
            for (int x = x0; x <= x1; x++) *p++ = c;
        } else {
            uint8_t b0 = (uint8_t)c, b1 = (uint8_t)(c >> 8), b2 = (uint8_t)(c >> 16);
            for (int x = x0; x <= x1; x++) {
                volatile uint8_t *p = row + (uint32_t)x * 3;
                p[0] = b0; p[1] = b1; p[2] = b2;
            }
        }
    }
}

static void fb_hline(int x0, int x1, int y, uint32_t c) { fb_fill(x0, y, x1, y, c); }
static void fb_vline(int x, int y0, int y1, uint32_t c) { fb_fill(x, y0, x, y1, c); }

static void fb_frame(int x, int y, int w, int h, uint32_t c) {
    fb_hline(x, x + w - 1, y, c);
    fb_hline(x, x + w - 1, y + h - 1, c);
    fb_vline(x, y, y + h - 1, c);
    fb_vline(x + w - 1, y, y + h - 1, c);
}

// ====================== 8x16 字体渲染 ======================
static void fb_char(int x, int y, unsigned char ch, uint32_t fg, uint32_t bg) {
    const unsigned char *g = fb_font8x16[ch & 0x7F];
    int transp = (bg == 0xFF000000u);
    fg = fb_map(fg);
    if (!transp) bg = fb_map(bg);
    for (int row = 0; row < 16; row++) {
        unsigned char bits = g[row];
        volatile uint8_t *base = g_fb + (uint32_t)(y + row) * g_pitch + (uint32_t)x * g_bppb;
        for (int col = 0; col < 8; col++) {
            uint32_t use = (bits & (0x80 >> col)) ? fg : (transp ? 0xFF000000u : bg);
            if (use == 0xFF000000u) { base += g_bppb; continue; }
            if (g_bppb == 4) {
                *(volatile uint32_t*)base = use;
                base += 4;
            } else {
                base[0] = (uint8_t)use; base[1] = (uint8_t)(use >> 8); base[2] = (uint8_t)(use >> 16);
                base += 3;
            }
        }
    }
}

static void fb_str(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) { fb_char(x, y, (unsigned char)*s, fg, bg); x += 8; s++; }
}

static int u32_str(uint32_t v, char *b) {
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) b[i] = t[n - 1 - i];
    b[n] = 0;
    return n;
}

static int fb_u32(int x, int y, uint32_t v, uint32_t fg, uint32_t bg) {
    char b[12];
    int n = u32_str(v, b);
    fb_str(x, y, b, fg, bg);
    return n * 8;
}

// ====================== 状态配色 ======================
static uint32_t state_color(task_state_t s) {
    switch (s) {
        case TASK_IDLE:          return C_EMPTY;
        case TASK_WAIT_IR:       return C_YELLOW;
        case TASK_COMPILE_ARM:   return C_CYAN;
        case TASK_TRANS_X86:     return C_CYAN2;
        case TASK_CACHE_STORE:   return C_BLUE;
        case TASK_FORWARD_SCHED: return C_PURPLE;
        case TASK_RUNNING:       return C_GREEN;
        case TASK_CRASH_RECOVER: return C_RED;
        case TASK_FINISH:        return C_GRAY;
        case TASK_STUCK_DETECT:  return C_ORANGE;
        case TASK_RETRY_QUEUE:   return C_PINK;
        default:                 return C_TEXT;
    }
}

// ====================== 图形终端 ======================
static int term_bx(void) { return term_x + 10; }
static int term_by(void) { return term_y + 24 + 8; }

static void term_cell(int r, int c) {
    uint32_t col;
    switch (term_at[r][c]) {
        case 1: col = C_DIM; break;
        case 2: col = C_GREEN; break;
        case 3: col = C_RED; break;
        case 4: col = C_CYAN; break;
        case 5: col = C_YELLOW; break;
        default: col = C_TEXT;
    }
    fb_char(term_bx() + c * 8, term_by() + r * 16, term_ch[r][c], col, C_WINBG);
}

static void term_redraw(void) {
    fb_fill(term_bx(), term_by(),
            term_bx() + term_cols * 8 - 1, term_by() + term_rows * 16 - 1, C_WINBG);
    for (int r = 0; r < term_rows; r++)
        for (int c = 0; c < term_cols; c++)
            term_cell(r, c);
}

static void term_scroll(void) {
    kmemcpy(&term_ch[0][0], &term_ch[1][0], (term_rows - 1) * TMAX_COLS);
    kmemcpy(&term_at[0][0], &term_at[1][0], (term_rows - 1) * TMAX_COLS);
    for (int c = 0; c < term_cols; c++) {
        term_ch[term_rows - 1][c] = ' ';
        term_at[term_rows - 1][c] = 0;
    }
    term_redraw();
}

static void term_putc(char c, int attr) {
    if (c == '\n') { term_c = 0; term_r++; if (term_r >= term_rows) { term_r = term_rows - 1; term_scroll(); } return; }
    if (c == '\r') { term_c = 0; return; }
    if (c == '\b') {
        if (term_c > 0) {
            term_c--;
            term_ch[term_r][term_c] = ' ';
            term_at[term_r][term_c] = 0;
            term_cell(term_r, term_c);
        }
        return;
    }
    if (c == '\t') { c = ' '; }
    if (c < ' ' || c > 126) return;
    if (term_c >= term_cols) { term_c = 0; term_r++; if (term_r >= term_rows) { term_r = term_rows - 1; term_scroll(); } }
    term_ch[term_r][term_c] = (unsigned char)c;
    term_at[term_r][term_c] = (unsigned char)attr;
    term_cell(term_r, term_c);
    term_c++;
}

static void term_attrs(const char *s, int attr) { while (*s) term_putc(*s++, attr); }
static void term_puts(const char *s) { term_attrs(s, 0); }

// ---- 中文图形控制台后端 (实现在 cterm.h, 文件后部定义; 此处前向声明) ----
static void ct_putc(char c, int attr);
static void ct_attrs(const char *s, int attr);

static void gprintfa(int attr, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { ct_putc(*fmt++, attr); continue; }
        fmt++;
        switch (*fmt) {
            case 'd': {
                int v = __builtin_va_arg(ap, int);
                char b[12]; int n = 0;
                if (v < 0) { ct_putc('-', attr); v = -v; }
                if (v == 0) b[n++] = '0';
                while (v > 0) { b[n++] = (char)('0' + v % 10); v /= 10; }
                while (n > 0) ct_putc(b[--n], attr);
                break;
            }
            case 'u': {
                unsigned int v = __builtin_va_arg(ap, unsigned int);
                char b[12]; int n = 0;
                if (v == 0) b[n++] = '0';
                while (v > 0) { b[n++] = (char)('0' + v % 10); v /= 10; }
                while (n > 0) ct_putc(b[--n], attr);
                break;
            }
            case 'x': case 'X': {
                unsigned int v = __builtin_va_arg(ap, unsigned int);
                char b[12]; int n = 0;
                if (v == 0) b[n++] = '0';
                while (v > 0) { int d = v % 16; b[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10); v /= 16; }
                while (n > 0) ct_putc(b[--n], attr);
                break;
            }
            case 's': {
                char *s = __builtin_va_arg(ap, char*);
                ct_attrs(s ? s : "(null)", attr);
                break;
            }
            case 'c': ct_putc((char)__builtin_va_arg(ap, int), attr); break;
            case 'l': {
                fmt++;
                if (*fmt == 'u' || *fmt == 'd') {
                    unsigned long v = __builtin_va_arg(ap, unsigned long);
                    char b[16]; int n = 0;
                    if (v == 0) b[n++] = '0';
                    while (v > 0) { b[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
                    while (n > 0) ct_putc(b[--n], attr);
                } else if (*fmt == 'x') {
                    unsigned long v = __builtin_va_arg(ap, unsigned long);
                    char b[16]; int n = 0;
                    if (v == 0) b[n++] = '0';
                    while (v > 0) { int d = (int)(v % 16); b[n++] = (char)(d<10?'0'+d:'A'+d-10); v /= 16; }
                    while (n > 0) ct_putc(b[--n], attr);
                }
                break;
            }
            case '%': ct_putc('%', attr); break;
            default: ct_putc('?', attr); break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}
#define gprintf(...) gprintfa(0, __VA_ARGS__)

static void term_clear(void) {
    for (int r = 0; r < TMAX_ROWS; r++)
        for (int c = 0; c < TMAX_COLS; c++) { term_ch[r][c] = ' '; term_at[r][c] = 0; }
    term_r = 0; term_c = 0;
    term_redraw();
}

// ====================== PIT 轮询节拍 (无需中断) ======================
static uint16_t pit_read(void) {
    outb(0x43, 0x00);
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

static void pit_wait_wrap(uint16_t *last) {
    for (;;) {
        uint16_t c = pit_read();
        if (c > *last) { *last = c; return; }   // 向下计数器发生回绕 ~55ms
        *last = c;
    }
}

// ====================== RTC 实时时钟 ======================
static uint8_t cmos_read(uint8_t idx) { outb(0x70, idx); return inb(0x71); }
static int bcd2dec_u(uint8_t x) { return (x >> 4) * 10 + (x & 15); }

static void rtc_now(int *h, int *m, int *s) {
    uint8_t s1, m1, h1, s2, m2, h2;
    do {
        while (cmos_read(0x0A) & 0x80) {}
        s1 = cmos_read(0); m1 = cmos_read(2); h1 = cmos_read(4);
        while (cmos_read(0x0A) & 0x80) {}
        s2 = cmos_read(0); m2 = cmos_read(2); h2 = cmos_read(4);
    } while (s1 != s2 || m1 != m2 || h1 != h2);
    if (!(cmos_read(0x0B) & 4)) { s1 = (uint8_t)bcd2dec_u(s1); m1 = (uint8_t)bcd2dec_u(m1); h1 = (uint8_t)bcd2dec_u(h1); }
    *s = s1; *m = m1; *h = h1;
}

// ====================== 64 位除法 libgcc 助手 (TCC i386 libcall) ======================
// 坐标映射 (uint64 乘除) 会生成 __udivdi3 调用; 裸机无 libgcc, 自带软件实现。
static uint64_t __udivdi3(uint64_t num, uint64_t den) {
    uint64_t q = 0, bit = 1;
    if (!den) return 0;                          // 除零防御: 返回 0
    while (!(den >> 63) && den < num) { den <<= 1; bit <<= 1; }
    while (bit) {
        if (num >= den) { num -= den; q |= bit; }
        den >>= 1;
        bit >>= 1;
    }
    return q;
}

// ====================== USB (xHCI + HID 键鼠, 见 usb_xhci.h) ======================
#include "usb_xhci.h"

// ====================== I2C-HID 触摸 (LPSS I2C 全栈, 见 i2c_hid.h) ======================
// 依赖 usb_xhci.h 的 pci_read32/paging_map_mmio/usb_dma/usb_parse_digitizer
#include "i2c_hid.h"

// ====================== 硬件探测 (PCI/ACPI/汇总, 见 hwprobe.h) ======================
// 依赖 usb_xhci.h 与 i2c_hid.h 的状态全局
#include "hwprobe.h"

// ====================== 中文字库渲染 (Noto Sans SC 22px, 见 gui_cn.h) ======================
#include "gui_cn.h"

// ====================== HarmonyOS 风格 UI 外壳 (见 ui_shell.h) ======================
#include "ui_shell.h"

// ====================== UEFI 路径开机动画 (见 bootanim.h) ======================
#include "bootanim.h"

// ====================== 非阻塞键盘 ======================
static int kb_poll(void) {
    if ((inb(0x64) & 0x01) == 0) return -1;
    unsigned char sc = inb(0x60);
    static int e0 = 0;
    if (sc == 0xE0) { e0 = 1; return -1; }
    if (sc == 0xE1) { return -2; }   // 简化: 丢弃 pause 序列
    if (e0) { e0 = 0; return -1; }   // 忽略扩展键
    if (sc & 0x80) {
        unsigned char mk = sc & 0x7F;
        if (mk == 0x2A || mk == 0x36) kb_shift = 0;
        return -1;
    }
    if (sc == 0x2A || sc == 0x36) { kb_shift = 1; return -1; }
    if (sc == 0x3A) { kb_caps = !kb_caps; return -1; }
    unsigned char ascii = kb_shift ? scancode_to_ascii_shift[sc] : scancode_to_ascii[sc];
    if (kb_caps && ascii) {
        if (ascii >= 'a' && ascii <= 'z') ascii -= 32;
        else if (ascii >= 'A' && ascii <= 'Z') ascii += 32;
    }
    return (int)ascii;
}

static void term_prompt(void) {
    term_attrs("sys_sched:/$ ", 4);
}

// ====================== 调度器流水线演示 (各模块围绕调度器运转) ======================
// 每个 tick 把所有活动任务沿模块链推进一站:
// WAIT_IR(收取) -> COMPILE_ARM(编译) -> TRANS_X86(翻译) -> CACHE_STORE(缓存)
//               -> FORWARD_SCHED(转发) -> RUNNING -> FINISH
static void sched_pipeline_step(void) {
    uint64_t now = sys_get_tick();
    for (int i = 0; i < MAX_TASK; i++) {
        task_t *t = &task_pool[i];
        switch (t->state) {
            case TASK_WAIT_IR:
                t->state = TASK_COMPILE_ARM;
                t->last_run_tick = now;
                break;
            case TASK_COMPILE_ARM:
                t->state = TASK_TRANS_X86;
                t->last_run_tick = now;
                break;
            case TASK_TRANS_X86:
                t->state = TASK_CACHE_STORE;
                t->last_run_tick = now;
                break;
            case TASK_CACHE_STORE:
                t->state = TASK_FORWARD_SCHED;
                t->last_run_tick = now;
                break;
            case TASK_FORWARD_SCHED:
                g_stat.total_forward_byte += t->ir_len ? t->ir_len : 64;
                t->state = TASK_RUNNING;
                t->last_run_tick = now;
                break;
            case TASK_RUNNING:
                t->state = TASK_FINISH;
                t->last_run_tick = now;
                g_stat.total_finish++;
                break;
            case TASK_RETRY_QUEUE:
                t->state = TASK_COMPILE_ARM;
                t->last_run_tick = now;
                break;
            default: break;
        }
    }
}

static int count_crash(void) {
    int n = 0;
    for (int i = 0; i < MAX_TASK; i++)
        if (task_pool[i].state == TASK_CRASH_RECOVER || task_pool[i].state == TASK_STUCK_DETECT) n++;
    return n;
}

static int in_pipeline(task_state_t s) {
    return s == TASK_WAIT_IR || s == TASK_COMPILE_ARM || s == TASK_TRANS_X86 ||
           s == TASK_CACHE_STORE || s == TASK_FORWARD_SCHED || s == TASK_RUNNING;
}

static void demo_tick(void) {
    g_rng = g_rng * 1103515245u + 12345u;
    uint32_t r = g_rng;

    // 保持约 10 个任务在流水线中
    int flowing = 0;
    for (int i = 0; i < MAX_TASK; i++) if (in_pipeline(task_pool[i].state)) flowing++;
    if (flowing < 10) {
        int prio = 1 + (int)((r >> 8) % 4);
        uint32_t seg = 1 + (r >> 16) % 40;
        unsigned char sample[64];
        for (int i = 0; i < 64; i++) sample[i] = (unsigned char)(i + (unsigned char)g_demo_pid);
        sched_create_task(g_demo_pid++, (task_prio_t)prio, seg, sample, 64);
    }

    // 偶发故障: 重试 / 崩溃, 驱动监控窗颜色与统计变化
    uint32_t kt = (uint32_t)g_kernel_tick;
    if ((kt % 47) == 0) {
        int idx = (int)(r % MAX_TASK);
        for (int k = 0; k < MAX_TASK; k++) {
            task_t *t = &task_pool[(idx + k) % MAX_TASK];
            if (in_pipeline(t->state)) {
                t->state = TASK_RETRY_QUEUE;
                t->retry_times++;
                g_stat.total_retry++;
                break;
            }
        }
    }
    if ((kt % 89) == 0) {
        int idx = (int)((r >> 11) % MAX_TASK);
        for (int k = 0; k < MAX_TASK; k++) {
            task_t *t = &task_pool[(idx + k) % MAX_TASK];
            if (in_pipeline(t->state)) {
                t->state = TASK_CRASH_RECOVER;
                t->is_timeout = 1;
                g_stat.total_crash++;
                break;
            }
        }
    }

    sched_pipeline_step();
    sched_watchdog();

    // 崩溃槽过多时恢复模块自动回收
    if (count_crash() >= 8) {
        int n = sched_recover_crash();
        if (n > 0) gprintf("[recover] auto crash-recovery reclaimed %d slot(s)\n", n);
    }
}

// ====================== 窗口/桌面 ======================
static void draw_window(int x, int y, int w, int h, const char *title) {
    fb_fill(x, y, x + w - 1, y + h - 1, C_WINBG);
    fb_frame(x, y, w, h, C_WINFRAME);
    fb_fill(x + 1, y + 1, x + w - 2, y + 23, C_TITLEBAR);
    fb_str(x + 10, y + 4, title, C_TITLETX, C_TITLEBAR);
    // 右上角状态点
    fb_fill(x + w - 18, y + 8, x + w - 11, y + 15, C_RED);
    fb_fill(x + w - 32, y + 8, x + w - 25, y + 15, C_YELLOW);
    fb_fill(x + w - 46, y + 8, x + w - 39, y + 15, C_GREEN);
}

static void draw_desktop(void) {
    fb_fill(0, 0, (int)g_fb_w - 1, (int)g_fb_h - 1, C_DESKTOP);
    // 顶部任务栏
    fb_fill(0, 0, (int)g_fb_w - 1, 30, C_TASKBAR);
    fb_hline(0, (int)g_fb_w - 1, 30, C_ACCENT);
    fb_hline(0, (int)g_fb_w - 1, 31, 0x1B3A57u);
    fb_str(12, 8, "IN OS  //  SUPER SCHEDULER GRAPHICAL CONSOLE", 0xDCEBFAu, C_TASKBAR);
    // 两个主窗口
    draw_window(mon_x, mon_y, mon_w, mon_h,
                "SUPER SCHEDULER MONITOR  -  task pool / pipeline / watchdog");
    draw_window(term_x, term_y, term_w, term_h,
                "TERMINAL  -  keyboard commands drive the super scheduler");
}

// ====================== 监控窗刷新 ======================
static const char * const legend_name[] =
    {"WAIT", "COMP", "X86", "CACHE", "FWD", "RUN", "DONE", "CRASH", "STUCK", "RETRY"};
static const uint32_t legend_col[] =
    {C_YELLOW, C_CYAN, C_CYAN2, C_BLUE, C_PURPLE, C_GREEN, C_GRAY, C_RED, C_ORANGE, C_PINK};

static void render_monitor(void) {
    int cx = mon_x + 12;
    int top = mon_y + 24 + 6;

    // ---- 统计区 (左右两列) ----
    static const char *labl[] =
        {"TASK CREATED", "TASK FINISHED", "CRASH RECOVER", "RETRY QUEUED"};
    static const char *labr[] =
        {"STUCK DETECT", "FORWARD KB", "SNAPSHOT KB", "ACTIVE POOL"};
    uint32_t vl[] = {
        (uint32_t)g_stat.total_create, (uint32_t)g_stat.total_finish,
        (uint32_t)g_stat.total_crash,  (uint32_t)g_stat.total_retry };
    uint32_t vr[] = {
        (uint32_t)g_stat.total_stuck, (uint32_t)g_stat.total_forward_byte / 1024,
        (uint32_t)g_stat.total_snap_byte / 1024, (uint32_t)sched_count_active() };
    int rx = mon_x + mon_w / 2;
    for (int i = 0; i < 4; i++) {
        int y = top + i * 18;
        fb_str(cx, y, labl[i], C_DIM, C_WINBG);
        fb_u32(cx + 118, y, vl[i], C_TEXT, C_WINBG);
        fb_str(rx, y, labr[i], C_DIM, C_WINBG);
        int e = rx + 118 + fb_u32(rx + 118, y, vr[i], C_TEXT, C_WINBG);
        if (i == 3) fb_str(e, y, "/64", C_DIM, C_WINBG);
    }

    // ---- 流水线图例 ----
    int leg_y = top + 4 * 18 + 2;
    int lx = cx;
    for (int i = 0; i < 10; i++) {
        if (lx + 58 > mon_x + mon_w - 12) break;
        fb_fill(lx, leg_y + 4, lx + 7, leg_y + 11, legend_col[i]);
        fb_str(lx + 12, leg_y, legend_name[i], C_TEXT, C_WINBG);
        lx += 58;
    }

    // ---- 64 槽热力图 ----
    int map_y = leg_y + 22;
    fb_str(cx, map_y, "POOL MAP", C_DIM, C_WINBG);
    int map_x = cx + 76;
    int cell = (mon_w - 24 - 76) / MAX_TASK;
    if (cell > 14) cell = 14;
    if (cell < 5) cell = 5;
    for (int i = 0; i < MAX_TASK; i++) {
        int x = map_x + i * cell;
        fb_fill(x, map_y, x + cell - 2, map_y + 13, state_color(task_pool[i].state));
    }

    // ---- 表头 + 状态行 ----
    int hdr_y = map_y + 22;
    fb_hline(cx, mon_x + mon_w - 12, hdr_y - 4, 0x263445u);
    fb_str(cx,        hdr_y, "SLOT", C_DIM, C_WINBG);
    fb_str(cx + 56,   hdr_y, "PID",  C_DIM, C_WINBG);
    fb_str(cx + 112,  hdr_y, "STATE", C_DIM, C_WINBG);
    fb_str(cx + 210,  hdr_y, "PRIO", C_DIM, C_WINBG);
    fb_str(cx + 292,  hdr_y, "SEG",  C_DIM, C_WINBG);
    fb_str(cx + 356,  hdr_y, "RETRY", C_DIM, C_WINBG);
    fb_str(cx + 430,  hdr_y, "FLAGS", C_DIM, C_WINBG);
    // 右侧引擎状态 (先擦除固定宽度, 避免 ON/OFF 长短不一时留影)
    fb_fill(mon_x + mon_w - 126, hdr_y - 2, mon_x + mon_w - 12, hdr_y + 15, C_WINBG);
    fb_str(mon_x + mon_w - 170, hdr_y, "AUTO:", C_DIM, C_WINBG);
    fb_str(mon_x + mon_w - 122, hdr_y, g_auto ? "ON " : "OFF", g_auto ? C_GREEN : C_GRAY, C_WINBG);
    fb_str(mon_x + mon_w - 84, hdr_y, "WDOG:ARM", C_GREEN, C_WINBG);

    int list_y0 = hdr_y + 20;
    int list_rows = (mon_y + mon_h - 8 - list_y0) / 16;
    int shown = 0;
    for (int i = 0; i < MAX_TASK && shown < list_rows; i++) {
        task_t *t = &task_pool[i];
        if (t->state == TASK_IDLE) continue;
        int y = list_y0 + shown * 16;
        shown++;
        // 先整行擦底, 消除短文本覆盖长文本残影
        fb_fill(cx, y, mon_x + mon_w - 12, y + 15, C_WINBG);
        fb_u32(cx, y, (uint32_t)i, C_TEXT, C_WINBG);
        fb_u32(cx + 56, y, t->pid, C_TEXT, C_WINBG);
        fb_str(cx + 112, y, state_name(t->state), state_color(t->state), C_WINBG);
        fb_str(cx + 210, y, prio_name(t->priority), C_TEXT, C_WINBG);
        fb_u32(cx + 292, y, t->seg_id, C_TEXT, C_WINBG);
        fb_u32(cx + 356, y, (uint32_t)t->retry_times, C_TEXT, C_WINBG);
        int fx = cx + 430;
        if (t->is_timeout) { fb_str(fx, y, "T", C_RED, C_WINBG); fx += 12; }
        if (t->is_stuck)   { fb_str(fx, y, "S", C_ORANGE, C_WINBG); fx += 12; }
        if (!t->is_timeout && !t->is_stuck) fb_str(fx, y, "-", C_DIM, C_WINBG);
    }
    // 擦除多余行区域 (任务数减少时不留影)
    int y = list_y0 + shown * 16;
    int yend = list_y0 + list_rows * 16 - 1;
    if (yend >= y) fb_fill(cx, y, mon_x + mon_w - 12, yend, C_WINBG);
}

// ====================== 任务栏状态 (tick + RTC) ======================
static void render_status(void) {
    int hh, mm, ss;
    rtc_now(&hh, &mm, &ss);
    char b[48];
    int n = 0;
    const char *p = "TICK ";
    while (*p) b[n++] = *p++;
    n += u32_str((uint32_t)g_kernel_tick, b + n);
    b[n++] = ' '; b[n++] = ' ';
    if (hh < 10) b[n++] = '0'; n += u32_str((uint32_t)hh, b + n); b[n++] = ':';
    if (mm < 10) b[n++] = '0'; n += u32_str((uint32_t)mm, b + n); b[n++] = ':';
    if (ss < 10) b[n++] = '0'; n += u32_str((uint32_t)ss, b + n);
    b[n] = 0;
    int w = n * 8;
    fb_fill((int)g_fb_w - w - 24, 4, (int)g_fb_w - 12, 24, C_TASKBAR);
    fb_str((int)g_fb_w - w - 12, 7, b, 0xDCEBFAu, C_TASKBAR);
}

// ====================== 中文图形控制台 (拼音输入/权限层/中文命令) ======================
#include "cterm.h"

// ====================== GUI 终端命令 ======================
static void gui_help(void) {
    gprintf("commands (all drive the super scheduler):\n", 0);
    gprintf("  help                         show this help\n", 0);
    gprintf("  auto / stop                  auto demo pipeline on/off\n", 0);
    gprintf("  create <pid> <seg> <prio>    create task (prio 1-4)\n", 0);
    gprintf("  runall                       run queued tasks by priority\n", 0);
    gprintf("  step                         advance pipeline one tick\n", 0);
    gprintf("  snap <pid> <vaddr> <len>     memory snapshot bytes\n", 0);
    gprintf("  recover                      reclaim crashed/stuck tasks\n", 0);
    gprintf("  stat                         scheduler statistics\n", 0);
    gprintf("  usb                          USB HID keyboard/mouse status\n", 0);
    gprintf("  tasks                        list task pool\n", 0);
    gprintf("  clear                        clear terminal\n", 0);
    gprintf("  about                        system info\n", 0);
    gprintf("  reboot / exit                reset / halt\n", 0);
}

static void gui_about(void) {
    gprintf("IN OS (c) 2026 - 32-bit PM, VBE LFB %ux%u@%u\n",
            g_fb_w, g_fb_h, (uint32_t)g_vbe->bpp);
    gprintf("LFB=0x%x pitch=%u mode=0x%x  framebuffer kernel GUI\n",
            g_vbe->fb, g_pitch, (uint32_t)g_vbe->mode);
    gprintf("modules centered on super_scheduler: receive/compile/translate/\n", 0);
    gprintf("cache/forward/run/recover, pool=64, watchdog=on.\n", 0);
}

static void gui_stat(void) {
    gprintf("created=%lu finished=%lu crash=%lu retry=%lu stuck=%lu\n",
            (unsigned long)g_stat.total_create, (unsigned long)g_stat.total_finish,
            (unsigned long)g_stat.total_crash, (unsigned long)g_stat.total_retry,
            (unsigned long)g_stat.total_stuck);
    gprintf("forward=%lu bytes snapshot=%lu bytes active=%d/%d tick=%lu\n",
            (unsigned long)g_stat.total_forward_byte,
            (unsigned long)g_stat.total_snap_byte,
            sched_count_active(), MAX_TASK, (unsigned long)g_kernel_tick);
}

static void gui_tasks(void) {
    int shown = 0;
    for (int i = 0; i < MAX_TASK; i++) {
        task_t *t = &task_pool[i];
        if (t->state == TASK_IDLE) continue;
        shown++;
        gprintf("[%2d] pid=%-5u %-8s prio=%-6s seg=%-4u retry=%u%s%s\n",
                i, t->pid, state_name(t->state), prio_name(t->priority),
                t->seg_id, (uint32_t)t->retry_times,
                t->is_timeout ? " T" : "", t->is_stuck ? " S" : "");
    }
    if (!shown) gprintf("(no active tasks)\n", 0);
}

static void gui_exec(char *args[], int argc) {
    if (argc == 0) return;
    const char *cmd = args[0];

    if (kstrcmp(cmd, "help") == 0 || kstrcmp(cmd, "?") == 0) { gui_help(); return; }
    if (kstrcmp(cmd, "about") == 0) { gui_about(); return; }
    if (kstrcmp(cmd, "auto") == 0) { g_auto = 1; gprintfa(2, "[ok] auto demo ON\n"); return; }
    if (kstrcmp(cmd, "stop") == 0) { g_auto = 0; gprintfa(2, "[ok] auto demo OFF\n"); return; }
    if (kstrcmp(cmd, "step") == 0) {
        sched_pipeline_step();
        gprintf("[ok] pipeline stepped, active=%d\n", sched_count_active());
        return;
    }
    if (kstrcmp(cmd, "clear") == 0 || kstrcmp(cmd, "cls") == 0) { term_clear(); return; }
    if (kstrcmp(cmd, "init") == 0) { sched_init(); gprintfa(2, "[ok] scheduler initialized\n"); return; }
    if (kstrcmp(cmd, "create") == 0 || kstrcmp(cmd, "create_task") == 0) {
        if (argc < 4) { gprintfa(3, "usage: create <pid> <seg_id> <prio 1-4>\n"); return; }
        uint32_t pid = (uint32_t)katoi(args[1]);
        uint32_t seg = (uint32_t)katoi(args[2]);
        int p = katoi(args[3]);
        if (p < 1) p = 1; if (p > 4) p = 4;
        unsigned char sample[64];
        for (int i = 0; i < 64; i++) sample[i] = (unsigned char)i;
        int tid = sched_create_task(pid, (task_prio_t)p, seg, sample, 64);
        if (tid >= 0)
            gprintf("[ok] slot=%d pid=%u seg=%u prio=%s\n", tid, pid, seg, prio_name((task_prio_t)p));
        else
            gprintfa(3, "[err] task pool full; try 'recover'\n");
        return;
    }
    if (kstrcmp(cmd, "runall") == 0 || kstrcmp(cmd, "run_all") == 0) {
        int ok = sched_run_all();
        gprintf("[ok] %u task(s) dispatched to completion\n", ok);
        return;
    }
    if (kstrcmp(cmd, "snap") == 0) {
        if (argc < 4) { gprintfa(3, "usage: snap <pid> <vaddr(hex)> <len>\n"); return; }
        uint32_t pid = (uint32_t)katoi(args[1]);
        uint32_t vaddr = (uint32_t)kstrtoul(args[2], 16);
        uint32_t len = (uint32_t)katoi(args[3]);
        int got = sched_snapshot(pid, vaddr, len);
        gprintf("[ok] snapshot pid=%u vaddr=0x%x bytes=%d\n", pid, vaddr, got);
        return;
    }
    if (kstrcmp(cmd, "recover") == 0) {
        int n = sched_recover_crash();
        gprintf("[ok] reclaimed %d crashed/stuck slot(s)\n", n);
        return;
    }
    if (kstrcmp(cmd, "stat") == 0) { gui_stat(); return; }
    if (kstrcmp(cmd, "usb") == 0) { usb_status(); return; }
    if (kstrcmp(cmd, "tasks") == 0 || kstrcmp(cmd, "ps") == 0) { gui_tasks(); return; }
    if (kstrcmp(cmd, "watchdog") == 0) {
        int stuck = sched_watchdog();
        gprintf("[ok] watchdog scan: %d stuck\n", stuck);
        return;
    }
    if (kstrcmp(cmd, "echo") == 0) {
        for (int i = 1; i < argc; i++) { term_puts(args[i]); if (i < argc - 1) term_putc(' ', 0); }
        term_putc('\n', 0);
        return;
    }
    if (kstrcmp(cmd, "exit") == 0 || kstrcmp(cmd, "halt") == 0) {
        gprintfa(2, "[ok] halting CPU.\n");
        __asm__ __volatile__("cli");
        for (;;) __asm__ __volatile__("hlt");
    }
    if (kstrcmp(cmd, "reboot") == 0) {
        gprintfa(2, "[ok] rebooting...\n");
        __asm__ __volatile__("cli\n" "lidt 0\n" "int $0x03");
        return;
    }
    gprintfa(3, "[err] unknown command: %s  (try 'help')\n", cmd);
}

// ====================== 键盘 -> 终端行 ======================
static void gui_kb_drain(void) {
    int guard = 64;
    while (guard-- > 0) {
        int c;
        if (g_ps2_present) {
            c = kb_poll();
            if (c == -2) { kb_poll(); kb_poll(); kb_poll(); kb_poll(); kb_poll(); continue; }
        } else {
            c = -1;                          // 真机无 i8042: 0x64 读 0xFF, 跳过 PS/2
        }
        if (c < 0) {
            usb_poll();                      // USB HID 键盘/鼠标轮询
            c = usb_kb_getc();
        }
        if (c < 0) break;
        if (c == 0) continue;
        if (c == 0x0A) {
            term_putc('\n', 0);
            cli_buf[in_len] = 0;
            char *args[16];
            int argc = cli_tokenize(cli_buf, args, 16);
            gui_exec(args, argc);
            in_len = 0;
            term_prompt();
            render_monitor();
            continue;
        }
        if (c == 0x08) {
            if (in_len > 0) { in_len--; term_putc('\b', 0); }
            continue;
        }
        if (c == 0x1B) {
            while (in_len > 0) { in_len--; term_putc('\b', 0); }
            continue;
        }
        if (c >= ' ' && c < 127 && in_len < CLI_BUF_SIZE - 1) {
            cli_buf[in_len++] = (char)c;
            term_putc((char)c, 0);
        }
    }
}

// ====================== GUI 主入口 ======================
static void gui_main(void) {
    g_fb = (volatile uint8_t*)(uint32_t)g_vbe->fb;
    g_fb_w = g_vbe->width;
    g_fb_h = g_vbe->height;
    g_pitch = g_vbe->pitch ? g_vbe->pitch : g_fb_w * 4;
    g_bppb = (g_pitch >= g_fb_w * 4) ? 4 : 3;   // 24bpp packed 或 32bpp XRGB
    g_swaprb = (g_vbe->red_pos != 16);          // RedFieldPosition!=16 表示内存 RGB 序

    // 自适应布局
    mon_x = 8; mon_y = 38; mon_w = (int)g_fb_w - 16;
    term_rows = g_fb_h >= 768 ? 12 : (g_fb_h >= 600 ? 8 : 6);
    term_h = term_rows * 16 + 24 + 16;
    term_x = 8;
    term_y = (int)g_fb_h - term_h - 8;
    term_w = (int)g_fb_w - 16;
    mon_h = term_y - 8 - mon_y;
    term_cols = (term_w - 20) / 8;
    if (term_cols > TMAX_COLS) term_cols = TMAX_COLS;

    sched_init();

    // ---- 默认进入中文图形控制台 (不再自动开 UI; 命令 "启动UI" 进入图形界面) ----
    bootanim_play();          // UEFI 加载的 FRAMES.BIN 开机动画 (无则立即返回)
    ct_init();                 // 全屏深色背景 + gprintf 后端就位 (必须在硬件探测之前)
    ct_banner();
    usb_input_init();          // xHCI + USB HID 键鼠 (结果直接打印到中文控制台)
    hw_acpi();                 // 先解析 ACPI DSDT/SSDT, 为 I2C 提供 WCOM485B 从地址
    i2c_hid_init();            // LPSS I2C + I2C-HID 触摸 (QEMU 无 LPSS: 立即退出)
    gprintf("[动画] 诊断 阶段=%u 魔数=%x 地址=%x 长度=%u 帧=%u\n",
            ba_diag_stage, ba_diag_magic, ba_diag_phys, ba_diag_len, ba_diag_n);
    hw_probe();                // PCI/ACPI 全扫 + 状态汇总 (hw_acpi 已幂等)
    gprintf("\n");
    gprintfa(2, "就绪。输入「帮助」查看全部命令, 或输入「启动UI」进入图形界面。\n");
    gprintfa(1, "不会打中文可直接输入拼音/英文 (如 bangzhu、ui、su), 或拼音后按空格选字。\n\n");
    ct_prompt();
    ct_run();                  // 中文控制台主循环
}
