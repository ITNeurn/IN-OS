// backup_display_drv.c 备用简易显示驱动
#ifndef BACKUP_DISPLAY_H
#define BACKUP_DISPLAY_H

// 外部依赖底层字符点阵接口
extern int chinese_encode_get_font(unsigned char h, unsigned char l, unsigned char *dot_buf);
extern int english_encode_get_font(unsigned char code, unsigned char *dot_buf);
// 底层显存坐标写入接口
extern void block_write_pos(int x, int y, int w, int h, unsigned char *buf);

// 备用驱动显存参数（低分辨率应急模式）
#define BACKUP_VRAM_MAX_X 640
#define BACKUP_VRAM_MAX_Y 480
// 字符缓冲区
#define BACKUP_CN_BUF (16*16/8)
#define BACKUP_EN_BUF 16
static unsigned char cn_buf[BACKUP_CN_BUF];
static unsigned char en_buf[BACKUP_EN_BUF];

static inline void backup_clear_cn(void)
{
    for(int i=0;i<BACKUP_CN_BUF;i++) cn_buf[i] = 0;
}
static inline void backup_clear_en(void)
{
    for(int i=0;i<BACKUP_EN_BUF;i++) en_buf[i] = 0;
}

// 校验坐标是否越界
static inline int backup_pos_check(int x, int y, int w, int h)
{
    if(x < 0 || y < 0 || (x + w) > BACKUP_VRAM_MAX_X || (y + h) > BACKUP_VRAM_MAX_Y)
        return 0;
    return 1;
}

// 渲染单个中文（16*16）
int backup_draw_cn(int x, int y, unsigned char h, unsigned char l)
{
    if (!backup_pos_check(x,y,16,16)) return 0;
    backup_clear_cn();
    int ret = chinese_encode_get_font(h,l,cn_buf);
    if (ret != 1) return 0;
    block_write_pos(x, y, 16, 16, cn_buf);
    return 1;
}

// 渲染单个英文（8*16）
int backup_draw_en(int x, int y, unsigned char code)
{
    if (!backup_pos_check(x,y,8,16)) return 0;
    backup_clear_en();
    int ret = english_encode_get_font(code, en_buf);
    if (ret != 1) return 0;
    block_write_pos(x, y, 8, 16, en_buf);
    return 1;
}

// 批量渲染混合中英字符串（应急日志专用）
void backup_draw_text(int start_x, int start_y, unsigned char *str, int len, int gap)
{
    int cur_x = start_x;
    int i = 0;
    while(i < len)
    {
        if (str[i] >= 0xA0 && i+1 < len)
        {
            backup_draw_cn(cur_x, start_y, str[i], str[i+1]);
            cur_x += 16 + gap;
            i += 2;
        }
        else
        {
            backup_draw_en(cur_x, start_y, str[i]);
            cur_x += 8 + gap;
            i += 1;
        }
    }
}

// 清空单行屏幕区域
void backup_clear_line(int y, int line_w)
{
    unsigned char empty[1024] = {0};
    block_write_pos(0, y, line_w, 16, empty);
}

// 检测主显示驱动是否故障（供上层切换判断）
int backup_check_main_drv(void)
{
    // 预留对接系统状态检测接口，返回1主驱动失效，0正常
    return 0;
}

#endif
