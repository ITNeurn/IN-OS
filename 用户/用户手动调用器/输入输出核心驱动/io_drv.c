// io_drv.c 输入输出核心驱动
#ifndef IO_DRV_H
#define IO_DRV_H

// 依赖上层驱动分发接口
extern int dev_core_dispatch(int cmd, unsigned char *param_buf, int param_len);
// 依赖字库字符串渲染接口
extern int drv_draw_string(int start_x, int start_y, unsigned char *str, int str_len, int char_gap);

// IO缓冲区规格
#define IO_INPUT_BUF_MAX 256
#define IO_OUTPUT_BUF_MAX 512
static unsigned char input_buf[IO_INPUT_BUF_MAX];
static unsigned char output_buf[IO_OUTPUT_BUF_MAX];
static int input_len = 0;

// 清空输入缓冲区
static inline void io_clear_input(void)
{
    input_len = 0;
    for(int i = 0; i < IO_INPUT_BUF_MAX; i++)
        input_buf[i] = 0x00;
}

// 清空输出缓冲区
static inline void io_clear_output(void)
{
    for(int i = 0; i < IO_OUTPUT_BUF_MAX; i++)
        output_buf[i] = 0x00;
}

/**
 * @brief 读取键盘单字符输入
 * @return ASCII字符，0无输入
 */
unsigned char io_get_key(void)
{
    unsigned char key = 0;
    // 调用底层端口读取
    dev_core_dispatch(0x10, &key, 1);
    if(key != 0 && input_len < IO_INPUT_BUF_MAX)
    {
        input_buf[input_len++] = key;
    }
    return key;
}

/**
 * @brief 获取整行输入字符串
 * @param out_str 输出缓冲区
 * @return 字符串有效长度
 */
int io_get_line(unsigned char *out_str)
{
    io_clear_input();
    unsigned char ch;
    while(1)
    {
        ch = io_get_key();
        if(ch == '\r' || ch == '\n' || input_len >= IO_INPUT_BUF_MAX)
            break;
    }
    for(int i = 0; i < input_len; i++)
        out_str[i] = input_buf[i];
    return input_len;
}

/**
 * @brief 控制台输出字符串（自动渲染到屏幕）
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param str 输出字符串
 * @param str_len 字符串长度
 * @param gap 字符间距
 */
void io_print(int x, int y, unsigned char *str, int str_len, int gap)
{
    io_clear_output();
    int copy_len = str_len > IO_OUTPUT_BUF_MAX ? IO_OUTPUT_BUF_MAX : str_len;
    for(int i = 0; i < copy_len; i++)
        output_buf[i] = str[i];
    // 调用字库渲染接口绘制文本
    drv_draw_string(x, y, output_buf, copy_len, gap);
}

/**
 * @brief 清空控制台指定行区域
 * @param y 行起始Y坐标
 * @param line_h 行高度
 * @param line_w 行宽度
 */
void io_clear_line(int y, int line_h, int line_w)
{
    unsigned char zero_buf[1024] = {0};
    block_write_pos(0, y, line_w, line_h, zero_buf);
}

#endif
