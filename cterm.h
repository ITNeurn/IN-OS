// cterm.h - 中文图形控制台 (LFB 全屏 + fcn16 UTF-8) + 拼音输入法 + 权限层 + 中文命令
// 依赖: gui_cn.h(fcn16_text/utf8_dec), fb_fill/g_fb*/fb_map, gprintf,
//       超级调度器 sched_*, ui_enter(ui_shell.h), rtc_now, kb_poll/usb_kb_getc,
//       i2c_hid.h/hwprobe.h/usb_xhci.h 状态变量
#ifndef CTERM_H
#define CTERM_H

// ====================== 布局 ======================
#define CT_MARGIN   12
#define CT_ROWH     24                 // 16px 字 + 行距 (fcn16 asc=19)
#define CT_MAXROWS  96
#define CT_LINEMAX  420

static int  ct_rows;
static char ct_buf[CT_MAXROWS][CT_LINEMAX];
static unsigned char ct_at[CT_MAXROWS][CT_LINEMAX];
static int  ct_len[CT_MAXROWS];
static int  ct_r;
static int  ct_x;
static uint32_t ct_bg;

// ====================== 权限层 ======================
// 0 = 用户层 (默认, 只读/安全命令); 1 = 最高权限 (可调度/快照/关机重启)
static int g_priv = 0;

// ====================== 输入行 + 拼音输入法 ======================
static char ct_in[CT_LINEMAX];

// 右侧实时任务面板 (任务管理器分窗)
static int ct_panel_en = 1;
static int ct_panel_x;                 // 面板左边界(物理px), ct_init 设定
#define CT_PANEL_W 680
static int  ct_panel_last = -1;        // 上次面板数据签名 (-1 = 未绘)
static int  ct_panel_dirty = 0;        // 布局脏标志 (滚动/清屏后置位, 统一补绘)
static int  ct_inlen = 0;
static int  ct_inrow = 0;

typedef struct { const char *py; const char *hz; } ime_ent_t;
// 命令词拼音词库 (空格/Tab 弹候选, 数字选字上屏); 回车也可直接敲拼音执行
static const ime_ent_t ime_dict[] = {
    {"bangzhu",    "帮助"},
    {"guanyu",     "关于"},
    {"qingping",   "清屏"},
    {"tongji",     "统计"},
    {"renwu",      "任务"},
    {"yingjian",   "硬件"},
    {"qidong",     "启动UI"},
    {"qidongui",   "启动UI"},
    {"tiquan",     "提权"},
    {"jiangquan",  "降权"},
    {"chuangjian", "创建"},
    {"yunxing",    "运行"},
    {"danbu",      "单步"},
    {"kuaizhao",   "快照"},
    {"huifu",      "恢复"},
    {"kanmengou",  "看门狗"},
    {"kanmen",     "看门狗"},
    {"zidong",     "自动"},
    {"tingzhi",    "停止"},
    {"chushihua",  "初始化"},
    {"chongqi",    "重启"},
    {"guanji",     "关机"},
    {"tuichu",     "退出"},
};
#define IME_N (int)(sizeof(ime_dict)/sizeof(ime_dict[0]))
#define IME_CAND_MAX 9
static int  ime_active = 0;
static int  ime_segstart = 0;
static int  ime_cand_n = 0;
static int  ime_cand_idx[IME_CAND_MAX];

// ====================== 颜色 ======================
static uint32_t ct_col(int a) {
    switch (a) {
        case 1: return C_DIM;
        case 2: return C_GREEN;
        case 3: return C_RED;
        case 4: return C_CYAN;
        case 5: return C_YELLOW;
        default: return C_TEXT;
    }
}

static int ct_cp_adv(unsigned cp) { return cp < 0x80 ? 8 : 16; }
static int ct_enc(unsigned cp, char t[5]) {
    if (cp < 0x80) { t[0] = (char)cp; t[1] = 0; return 1; }
    if (cp < 0x800) { t[0]=(char)(0xC0|(cp>>6)); t[1]=(char)(0x80|(cp&0x3F)); t[2]=0; return 2; }
    t[0]=(char)(0xE0|(cp>>12)); t[1]=(char)(0x80|((cp>>6)&0x3F));
    t[2]=(char)(0x80|(cp&0x3F)); t[3]=0; return 3;
}

// 在 (x,y) 渲染缓冲一段, 返回结束像素 x
static int ct_draw_seg(int x, int y, const char *s, int n, uint32_t col) {
    int i = 0;
    while (i < n) {
        unsigned cp;
        int u = utf8_dec((const unsigned char*)s + i, &cp);
        if (i + u > n) u = n - i;
        char tmp[5]; int en = ct_enc(cp, tmp);
        (void)en;
        fcn16_text(x, y, tmp, col);
        x += ct_cp_adv(cp);
        i += u;
    }
    return x;
}

static int ct_row_y(int r) { return CT_MARGIN + r * CT_ROWH + 2; }

// 控制台文字有效右边界 (面板开启时让出右侧)
static int ct_text_lim(void) {
    return ct_panel_en ? ct_panel_x - 8 : (int)g_fb_w - CT_MARGIN;
}

static void ct_panel(void);        // 右侧面板 (定义在命令区, 依赖 cn_state/cn_prio)
static void ct_panel_sync(void);   // 签名门控重绘: 数据没变不重画 (防闪烁)

// 重绘一整行 (行缓冲), 返回结束 x
static int ct_render_row(int r) {
    int y = ct_row_y(r);
    fb_fill(0, y - 2, ct_panel_en ? ct_panel_x - 9 : (int)g_fb_w - 1, y + CT_ROWH - 4, ct_bg);
    int x = CT_MARGIN;
    int i = 0, L = ct_len[r];
    int lim = ct_text_lim();
    while (i < L) {
        unsigned cp;
        int u = utf8_dec((const unsigned char*)ct_buf[r] + i, &cp);
        if (i + u > L) u = L - i;
        char tmp[5]; ct_enc(cp, tmp);
        fcn16_text(x, y, tmp, ct_col(ct_at[r][i]));
        x += ct_cp_adv(cp);
        i += u;
        if (x > lim) break;
    }
    return x;
}

static void ct_scroll_up(void) {
    for (int r = 0; r < ct_rows - 1; r++) {
        kmemcpy(ct_buf[r], ct_buf[r + 1], CT_LINEMAX);
        kmemcpy(ct_at[r],  ct_at[r + 1],  CT_LINEMAX);
        ct_len[r] = ct_len[r + 1];
    }
    ct_len[ct_rows - 1] = 0;
    ct_r = ct_rows - 1;
    // 帧缓冲像素整体上移一行
    int top = CT_MARGIN, h = ct_rows * CT_ROWH;
    volatile uint8_t *dst = g_fb + (uint32_t)top * g_pitch;
    volatile uint8_t *src = g_fb + (uint32_t)(top + CT_ROWH) * g_pitch;
    int nbytes = (int)g_fb_w * g_bppb;
    for (int y = 0; y < h - CT_ROWH; y++)
        kmemcpy((void*)(dst + (uint32_t)y * g_pitch),
                (void*)(src + (uint32_t)y * g_pitch), nbytes);
    int ly = ct_row_y(ct_rows - 1);
    fb_fill(0, ly - 2, ct_panel_en ? ct_panel_x - 9 : (int)g_fb_w - 1, ly + CT_ROWH - 4, ct_bg);
    ct_panel_dirty = 1;                // 像素整体上移覆盖了面板, 标脏统一补绘
}

static void ct_newline(void) {
    if (ct_r >= ct_rows - 1) ct_scroll_up();
    else { ct_r++; ct_len[ct_r] = 0; }
    ct_x = CT_MARGIN;
}

// ====================== gprintf 后端 ======================
static void ct_putc(char c, int attr) {
    if (c == '\r') return;
    if (c == '\n') { ct_newline(); return; }
    if (c == '\t') c = ' ';
    unsigned char uc = (unsigned char)c;
    int w;
    if (uc < 0x80) w = 8;                 // ASCII 半角
    else if ((uc & 0xC0) == 0x80) w = 0; // UTF-8 续字节不占宽
    else w = 16;                         // UTF-8 多字节首字符 = 全角
    if (w && ct_x + w > ct_text_lim()) ct_newline();
    int L = ct_len[ct_r];
    if (L < CT_LINEMAX - 1) {
        ct_buf[ct_r][L] = c;
        ct_at[ct_r][L] = (unsigned char)attr;
        ct_len[ct_r] = L + 1;
        if (w) ct_x += w;
    }
    ct_render_row(ct_r);
}
static void ct_attrs(const char *s, int attr) { while (*s) ct_putc(*s++, attr); }

static void ct_clear(void) {
    fb_fill(0, 0, (int)g_fb_w - 1, (int)g_fb_h - 1, ct_bg);
    for (int r = 0; r < ct_rows; r++) ct_len[r] = 0;
    ct_r = 0; ct_x = CT_MARGIN;
    ct_panel_dirty = 1; ct_panel_sync();
}

static void ct_full_redraw(void) {
    fb_fill(0, 0, (int)g_fb_w - 1, (int)g_fb_h - 1, ct_bg);
    for (int r = 0; r <= ct_r && r < ct_rows; r++) ct_render_row(r);
    ct_panel_dirty = 1; ct_panel_sync();
}

// ====================== 输入行回显 ======================
static void ct_draw_cursor(int x) {
    int y = ct_row_y(ct_inrow);
    fb_fill(x, y, x + 1, y + 18, C_GREEN);
}

static void ct_render_input(void) {
    int x = ct_render_row(ct_inrow);           // 行缓冲 (提示符)
    x = ct_draw_seg(x, ct_row_y(ct_inrow), ct_in, ct_inlen, C_TEXT);  // 已输入内容
    ct_x = x;
    if (ime_active) {                          // 拼音候选条 (临时, 不落缓冲)
        int y = ct_row_y(ct_inrow);
        int ox = x + 12;
        ox = ct_draw_seg(ox, y, "  ", 2, C_YELLOW);
        for (int k = 0; k < ime_cand_n; k++) {
            char num[2] = { (char)('1' + k), 0 };
            ox = ct_draw_seg(ox, y, num, 1, C_YELLOW);
            ox = ct_draw_seg(ox, y, ".", 1, C_YELLOW);
            const char *hz = ime_dict[ime_cand_idx[k]].hz;
            int hn = 0; while (hz[hn]) hn++;
            ox = ct_draw_seg(ox, y, hz, hn, C_YELLOW);
            ox = ct_draw_seg(ox, y, " ", 1, C_YELLOW);
            if (ox > ct_text_lim() - 120) break;
        }
    }
    ct_draw_cursor(x);
}

static void ct_prompt(void) {
    if (g_priv) { ct_attrs("[管理员] ", 5); ct_attrs("INOS:# ", 4); }
    else        { ct_attrs("[用户] ", 4);   ct_attrs("INOS:$ ", 0); }
    ct_inrow = ct_r;
    ct_inlen = 0;
    ime_active = 0;
    ct_render_input();
}

// ====================== 中文命令 ======================
static int cmd_is(const char *s, const char *cn, const char *const *al) {
    if (kstrcmp(s, cn) == 0) return 1;
    for (; *al; al++) if (kstrcmp(s, *al) == 0) return 1;
    return 0;
}
static const char *const AL_HELP[]   = {"help","?","bangzhu",0};
static const char *const AL_ABOUT[]  = {"about","guanyu",0};
static const char *const AL_CLEAR[]  = {"clear","cls","qingping",0};
static const char *const AL_STAT[]   = {"stat","tongji",0};
static const char *const AL_TASKS[]  = {"tasks","ps","renwu",0};
static const char *const AL_HW[]     = {"hardware","usb","yingjian",0};
static const char *const AL_UI[]     = {"ui","qidong","qidongui",0};
static const char *const AL_SU[]     = {"su","tiquan",0};
static const char *const AL_EXIT[]   = {"logout","exit","jiangquan",0};
static const char *const AL_CREATE[] = {"create","chuangjian",0};
static const char *const AL_RUN[]    = {"runall","run","yunxing",0};
static const char *const AL_STEP[]   = {"step","danbu",0};
static const char *const AL_SNAP[]   = {"snap","kuaizhao",0};
static const char *const AL_RECV[]   = {"recover","huifu",0};
static const char *const AL_WDOG[]   = {"watchdog","kanmengou","kanmen",0};
static const char *const AL_AUTO[]   = {"auto","zidong",0};
static const char *const AL_STOP[]   = {"stop","tingzhi",0};
static const char *const AL_INIT[]   = {"init","chushihua",0};
static const char *const AL_REBOOT[] = {"reboot","chongqi",0};
static const char *const AL_HALT[]   = {"halt","poweroff","guanji",0};
static const char *const AL_PANEL[]  = {"panel","mianban",0};

static const char *cn_state(int s) {
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
static const char *cn_prio(int p) {
    switch (p) {
        case PRIORITY_LOW:       return "低";
        case PRIORITY_NORMAL:    return "普通";
        case PRIORITY_HIGH:      return "高";
        case PRIORITY_REAL_TIME: return "实时";
        default:                 return "空闲";
    }
}

// ====================== 右侧实时任务面板 ======================
static int ct_utoa(unsigned v, char *b) {
    char t[12]; int n = 0, m = 0;
    if (!v) { b[0] = '0'; b[1] = 0; return 1; }
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) b[m++] = t[--n];
    b[m] = 0; return m;
}
static int ct_pseg(int x, int y, const char *s, uint32_t c) {
    int n = 0; while (s[n]) n++;
    return ct_draw_seg(x, y, s, n, c);
}
static int ct_pnum(int x, int y, unsigned v, uint32_t c) {
    char b[12]; ct_utoa(v, b); return ct_pseg(x, y, b, c);
}
static int ct_pfmt(int x, int y, const char *pre, unsigned v, const char *post, uint32_t c) {
    char b[72]; int n = 0;
    while (*pre) b[n++] = *pre++;
    n += ct_utoa(v, b + n);
    while (*post) b[n++] = *post++;
    b[n] = 0;
    return ct_draw_seg(x, y, b, n, c);
}
static void ct_panel(void) {
    int px0 = ct_panel_x, gw = (int)g_fb_w, gh = (int)g_fb_h;
    int x = px0 + 16, y = 12;
    fb_fill(px0 + 2, 0, gw - 1, gh - 1, 0x18223A);       // 深蓝面板底
    fb_fill(px0, 0, px0 + 1, gh - 1, 0x2C3E5C);          // 左侧亮边条
    ct_pseg(x, y, "任务管理器", C_CYAN);
    y += 34;
    int ax = ct_pfmt(x, y, "活动 ", (unsigned)sched_count_active(), "", C_GREEN);
    ct_pfmt(ax + 10, y, "/", (unsigned)MAX_TASK, "", C_DIM);
    ct_pfmt(x + 240, y, "创建 ", (unsigned)g_stat.total_create, "", C_TEXT);
    y += 26;
    ct_pfmt(x, y, "完成 ", (unsigned)g_stat.total_finish, "", C_TEXT);
    ct_pfmt(x + 240, y, "崩溃 ", (unsigned)g_stat.total_crash, "", C_RED);
    y += 24;
    ct_pfmt(x, y, "重试 ", (unsigned)g_stat.total_retry, "", C_YELLOW);
    ct_pfmt(x + 240, y, "卡死 ", (unsigned)g_stat.total_stuck, "", C_RED);
    y += 28;
    fb_fill(px0 + 14, y, gw - 15, y + 1, 0x2C3E5C);      // 分隔线
    y += 10;
    ct_pseg(x, y, "槽位", C_DIM);
    ct_pseg(px0 + 120, y, "进程", C_DIM);
    ct_pseg(px0 + 220, y, "状态", C_DIM);
    ct_pseg(px0 + 360, y, "优先级", C_DIM);
    ct_pseg(px0 + 500, y, "段号", C_DIM);
    ct_pseg(px0 + 600, y, "重试", C_DIM);
    y += 26;
    int maxy = gh - 40;
    for (int i = 0; i < MAX_TASK && y <= maxy; i++) {
        task_t *t = &task_pool[i];
        if (t->state == TASK_IDLE) continue;
        uint32_t cc = C_TEXT;
        if (t->state == TASK_RUNNING) cc = C_GREEN;
        else if (t->state == TASK_CRASH_RECOVER ||
                 t->state == TASK_STUCK_DETECT ||
                 t->state == TASK_RETRY_QUEUE) cc = C_RED;
        ct_pnum(x, y, (unsigned)i, C_DIM);
        ct_pnum(px0 + 120, y, t->pid, cc);
        ct_pseg(px0 + 220, y, cn_state(t->state), cc);
        ct_pseg(px0 + 360, y, cn_prio(t->priority), cc);
        ct_pnum(px0 + 500, y, t->seg_id, cc);
        ct_pnum(px0 + 600, y, (unsigned)t->retry_times, cc);
        y += CT_ROWH;
    }
    ct_pseg(px0 + 16, gh - 34, "提示: 输入 面板 可开关本侧栏", C_DIM);
}

// 面板数据签名 (FNV): 任务池+统计任何变化都改变签名
static unsigned ct_panel_sig(void) {
    unsigned h = 2166136261u;
    h = (h ^ (unsigned)sched_count_active()) * 16777619u;
    h = (h ^ (unsigned)g_stat.total_create) * 16777619u;
    h = (h ^ (unsigned)g_stat.total_finish) * 16777619u;
    h = (h ^ (unsigned)g_stat.total_crash)  * 16777619u;
    h = (h ^ (unsigned)g_stat.total_retry)  * 16777619u;
    h = (h ^ (unsigned)g_stat.total_stuck)  * 16777619u;
    for (int i = 0; i < MAX_TASK; i++) {
        task_t *t = &task_pool[i];
        if (t->state == TASK_IDLE) continue;
        h = (h ^ (unsigned)i)                    * 16777619u;
        h = (h ^ (unsigned)t->state)             * 16777619u;
        h = (h ^ t->pid)                         * 16777619u;
        h = (h ^ (unsigned)t->priority)          * 16777619u;
        h = (h ^ t->seg_id)                      * 16777619u;
        h = (h ^ (unsigned)t->retry_times)       * 16777619u;
        h = (h ^ (unsigned)(t->is_timeout ? 3u : 1u)) * 16777619u;
        h = (h ^ (unsigned)(t->is_stuck   ? 5u : 7u)) * 16777619u;
    }
    return h;
}

// 签名门控重绘: 数据没变/不脏就一个像素都不动 (静止零重绘, 防闪烁)
static void ct_panel_sync(void) {
    if (!ct_panel_en) return;
    unsigned sg = ct_panel_sig();
    if (!ct_panel_dirty && sg == (unsigned)ct_panel_last) return;
    ct_panel_last = (int)sg;
    ct_panel_dirty = 0;
    ct_panel();
}

static void cn_help(void) {
    gprintfa(4, "================ 可用命令 ================\n");
    gprintf("  帮助            显示本帮助\n");
    gprintf("  关于            系统信息\n");
    gprintf("  统计            调度器统计\n");
    gprintf("  任务            查看任务池\n");
    gprintf("  硬件            硬件 / 触摸 / 指纹 / 摄像头状态\n");
    gprintf("  启动UI          进入 HarmonyOS 图形界面 (界面内 Esc 返回)\n");
    gprintf("  清屏            清空屏幕\n");
    gprintf("  面板            右侧任务面板开关\n");
    gprintf("  提权 / 降权     切换最高权限 / 回到用户层\n");
    gprintfa(3, "  ---- 以下命令需先[提权] ----\n");
    gprintf("  创建 <进程号> <段号> <优先级1-4>\n");
    gprintf("  运行  单步  快照 <进程号> <十六进制地址> <长度>\n");
    gprintf("  恢复  看门狗  自动  停止  初始化\n");
    gprintf("  重启  关机\n");
    gprintfa(1, "提示: 不会打中文可直接敲拼音/英文, 如 bangzhu、ui、su; \n");
    gprintfa(1, "      也可输入拼音后按空格用数字选字上屏。\n");
}

static void cn_about(void) {
    gprintfa(4, "IN OS  (c) 2026  裸机微内核操作系统\n");
    gprintf("32 位保护模式 + GOP 真彩帧缓冲 %ux%u@%u\n", g_fb_w, g_fb_h, (uint32_t)g_vbe->bpp);
    gprintf("帧缓冲物理地址 0x%x, 行跨度 %u\n", g_vbe->fb, g_pitch);
    gprintf("核心: 超级调度器 (任务池 64 / 四级优先级 / 看门狗 / 自愈)\n");
    gprintf("流水线: 等待→ARM编译→X86翻译→缓存→转发→运行\n");
    gprintf("当前权限: %s\n", g_priv ? "最高权限" : "用户层");
}

static void cn_stat(void) {
    gprintfa(4, "================ 调度器统计 ================\n");
    gprintf("总创建任务 : %lu\n", (unsigned long)g_stat.total_create);
    gprintf("总完成任务 : %lu\n", (unsigned long)g_stat.total_finish);
    gprintf("崩溃任务   : %lu\n", (unsigned long)g_stat.total_crash);
    gprintf("重试任务   : %lu\n", (unsigned long)g_stat.total_retry);
    gprintf("卡死任务   : %lu\n", (unsigned long)g_stat.total_stuck);
    gprintf("转发字节   : %lu\n", (unsigned long)g_stat.total_forward_byte);
    gprintf("快照字节   : %lu\n", (unsigned long)g_stat.total_snap_byte);
    gprintf("活动任务   : %d / %d\n", sched_count_active(), MAX_TASK);
    gprintf("内核节拍   : %lu\n", (unsigned long)g_kernel_tick);
}

static void cn_tasks(void) {
    int shown = 0;
    gprintfa(4, "槽位 进程号 状态 优先级 段号 重试 标志\n");
    for (int i = 0; i < MAX_TASK; i++) {
        task_t *t = &task_pool[i];
        if (t->state == TASK_IDLE) continue;
        shown++;
        gprintf("槽位%d 进程号%u 状态%s 优先级%s 段号%u 重试%u",
                i, t->pid, cn_state(t->state), cn_prio(t->priority),
                t->seg_id, (uint32_t)t->retry_times);
        if (t->is_timeout) gprintf(" 超时");
        if (t->is_stuck) gprintf(" 卡死");
        gprintf("\n");
    }
    if (!shown) gprintfa(1, "(当前没有活动任务, 可在提权后用 创建 命令建立)\n");
}

static void cn_hardware(void) {
    gprintfa(4, "================ 硬件状态 ================\n");
    gprintf("GOP 面板   : %ux%u\n", hw_gop_w, hw_gop_h);
    gprintf("PS/2 键盘  : %s\n", g_ps2_present ? "在线" : "无 (使用 USB 键盘)");
    gprintf("USB 键盘数 : %d   USB 鼠标数 : %d   HID报告: %lu\n",
            hw_kb_n, hw_ms_n, (unsigned long)g_usb_reports);
    gprintf("USB 触摸板 : %s   USB指纹: %s   USB摄像头: %s\n",
            usb_digi_present ? "检测到" : "无",
            usb_fp_present ? "检测到" : "无",
            usb_cam_present ? "检测到" : "无");
    if (i2c_present)
        gprintfa(2, "I2C 触摸   : 已在线 (HID 地址 0x%x)\n", i2c_addr_used);
    else if (i2c_ctrl_found)
        gprintfa(3, "I2C 触摸   : LPSS 控制器已找到, 但总线上无应答设备\n");
    else
        gprintfa(1, "I2C 触摸   : 未找到 LPSS I2C 控制器 (QEMU 属正常)\n");
    gprintf("ACPI 线索  : 触摸%s 指纹%s 摄像头%s (PCI设备 %d, ACPI表 %d)\n",
            hw_touch_hint ? "[有]" : "[无]", hw_fp_hint ? "[有]" : "[无]",
            hw_cam_hint ? "[有]" : "[无]", hw_pci_n, hw_acpi_n);
}

// 进入图形 UI, Esc 返回
static void cn_enter_ui(void) {
    gprintfa(2, "正在进入图形界面…… 进入后按 Esc 返回控制台。\n");
    g_ui_exit = 0;
    ui_enter();
    uint16_t last = pit_read();
    while (!g_ui_exit) {
        pit_wait_wrap(&last);
        g_kernel_tick++;
        ui_tick();
    }
    ct_full_redraw();
    // 提示符由 ct_commit() 在 cn_exec 返回后统一打印, 此处不再重复
}

// 需要最高权限的命令判别
static int cmd_need_admin(const char *s) {
    return cmd_is(s, "初始化", AL_INIT) || cmd_is(s, "创建", AL_CREATE) ||
           cmd_is(s, "运行", AL_RUN)   || cmd_is(s, "单步", AL_STEP) ||
           cmd_is(s, "快照", AL_SNAP)  || cmd_is(s, "恢复", AL_RECV) ||
           cmd_is(s, "看门狗", AL_WDOG)|| cmd_is(s, "自动", AL_AUTO) ||
           cmd_is(s, "停止", AL_STOP)  || cmd_is(s, "关机", AL_HALT) ||
           cmd_is(s, "重启", AL_REBOOT);
}

static void cn_exec(char *args[], int argc) {
    const char *cmd = args[0];
    if (cmd_is(cmd, "帮助", AL_HELP))  { cn_help(); return; }
    if (cmd_is(cmd, "关于", AL_ABOUT)) { cn_about(); return; }
    if (cmd_is(cmd, "清屏", AL_CLEAR)) { ct_clear(); return; }
    if (cmd_is(cmd, "统计", AL_STAT))  { cn_stat(); return; }
    if (cmd_is(cmd, "任务", AL_TASKS)) { cn_tasks(); return; }
    if (cmd_is(cmd, "硬件", AL_HW))    { cn_hardware(); return; }
    if (cmd_is(cmd, "启动UI", AL_UI))  { cn_enter_ui(); return; }
    if (cmd_is(cmd, "提权", AL_SU)) {
        if (g_priv) { gprintfa(5, "已经是最高权限。\n"); return; }
        g_priv = 1; gprintfa(2, "已获得最高权限 (提示符 #)。可用 降权 退回用户层。\n");
        return;
    }
    if (cmd_is(cmd, "降权", AL_EXIT)) {
        g_priv = 0; gprintfa(2, "已回到用户层。\n"); return;
    }
    if (cmd_is(cmd, "面板", AL_PANEL)) {
        if (!ct_panel_en && (int)g_fb_w < 1200) {
            gprintfa(3, "分辨率过低, 无法开启任务面板。\n");
            return;
        }
        ct_panel_en ^= 1;
        ct_panel_x = (int)g_fb_w - CT_PANEL_W;
        ct_panel_last = -1;
        ct_panel_dirty = ct_panel_en;
        ct_full_redraw();
        gprintfa(2, ct_panel_en ? "任务面板已开启 (右侧实时显示任务与统计)。\n"
                                : "任务面板已关闭。\n");
        return;
    }

    // ---- 以下需要最高权限 ----
    if (!g_priv) {
        if (cmd_need_admin(cmd))
            gprintfa(3, "权限不足: 「%s」需要最高权限, 请先执行 提权 (su)。\n", cmd);
        else
            gprintfa(3, "未知命令: %s  (输入 帮助 查看全部命令)\n", cmd);
        return;
    }

    if (cmd_is(cmd, "初始化", AL_INIT)) {
        sched_init(); gprintfa(2, "超级调度器已重新初始化。\n"); return;
    }
    if (cmd_is(cmd, "创建", AL_CREATE)) {
        if (argc < 4) { gprintfa(3, "用法: 创建 <进程号> <段号> <优先级1-4>\n"); return; }
        uint32_t pid = (uint32_t)katoi(args[1]);
        uint32_t seg = (uint32_t)katoi(args[2]);
        int p = katoi(args[3]);
        if (p < 1) p = 1; if (p > 4) p = 4;
        static unsigned char sample[64];
        for (int i = 0; i < 64; i++) sample[i] = (unsigned char)i;
        int tid = sched_create_task(pid, (task_prio_t)p, seg, sample, 64);
        if (tid >= 0) gprintfa(2, "任务已创建: 槽位=%d 进程号=%u 段号=%u 优先级=%s\n",
                               tid, pid, seg, cn_prio(p));
        else gprintfa(3, "创建失败: 任务池已满, 请先 恢复。\n");
        return;
    }
    if (cmd_is(cmd, "运行", AL_RUN)) {
        int ok = sched_run_all();
        gprintfa(2, "批量调度完成, %d 个任务成功执行。\n", ok); return;
    }
    if (cmd_is(cmd, "单步", AL_STEP)) {
        sched_pipeline_step();
        gprintfa(2, "流水线推进一拍, 当前活动任务 %d。\n", sched_count_active()); return;
    }
    if (cmd_is(cmd, "快照", AL_SNAP)) {
        if (argc < 4) { gprintfa(3, "用法: 快照 <进程号> <十六进制地址> <长度>\n"); return; }
        uint32_t pid = (uint32_t)katoi(args[1]);
        uint32_t vaddr = (uint32_t)kstrtoul(args[2], 16);
        uint32_t len = (uint32_t)katoi(args[3]);
        int got = sched_snapshot(pid, vaddr, len);
        gprintfa(2, "快照完成: 进程号=%u 地址=0x%x 字节数=%d\n", pid, vaddr, got); return;
    }
    if (cmd_is(cmd, "恢复", AL_RECV)) {
        int n = sched_recover_crash();
        gprintfa(2, "已回收 %d 个崩溃/卡死任务槽位。\n", n); return;
    }
    if (cmd_is(cmd, "看门狗", AL_WDOG)) {
        int stuck = sched_watchdog();
        gprintfa(2, "看门狗扫描完成, 发现 %d 个卡死任务。\n", stuck); return;
    }
    if (cmd_is(cmd, "自动", AL_AUTO)) {
        g_auto = 1; gprintfa(2, "自动演示已开启。\n"); return;
    }
    if (cmd_is(cmd, "停止", AL_STOP)) {
        g_auto = 0; gprintfa(2, "自动演示已停止。\n"); return;
    }
    if (cmd_is(cmd, "关机", AL_HALT)) {
        gprintfa(2, "系统正在关机, 请按电源键重启。\n");
        __asm__ __volatile__("cli");
        for (;;) __asm__ __volatile__("hlt");
    }
    if (cmd_is(cmd, "重启", AL_REBOOT)) {
        gprintfa(2, "正在重启……\n");
        __asm__ __volatile__("cli\n" "lidt 0\n" "int $0x03");
        return;
    }
    gprintfa(3, "未知命令: %s  (输入 帮助 查看全部命令)\n", cmd);
}

// ====================== 键盘输入 ======================
static int ct_getchar(void) {
    int c = -1;
    if (g_ps2_present) {
        c = kb_poll();
        if (c == -2) return -3;          // pause 序列, 丢弃
    }
    if (c < 0) { usb_poll(); c = usb_kb_getc(); }
    return c;
}

static void ct_commit(void) {
    // 把已输入命令固化到当前行缓冲
    for (int i = 0; i < ct_inlen; i++) {
        int L = ct_len[ct_inrow];
        if (L < CT_LINEMAX - 1) {
            ct_buf[ct_inrow][L] = ct_in[i];
            ct_at[ct_inrow][L] = 0;
            ct_len[ct_inrow] = L + 1;
        }
    }
    ct_render_row(ct_inrow);
    ct_newline();
    if (ct_inlen > 0) {
        ct_in[ct_inlen] = 0;           // 显式终止, 防 cli_tokenize 读到上次残留
        char *args[16];
        int argc = cli_tokenize(ct_in, args, 16);
        if (argc > 0) cn_exec(args, argc);
    }
    ct_inlen = 0;
    ime_active = 0;
    ct_prompt();
}

// 取输入尾部连续小写字母段, 返回起点 (无则 -1)
static int ct_tail_letters(void) {
    int e = ct_inlen, s = e;
    while (s > 0) {
        char ch = ct_in[s - 1];
        if (ch >= 'a' && ch <= 'z') s--;
        else break;
    }
    return (s < e) ? s : -1;
}

static void ct_try_ime(void) {
    int s = ct_tail_letters();
    if (s < 0) { ct_in[ct_inlen++] = ' '; ct_render_input(); return; }
    ime_cand_n = 0;
    for (int k = 0; k < IME_N && ime_cand_n < IME_CAND_MAX; k++) {
        const char *py = ime_dict[k].py;
        int pl = 0; while (py[pl]) pl++;
        int sl = ct_inlen - s;
        if (pl == sl) {
            int eq = 1;
            for (int j = 0; j < pl; j++) if (py[j] != ct_in[s + j]) { eq = 0; break; }
            if (eq) ime_cand_idx[ime_cand_n++] = k;
        }
    }
    if (ime_cand_n > 0) { ime_active = 1; ime_segstart = s; ct_render_input(); }
    else { ct_in[ct_inlen++] = ' '; ct_render_input(); }
}

static void ct_kb_poll(void) {
    int guard = 64;
    while (guard-- > 0) {
        int c = ct_getchar();
        if (c == -3) continue;
        if (c < 0) break;
        if (c == 0) continue;

        if (c == '\n' || c == '\r') { ct_commit(); continue; }

        // 拼音候选选词状态
        if (ime_active) {
            if (c >= '1' && c <= '9') {
                int idx = c - '1';
                if (idx < ime_cand_n) {
                    const char *hz = ime_dict[ime_cand_idx[idx]].hz;
                    ct_inlen = ime_segstart;
                    for (int k = 0; hz[k] && ct_inlen < CT_LINEMAX - 4; k++)
                        ct_in[ct_inlen++] = hz[k];
                    ime_active = 0; ct_render_input();
                }
                continue;
            }
            ime_active = 0;   // 其它键取消候选, 并继续处理该键
        }

        if (c == 0x08) {                      // 退格
            if (ct_inlen > 0) {
                char ch = ct_in[ct_inlen - 1];
                ct_inlen--;
                (void)ch;
                ct_render_input();
            }
            continue;
        }
        if (c == 0x1B) {                      // Esc: 清空输入
            ct_inlen = 0; ct_render_input(); continue;
        }
        if (c == ' ' || c == '\t') { ct_try_ime(); continue; }
        if (c >= ' ' && c < 127 && ct_inlen < CT_LINEMAX - 1) {
            ct_in[ct_inlen++] = (char)c;
            ct_render_input();
        }
    }
}

// ====================== 初始化 ======================
static void ct_init(void) {
    ct_bg = C_DESKTOP;
    ct_rows = ((int)g_fb_h - 2 * CT_MARGIN) / CT_ROWH;
    if (ct_rows < 8) ct_rows = 8;
    if (ct_rows > CT_MAXROWS) ct_rows = CT_MAXROWS;
    for (int r = 0; r < CT_MAXROWS; r++) ct_len[r] = 0;
    ct_r = 0; ct_x = CT_MARGIN;
    ct_inlen = 0; ime_active = 0; g_priv = 0;
    ct_panel_x = (int)g_fb_w - CT_PANEL_W;
    if ((int)g_fb_w < 1200) ct_panel_en = 0;   // 小屏不开面板
    ct_clear();
}

static void ct_banner(void) {
    gprintfa(4, "==================================================\n");
    gprintfa(4, "   IN OS  裸机微内核操作系统  (c) 2026\n");
    gprintfa(4, "   32位保护模式  |  GOP 真彩图形  |  超级调度器\n");
    gprintfa(4, "==================================================\n");
    gprintfa(1, "内核入口 0x300200, 帧缓冲 %ux%u, 正在初始化各模块……\n", g_fb_w, g_fb_h);
}

// 控制台主循环 (忙轮询键盘, 跟手; 裸机无功耗管理)
static void ct_run(void) {
    uint16_t last = pit_read();
    for (;;) {
        ct_kb_poll();
        pit_wait_wrap(&last);          // 10ms 节拍 (空闲时 tick 不自走)
        g_kernel_tick++;
        if (ct_panel_en && (g_kernel_tick & 7u) == 0) ct_panel_sync();  // ~80ms 检查, 数据变了才重绘
    }
}

#endif // CTERM_H
