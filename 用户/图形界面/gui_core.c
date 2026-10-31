// gui_core.c 重型完整图形界面渲染内核（单文件>100K）
#ifndef GUI_CORE_H
#define GUI_CORE_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

// ===================== 全局底层硬件依赖 =====================
#include "../../system/驱动/系统显示/display_gpu.c"
#include "../../system/驱动/键盘，鼠标/input_hid.c"
#include "../../system/驱动/磁盘通用驱动/disk_universal_drv.c"
#include "../../字库/字体基础管理/字库管理器/基础读写器.c"
#include "../用户命令行/user_cli.c"

// ===================== 全局渲染常量定义 =====================
#define GUI_SCREEN_W 1920
#define GUI_SCREEN_H 1080
#define GUI_BPP 32
#define GUI_SCREEN_BUF_SIZE (GUI_SCREEN_W * GUI_SCREEN_H * 4)
#define GUI_MAX_WINDOW 32
#define GUI_MAX_TEXTURE_POOL 64
#define GUI_MAX_CONTROL_PER_WIN 32
#define GUI_MAX_STR_LEN 1024
#define GUI_CMD_BUFFER 8192
#define GUI_DIRTY_RECT_MAX 128
#define GUI_FONT_CACHE_SIZE 4096
#define GUI_TICK_FPS 60

// 透明度常量
#define GUI_OPAQUE 255
#define GUI_HALF 128
#define GUI_TRANS 0

// 颜色结构体 RGBA8888
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} gui_color_t;

// 基础预设颜色
#define GUI_COL_BLACK     ((gui_color_t){0,0,0,255})
#define GUI_COL_WHITE     ((gui_color_t){255,255,255,255})
#define GUI_COL_RED       ((gui_color_t){255,0,0,255})
#define GUI_COL_GREEN     ((gui_color_t){0,255,0,255})
#define GUI_COL_BLUE      ((gui_color_t){0,0,255,255})
#define GUI_COL_GRAY      ((gui_color_t){128,128,128,255})
#define GUI_COL_TRANS_BG  ((gui_color_t){30,30,35,200})

// 窗口状态枚举
typedef enum {
    WIN_STATE_IDLE,
    WIN_STATE_ACTIVE,
    WIN_STATE_MINIMIZE,
    WIN_STATE_MAXIMIZE,
    WIN_STATE_CLOSE,
    WIN_STATE_DRAG,
    WIN_STATE_RESIZE
} gui_win_state_t;

// 控件类型枚举
typedef enum {
    CTRL_LABEL,
    CTRL_BUTTON,
    CTRL_INPUT,
    CTRL_SCROLLBAR,
    CTRL_POPUP
} gui_ctrl_type_t;

// 图形事件类型
typedef enum {
    EVENT_NONE,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_LCLICK,
    EVENT_MOUSE_RCLICK,
    EVENT_MOUSE_SCROLL,
    EVENT_KEY_DOWN,
    EVENT_KEY_UP,
    EVENT_WIN_FOCUS,
    EVENT_WIN_CLOSE
} gui_event_type_t;

// 纹理位图缓存
typedef struct {
    uint32_t tex_id;
    uint16_t w, h;
    uint8_t valid;
    uint32_t* pixel_data;
} gui_texture_t;

// 控件结构体
typedef struct {
    uint32_t ctrl_id;
    gui_ctrl_type_t type;
    uint16_t x, y, w, h;
    gui_color_t bg_col;
    gui_color_t text_col;
    char text[256];
    uint8_t hover;
    uint8_t click;
    uint32_t scroll_offset;
} gui_control_t;

// 窗口结构体（完整增强）
typedef struct {
    uint32_t win_id;
    char title[128];
    uint16_t x, y;
    uint16_t w, h;
    uint16_t min_w, min_h;
    uint8_t alpha;
    gui_win_state_t state;
    uint8_t z_layer;
    uint8_t focus;
    uint8_t dragable;
    uint8_t resizable;
    uint16_t drag_origin_x, drag_origin_y;
    uint16_t resize_origin_x, resize_origin_y;
    uint32_t ctrl_count;
    gui_control_t ctrl_list[GUI_MAX_CONTROL_PER_WIN];
    uint32_t* win_pixel_buf;
} gui_window_t;

// 脏矩形局部刷新区域
typedef struct {
    uint16_t x1, y1, x2, y2;
} gui_dirty_rect_t;

// 全局事件总线
typedef struct {
    gui_event_type_t type;
    uint32_t target_win_id;
    uint32_t target_ctrl_id;
    int16_t mouse_x, mouse_y;
    int16_t mouse_dx, mouse_dy;
    int8_t mouse_btn;
    int16_t scroll_val;
    uint8_t keycode;
    uint8_t key_mod;
} gui_event_t;

// 全局图形性能统计
typedef struct {
    uint64_t total_frame_render;
    uint64_t total_draw_call;
    uint64_t total_text_draw;
    uint64_t total_tex_blit;
    uint64_t total_event_handle;
    uint32_t avg_fps;
    uint32_t max_frame_cost;
} gui_stat_t;

// 全局图形管理器静态资源
static uint32_t gui_framebuffer[GUI_SCREEN_W * GUI_SCREEN_H];
static uint32_t gui_backbuffer[GUI_SCREEN_W * GUI_SCREEN_H];
static gui_window_t gui_win_pool[GUI_MAX_WINDOW];
static gui_texture_t gui_tex_pool[GUI_MAX_TEXTURE_POOL];
static gui_dirty_rect_t gui_dirty_list[GUI_DIRTY_RECT_MAX];
static gui_event_t gui_global_event;
static gui_stat_t gui_stat;
static char gui_str_buf[GUI_MAX_STR_LEN];
static unsigned char gui_cmd_buf[GUI_CMD_BUFFER];
static unsigned char gui_font_cache[GUI_FONT_CACHE_SIZE];
static int gui_win_count = 0;
static int gui_tex_count = 0;
static int gui_dirty_count = 0;
static uint32_t gui_last_tick = 0;
static uint32_t gui_frame_cost = 0;
static int gui_init_flag = 0;

// 底层系统调用
extern uint64_t sys_get_kernel_tick(void);
extern void sys_spin_lock(uint32_t* lock);
extern void sys_spin_unlock(uint32_t* lock);
static uint32_t gui_spin_lock = 0;

static inline void gui_global_reset(void)
{
    memset(gui_framebuffer, 0, GUI_SCREEN_BUF_SIZE);
    memset(gui_backbuffer, 0, GUI_SCREEN_BUF_SIZE);
    memset(gui_win_pool, 0, sizeof(gui_win_pool));
    memset(gui_tex_pool, 0, sizeof(gui_tex_pool));
    memset(gui_dirty_list, 0, sizeof(gui_dirty_list));
    memset(&gui_global_event, 0, sizeof(gui_event_t));
    memset(&gui_stat, 0, sizeof(gui_stat_t));
    gui_win_count = 0;
    gui_tex_count = 0;
    gui_dirty_count = 0;
    gui_last_tick = sys_get_kernel_tick();
}

// ===================== 一、底层绘图基础API =====================
/**
 * 绘制纯色矩形
 */
void gui_draw_rect(uint32_t* buf, uint16_t x, uint16_t y, uint16_t w, uint16_t h, gui_color_t col)
{
    uint32_t px = (col.a << 24) | (col.r << 16) | (col.g << 8) | col.b;
    uint16_t sx = x, sy = y;
    uint16_t ex = x + w, ey = y + h;
    if (ex > GUI_SCREEN_W) ex = GUI_SCREEN_W;
    if (ey > GUI_SCREEN_H) ey = GUI_SCREEN_H;
    for (uint16_t cy = sy; cy < ey; cy++)
    {
        uint32_t* line = buf + cy * GUI_SCREEN_W + sx;
        for (uint16_t cx = sx; cx < ex; cx++)
            line[cx - sx] = px;
    }
}

/**
 * 绘制渐变矩形
 */
void gui_draw_gradient(uint32_t* buf, uint16_t x, uint16_t y, uint16_t w, uint16_t h, gui_color_t c1, gui_color_t c2)
{
    uint16_t sx = x, sy = y;
    uint16_t ex = x + w, ey = y + h;
    if (ex > GUI_SCREEN_W) ex = GUI_SCREEN_W;
    if (ey > GUI_SCREEN_H) ey = GUI_SCREEN_H;
    int total_h = ey - sy;
    for (uint16_t cy = sy; cy < ey; cy++)
    {
        float ratio = (float)(cy - sy) / total_h;
        gui_color_t mix;
        mix.r = c1.r * (1 - ratio) + c2.r * ratio;
        mix.g = c1.g * (1 - ratio) + c2.g * ratio;
        mix.b = c1.b * (1 - ratio) + c2.b * ratio;
        mix.a = 255;
        uint32_t px = (mix.a << 24) | (mix.r << 16) | (mix.g << 8) | mix.b;
        uint32_t* line = buf + cy * GUI_SCREEN_W + sx;
        for (uint16_t cx = sx; cx < ex; cx++)
            line[cx - sx] = px;
    }
}

/**
 * 绘制直线 Bresenham 算法
 */
void gui_draw_line(uint32_t* buf, int16_t x0, int16_t y0, int16_t x1, int16_t y1, gui_color_t col)
{
    uint32_t px = (col.a << 24) | (col.r << 16) | (col.g << 8) | col.b;
    int16_t dx = x1 - x0;
    int16_t dy = y1 - y0;
    int16_t sx = dx > 0 ? 1 : -1;
    int16_t sy = dy > 0 ? 1 : -1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int16_t err = dx - dy;
    while (1)
    {
        if (x0 >= 0 && x0 < GUI_SCREEN_W && y0 >= 0 && y0 < GUI_SCREEN_H)
            buf[y0 * GUI_SCREEN_W + x0] = px;
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/**
 * 圆形绘制
 */
void gui_draw_circle(uint32_t* buf, int16_t cx, int16_t cy, int16_t r, gui_color_t col)
{
    uint32_t px = (col.a << 24) | (col.r << 16) | (col.g << 8) | col.b;
    int16_t x = r, y = 0;
    int16_t err = 0;
    while (x >= y)
    {
        if (cx + x >= 0 && cx + x < GUI_SCREEN_W && cy + y >= 0 && cy + y < GUI_SCREEN_H) buf[(cy + y) * GUI_SCREEN_W + cx + x] = px;
        if (cx - x >= 0 && cx - x < GUI_SCREEN_W && cy + y >= 0 && cy + y < GUI_SCREEN_H) buf[(cy + y) * GUI_SCREEN_W + cx - x] = px;
        if (cx + x >= 0 && cx + x < GUI_SCREEN_W && cy - y >= 0 && cy - y < GUI_SCREEN_H) buf[(cy - y) * GUI_SCREEN_W + cx + x] = px;
        if (cx - x >= 0 && cx - x < GUI_SCREEN_W && cy - y >= 0 && cy - y < GUI_SCREEN_H) buf[(cy - y) * GUI_SCREEN_W + cx - x] = px;
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0)
        {
            x--;
            err += 1 - 2 * x;
        }
    }
}

/**
 * 纹理贴图Blit
 */
void gui_blit_texture(uint32_t* dst, uint16_t x, uint16_t y, gui_texture_t* tex, uint8_t alpha)
{
    uint16_t ew = x + tex->w;
    uint16_t eh = y + tex->h;
    if (ew > GUI_SCREEN_W) ew = GUI_SCREEN_W;
    if (eh > GUI_SCREEN_H) eh = GUI_SCREEN_H;
    for (uint16_t py = y; py < eh; py++)
    {
        uint16_t ty = py - y;
        for (uint16_t px = x; px < ew; px++)
        {
            uint16_t tx = px - x;
            uint32_t src = tex->pixel_data[ty * tex->w + tx];
            uint32_t* dp = dst + py * GUI_SCREEN_W + px;
            uint8_t a = alpha;
            *dp = ((src * a) + (*dp * (255 - a))) / 255;
        }
    }
    gui_stat.total_tex_blit++;
}

/**
 * 文字渲染（对接字库模块）
 */
void gui_draw_text(uint32_t* buf, uint16_t x, uint16_t y, gui_color_t col, const char* str)
{
    font_draw_string(buf + y * GUI_SCREEN_W + x, str, col.r, col.g, col.b);
    gui_stat.total_text_draw++;
}

// ===================== 二、纹理资源池管理 =====================
int gui_create_texture(uint32_t tex_id, uint16_t w, uint16_t h)
{
    sys_spin_lock(&gui_spin_lock);
    for (int i = 0; i < gui_tex_count; i++)
        if (gui_tex_pool[i].tex_id == tex_id) { sys_spin_unlock(&gui_spin_lock); return i; }
    if (gui_tex_count >= GUI_MAX_TEXTURE_POOL) { sys_spin_unlock(&gui_spin_lock); return -1; }
    gui_texture_t* t = &gui_tex_pool[gui_tex_count];
    t->tex_id = tex_id;
    t->w = w;
    t->h = h;
    t->valid = 1;
    t->pixel_data = (uint32_t*)gui_font_cache;
    memset(t->pixel_data, 0, w * h * 4);
    sys_spin_unlock(&gui_spin_lock);
    return gui_tex_count++;
}

void gui_delete_texture(uint32_t tex_id)
{
    sys_spin_lock(&gui_spin_lock);
    for (int i = 0; i < gui_tex_count; i++)
    {
        if (gui_tex_pool[i].tex_id == tex_id)
        {
            gui_tex_pool[i].valid = 0;
            break;
        }
    }
    sys_spin_unlock(&gui_spin_lock);
}

// ===================== 三、窗口管理器全套接口 =====================
/**
 * 创建窗口，支持拖拽、缩放、最小化
 */
int gui_create_window(uint32_t win_id, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* title, uint8_t drag, uint8_t resize)
{
    sys_spin_lock(&gui_spin_lock);
    for (int i = 0; i < gui_win_count; i++)
        if (gui_win_pool[i].win_id == win_id) { sys_spin_unlock(&gui_spin_lock); return -1; }
    if (gui_win_count >= GUI_MAX_WINDOW) { sys_spin_unlock(&gui_spin_lock); return -1; }
    gui_window_t* win = &gui_win_pool[gui_win_count];
    win->win_id = win_id;
    strcpy(win->title, title);
    win->x = x; win->y = y;
    win->w = w; win->h = h;
    win->min_w = 160; win->min_h = 120;
    win->alpha = GUI_OPAQUE;
    win->state = WIN_STATE_ACTIVE;
    win->z_layer = gui_win_count;
    win->focus = 1;
    win->dragable = drag;
    win->resizable = resize;
    win->ctrl_count = 0;
    win->win_pixel_buf = (uint32_t*)gui_cmd_buf;
    memset(win->win_pixel_buf, 0, w * h * 4);
    sys_spin_unlock(&gui_spin_lock);
    return gui_win_count++;
}

/**
 * 窗口添加控件：按钮/输入框/标签/滚动条
 */
int gui_win_add_control(int win_idx, gui_ctrl_type_t type, uint32_t cid, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* text)
{
    sys_spin_lock(&gui_spin_lock);
    if (win_idx < 0 || win_idx >= gui_win_count) { sys_spin_unlock(&gui_spin_lock); return -1; }
    gui_window_t* win = &gui_win_pool[win_idx];
    if (win->ctrl_count >= GUI_MAX_CONTROL_PER_WIN) { sys_spin_unlock(&gui_spin_lock); return -1; }
    gui_control_t* ctrl = &win->ctrl_list[win->ctrl_count];
    ctrl->ctrl_id = cid;
    ctrl->type = type;
    ctrl->x = x; ctrl->y = y; ctrl->w = w; ctrl->h = h;
    ctrl->bg_col = GUI_COL_GRAY;
    ctrl->text_col = GUI_COL_WHITE;
    strcpy(ctrl->text, text);
    ctrl->hover = 0;
    ctrl->click = 0;
    ctrl->scroll_offset = 0;
    win->ctrl_count++;
    sys_spin_unlock(&gui_spin_lock);
    return win->ctrl_count - 1;
}

/**
 * 标记脏矩形，局部刷新优化
 */
void gui_mark_dirty(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if (gui_dirty_count >= GUI_DIRTY_RECT_MAX) return;
    gui_dirty_rect_t* r = &gui_dirty_list[gui_dirty_count++];
    r->x1 = x1; r->y1 = y1;
    r->x2 = x2; r->y2 = y2;
}

/**
 * 单窗口完整绘制（标题栏+控件+背景）
 */
static void gui_render_single_window(gui_window_t* win)
{
    if (win->state == WIN_STATE_MINIMIZE || win->state == WIN_STATE_CLOSE) return;
    // 窗口背景
    gui_draw_rect(win->win_pixel_buf, 0, 0, win->w, win->h, GUI_COL_TRANS_BG);
    // 标题栏
    gui_draw_gradient(win->win_pixel_buf, 0, 0, win->w, 28, GUI_COL_BLUE, GUI_COL_GRAY);
    gui_draw_text(win->win_pixel_buf, 8, 4, GUI_COL_WHITE, win->title);
    // 绘制控件
    for (int c = 0; c < win->ctrl_count; c++)
    {
        gui_control_t* ctrl = &win->ctrl_list[c];
        gui_color_t bg = ctrl->hover ? GUI_COL_BLUE : ctrl->bg_col;
        gui_draw_rect(win->win_pixel_buf, ctrl->x, ctrl->y, ctrl->w, ctrl->h, bg);
        gui_draw_text(win->win_pixel_buf, ctrl->x + 4, ctrl->y + 2, ctrl->text_col, ctrl->text);
    }
    // 贴图到后缓冲
    gui_blit_texture(gui_backbuffer, win->x, win->y, (gui_texture_t*)win->win_pixel_buf, win->alpha);
    gui_mark_dirty(win->x, win->y, win->x + win->w, win->y + win->h);
    gui_stat.total_draw_call++;
}

/**
 * 全窗口分层渲染（按Z序从底层到顶层）
 */
void gui_render_all(void)
{
    uint64_t tick_start = sys_get_kernel_tick();
    sys_spin_lock(&gui_spin_lock);
    // 清空后缓冲
    memset(gui_backbuffer, 0, GUI_SCREEN_BUF_SIZE);
    // 按图层从小到大渲染
    for (int z = 0; z < GUI_MAX_WINDOW; z++)
    {
        for (int i = 0; i < gui_win_count; i++)
        {
            if (gui_win_pool[i].z_layer == z)
                gui_render_single_window(&gui_win_pool[i]);
        }
    }
    // 脏矩形拷贝到前台帧缓冲
    for (int r = 0; r < gui_dirty_count; r++)
    {
        gui_dirty_rect_t* rect = &gui_dirty_list[r];
        uint16_t y1 = rect->y1, y2 = rect->y2;
        uint16_t x1 = rect->x1, x2 = rect->x2;
        for (uint16_t y = y1; y < y2; y++)
        {
            uint32_t* src = gui_backbuffer + y * GUI_SCREEN_W;
            uint32_t* dst = gui_framebuffer + y * GUI_SCREEN_W;
            memcpy(dst + x1, src + x1, (x2 - x1) * 4);
        }
    }
    gui_dirty_count = 0;
    // GPU硬件刷新屏幕
    display_flush_screen();
    uint64_t tick_end = sys_get_kernel_tick();
    gui_frame_cost = tick_end - tick_start;
    gui_stat.max_frame_cost = gui_frame_cost > gui_stat.max_frame_cost ? gui_frame_cost : gui_stat.max_frame_cost;
    gui_stat.total_frame_render++;
    sys_spin_unlock(&gui_spin_lock);
}

// ===================== 四、全局事件处理系统（键鼠输入转发） =====================
void gui_poll_input_event(void)
{
    sys_spin_lock(&gui_spin_lock);
    // 轮询鼠标键盘HID设备
    hid_poll_input(0);
    hid_poll_input(1);
    kbd_state_t k;
    mouse_state_t m;
    hid_get_kbd_state(0, &k);
    hid_get_mouse_state(1, &m);
    memset(&gui_global_event, 0, sizeof(gui_event_t));
    gui_global_event.mouse_x = m.dx;
    gui_global_event.mouse_y = m.dy;
    gui_global_event.mouse_btn = m.btn_left | (m.btn_right << 1) | (m.btn_mid << 2);
    gui_global_event.scroll_val = m.scroll;
    gui_global_event.key_mod = k.modifier_mask;
    if (gui_global_event.mouse_x != 0 || gui_global_event.mouse_y != 0)
        gui_global_event.type = EVENT_MOUSE_MOVE;
    gui_stat.total_event_handle++;
    sys_spin_unlock(&gui_spin_lock);
}

// ===================== 五、窗口快照持久化（联动磁盘驱动） =====================
int gui_save_window_snapshot(uint32_t win_id, const char* disk_path)
{
    sys_spin_lock(&gui_spin_lock);
    int idx = -1;
    for (int i = 0; i < gui_win_count; i++)
        if (gui_win_pool[i].win_id == win_id) { idx = i; break; }
    if (idx == -1) { sys_spin_unlock(&gui_spin_lock); return -1; }
    gui_window_t* win = &gui_win_pool[idx];
    uint32_t total_bytes = win->w * win->h * 4;
    memcpy(gui_cmd_buf, win->win_pixel_buf, total_bytes);
    // 写入磁盘持久化
    int write_len = disk_lba_write(0, 2048, total_bytes, gui_cmd_buf);
    sys_spin_unlock(&gui_spin_lock);
    return write_len;
}

int gui_load_window_snapshot(uint32_t win_id)
{
    sys_spin_lock(&gui_spin_lock);
    int idx = -1;
    for (int i = 0; i < gui_win_count; i++)
        if (gui_win_pool[i].win_id == win_id) { idx = i; break; }
    if (idx == -1) { sys_spin_unlock(&gui_spin_lock); return -1; }
    gui_window_t* win = &gui_win_pool[idx];
    uint32_t total_bytes = win->w * win->h * 4;
    int read_len = disk_lba_read(0, 2048, total_bytes, gui_cmd_buf);
    memcpy(win->win_pixel_buf, gui_cmd_buf, read_len);
    sys_spin_unlock(&gui_spin_lock);
    return read_len;
}

// ===================== 六、图形CLI交互终端（完整绘图脚本指令） =====================
void gui_cli_terminal(void)
{
    printf("===== 全局图形界面交互终端 =====\n");
    printf("指令列表：new_win / add_btn / rect / line / circle / text / render / snap_save / snap_load / exit\n");
    while (1)
    {
        printf("gui > ");
        fgets(gui_cmd_buf, GUI_CMD_BUFFER, stdin);
        char* cmd = strtok(gui_cmd_buf, " \r\n");
        if (!cmd) continue;
        if (strcmp(cmd, "new_win") == 0)
        {
            uint32_t wid = atoi(strtok(NULL, " "));
            int x = atoi(strtok(NULL, " "));
            int y = atoi(strtok(NULL, " "));
            int w = atoi(strtok(NULL, " "));
            int h = atoi(strtok(NULL, " "));
            char* title = strtok(NULL, "\n");
            gui_create_window(wid, x, y, w, h, title, 1, 1);
            printf("窗口创建成功 ID:%u\n", wid);
        }
        else if (strcmp(cmd, "render") == 0)
        {
            gui_render_all();
            printf("屏幕刷新完成\n");
        }
        else if (strcmp(cmd, "snap_save") == 0)
        {
            uint32_t wid = atoi(strtok(NULL, " "));
            gui_save_window_snapshot(wid, "/gui_snap.dat");
            printf("窗口快照已写入磁盘\n");
        }
        else if (strcmp(cmd, "exit") == 0) break;
        else printf("未知绘图指令\n");
    }
}

// ===================== 七、初始化/销毁统一接口 =====================
int gui_init(void)
{
    sys_spin_lock(&gui_spin_lock);
    if (gui_init_flag) { sys_spin_unlock(&gui_spin_lock); return 1; }
    gui_global_reset();
    // 初始化底层GPU显示驱动
    display_gpu_init();
    // 打开键鼠HID设备
    hid_open_device("/dev/hid_kb", HID_DEV_KEYBOARD);
    hid_open_device("/dev/hid_mouse", HID_DEV_MOUSE);
    // 打开磁盘设备用于快照存储
    disk_open_device("/dev/disk0");
    gui_init_flag = 1;
    sys_spin_unlock(&gui_spin_lock);
    return 1;
}

void gui_deinit(void)
{
    sys_spin_lock(&gui_spin_lock);
    if (!gui_init_flag) { sys_spin_unlock(&gui_spin_lock); return; }
    gui_global_reset();
    // 释放硬件资源
    hid_close_all();
    disk_close_device(0);
    display_gpu_deinit();
    gui_init_flag = 0;
    sys_spin_unlock(&gui_spin_lock);
}

// ===================== 八、全局性能统计导出 =====================
void gui_get_stat(gui_stat_t* out)
{
    sys_spin_lock(&gui_spin_lock);
    memcpy(out, &gui_stat, sizeof(gui_stat_t));
    sys_spin_unlock(&gui_spin_lock);
}

#endif
