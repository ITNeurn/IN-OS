// display_gpu.c 帧缓冲GPU显示驱动
#ifndef DISPLAY_GPU_H
#define DISPLAY_GPU_H

#include <stdint.h>
#include <string.h>

// 显示基础参数
#define FB_WIDTH  1920
#define FB_HEIGHT 1080
#define BPP       32
#define FB_BUF_SIZE (FB_WIDTH * FB_HEIGHT * 4)
#define GPU_CMD_BUF 4096

// 渲染图层最大层数
#define MAX_LAYER_COUNT 8

// 颜色结构体 RGBA8888
typedef struct {
    uint8_t r, g, b, a;
} rgba_t;

// 图层描述符
typedef struct {
    uint32_t layer_id;
    uint16_t x, y;
    uint16_t w, h;
    uint8_t alpha;
    uint8_t visible;
    uint32_t *pixel_buf;
} layer_t;

static uint32_t framebuffer[FB_WIDTH * FB_HEIGHT];
static unsigned char gpu_cmd_buf[GPU_CMD_BUF];
static layer_t layer_pool[MAX_LAYER_COUNT];
static int layer_cnt = 0;
static uint32_t gpu_dev_fd = 0;
static int gpu_inited = 0;

// GPU硬件系统调用
extern uint32_t sys_gpu_fb_open(void);
extern void sys_gpu_fb_close(uint32_t fd);
extern int sys_gpu_flush(uint32_t fd, uint32_t *fb, uint32_t len);

static inline void display_reset(void)
{
    memset(framebuffer, 0, FB_BUF_SIZE);
    memset(gpu_cmd_buf, 0, GPU_CMD_BUF);
    layer_cnt = 0;
    for(int i = 0; i < MAX_LAYER_COUNT; i++)
    {
        layer_pool[i].layer_id = 0;
        layer_pool[i].visible = 0;
        layer_pool[i].pixel_buf = NULL;
    }
}

/**
 * @brief 初始化GPU帧缓冲硬件
 */
int display_gpu_init(void)
{
    if(gpu_inited) return 1;
    gpu_dev_fd = sys_gpu_fb_open();
    if(gpu_dev_fd == 0) return 0;
    display_reset();
    gpu_inited = 1;
    return 1;
}

/**
 * @brief 创建渲染图层
 * @param lid 图层唯一ID
 * @param x 左上角X
 * @param y 左上角Y
 * @param w 宽 h 高
 * @return 图层索引
 */
int display_create_layer(uint32_t lid, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if(layer_cnt >= MAX_LAYER_COUNT) return -1;
    for(int i = 0; i < layer_cnt; i++)
        if(layer_pool[i].layer_id == lid) return -1;

    layer_t *ly = &layer_pool[layer_cnt++];
    ly->layer_id = lid;
    ly->x = x; ly->y = y;
    ly->w = w; ly->h = h;
    ly->alpha = 255;
    ly->visible = 1;
    // 分配图层像素缓存
    ly->pixel_buf = (uint32_t*)&gpu_cmd_buf[layer_cnt * 1024];
    memset(ly->pixel_buf, 0, w * h * 4);
    return layer_cnt - 1;
}

/**
 * @brief 纯色填充图层
 */
void display_layer_fill(int lyr_idx, rgba_t color)
{
    if(lyr_idx < 0 || lyr_idx >= layer_cnt) return;
    layer_t *ly = &layer_pool[lyr_idx];
    uint32_t pixel = (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
    int total = ly->w * ly->h;
    for(int i = 0; i < total; i++)
        ly->pixel_buf[i] = pixel;
}

/**
 * @brief 图层混合合成到主帧缓冲
 */
void display_compose_layers(void)
{
    // 清空主画布
    memset(framebuffer, 0, FB_BUF_SIZE);
    // 从底层到顶层叠加
    for(int lyr = 0; lyr < layer_cnt; lyr++)
    {
        layer_t *ly = &layer_pool[lyr];
        if(!ly->visible) continue;
        for(int y = 0; y < ly->h; y++)
        {
            uint16_t screen_y = ly->y + y;
            if(screen_y >= FB_HEIGHT) break;
            for(int x = 0; x < ly->w; x++)
            {
                uint16_t screen_x = ly->x + x;
                if(screen_x >= FB_WIDTH) break;
                uint32_t src_pix = ly->pixel_buf[y * ly->w + x];
                // 简易Alpha混合
                uint8_t a = ly->alpha;
                uint32_t *dst = &framebuffer[screen_y * FB_WIDTH + screen_x];
                *dst = ((src_pix * a) + (*dst * (255 - a))) / 255;
            }
        }
    }
}

/**
 * @brief 硬件刷新屏幕
 */
int display_flush_screen(void)
{
    if(!gpu_inited) return 0;
    return sys_gpu_flush(gpu_dev_fd, framebuffer, FB_BUF_SIZE / 4);
}

/**
 * @brief 销毁图层
 */
void display_delete_layer(uint32_t lid)
{
    for(int i = 0; i < layer_cnt; i++)
    {
        if(layer_pool[i].layer_id == lid)
        {
            // 数组前移删除
            for(int j = i; j < layer_cnt - 1; j++)
                layer_pool[j] = layer_pool[j+1];
            layer_cnt--;
            break;
        }
    }
}

/**
 * @brief 释放GPU硬件资源
 */
void display_gpu_deinit(void)
{
    if(gpu_inited)
    {
        sys_gpu_fb_close(gpu_dev_fd);
        gpu_dev_fd = 0;
        gpu_inited = 0;
    }
    display_reset();
}

#endif
