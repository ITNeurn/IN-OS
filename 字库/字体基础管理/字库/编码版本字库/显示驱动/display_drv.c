// display_drv.c
// 编码版本字库专用显示驱动
// 路径：字体基础管理/字库/编码版本字库/显示驱动
// 依赖：chinses_encode_font.c、ascii_encode_font.c、系统坐标读写接口

#ifndef DISPLAY_DRV_H
#define DISPLAY_DRV_H

// 引入字体编码层对外接口
extern int chinese_encode_get_font(unsigned char h_byte, unsigned char l_byte, unsigned char *out_dot);
extern int english_encode_get_font(unsigned char ascii_code, unsigned char *out_dot);

// 系统底层坐标读写接口（来自调度核心/基础读写器）
// 写入显存指定坐标，width像素宽，height像素高，buf为点阵数据
extern void block_write_pos(int x, int y, int width, int height, unsigned char *buf);

// 显存全局基础参数
#define VRAM_BASE_X 0
#define VRAM_BASE_Y 0
#define VRAM_MAX_X  800
#define VRAM_MAX_Y  600

// 字符渲染缓冲区
#define CN_CHAR_BUF_LEN  (16 * 16 / 8)
#define EN_CHAR_BUF_LEN  16
static unsigned char cn_render_buf[CN_CHAR_BUF_LEN];
static unsigned char en_render_buf[EN_CHAR_BUF_LEN];

// 清空渲染缓存
static inline void drv_clear_cn_buf(void)
{
    for(int i = 0; i < CN_CHAR_BUF_LEN; i++) cn_render_buf[i] = 0x00;
}
static inline void drv_clear_en_buf(void)
{
    for(int i = 0; i < EN_CHAR_BUF_LEN; i++) en_render_buf[i] = 0x00;
}

/**
 * @brief 渲染单个GB2312中文字符到指定坐标
 * @param x 屏幕X坐标
 * @param y 屏幕Y坐标
 * @param h 汉字高字节
 * @param l 汉字低字节
 * @return 1渲染成功，0失败
 */
int drv_draw_cn_char(int x, int y, unsigned char h, unsigned char l)
{
    // 边界校验，防止越界写入显存
    if(x < VRAM_BASE_X || y < VRAM_BASE_Y || (x + 16) > VRAM_MAX_X || (y + 16) > VRAM_MAX_Y)
        return 0;

    drv_clear_cn_buf();
    // 从编码字库获取16*16点阵
    int ret = chinese_encode_get_font(h, l, cn_render_buf);
    if(ret != 1)
        return 0;

    // 调用底层坐标写接口，写入显存
    block_write_pos(x, y, 16, 16, cn_render_buf);
    return 1;
}

/**
 * @brief 渲染单个ASCII英文字符到指定坐标
 * @param x 屏幕X坐标
 * @param y 屏幕Y坐标
 * @param code ASCII单字节编码
 * @return 1渲染成功，0失败
 */
int drv_draw_en_char(int x, int y, unsigned char code)
{
    if(x < VRAM_BASE_X || y < VRAM_BASE_Y || (x + 8) > VRAM_MAX_X || (y + 16) > VRAM_MAX_Y)
        return 0;

    drv_clear_en_buf();
    int ret = english_encode_get_font(code, en_render_buf);
    if(ret != 1)
        return 0;

    block_write_pos(x, y, 8, 16, en_render_buf);
    return 1;
}

/**
 * @brief 批量渲染字符串（自动区分中英文字符）
 * @param start_x 起始X坐标
 * @param start_y 起始Y坐标
 * @param str 字符串缓冲区（GB2312编码）
 * @param str_len 字符串长度
 * @param char_gap 字符横向间距
 */
void drv_draw_string(int start_x, int start_y, unsigned char *str, int str_len, int char_gap)
{
    int cur_x = start_x;
    int i = 0;
    while(i < str_len)
    {
        // 判断是否为GB2312双字节中文（0xA0以上为汉字）
        if(str[i] >= 0xA0 && (i + 1) < str_len)
        {
            drv_draw_cn_char(cur_x, start_y, str[i], str[i+1]);
            cur_x += 16 + char_gap;
            i += 2;
        }
        else
        {
            drv_draw_en_char(cur_x, start_y, str[i]);
            cur_x += 8 + char_gap;
            i += 1;
        }
    }
}

#endif
