// call_core.c 用户层调用核心
#ifndef CALL_CORE_H
#define CALL_CORE_H

// 引入下层驱动核心接口
extern int dev_core_dispatch(int cmd, unsigned char *param_buf, int param_len);

// 系统调用命令码定义
#define CALL_CMD_DRAW_TEXT    0x01  // 文字渲染
#define CALL_CMD_MEM_WRITE    0x02  // 内存批量写入
#define CALL_CMD_MEM_READ     0x03  // 内存批量读取
#define CALL_CMD_FONT_LOAD    0x04  // 加载字库
#define CALL_CMD_FILE_RW      0x05  // 文件读写
#define CALL_CMD_ARM_RUN      0x06  // 运行ARM程序

// 调用参数最大缓冲区
#define CALL_PARAM_BUF_MAX 1024
static unsigned char call_param_buf[CALL_PARAM_BUF_MAX];

// 清空调用参数缓冲区
static inline void call_buf_clear(void)
{
    for(int i = 0; i < CALL_PARAM_BUF_MAX; i++)
        call_param_buf[i] = 0x00;
}

/**
 * @brief 组装参数并发起系统调用
 * @param cmd 功能命令码
 * @param data 待组装参数数据
 * @param len 参数长度
 * @return 底层驱动返回值
 */
int call_core_invoke(int cmd, unsigned char *data, int len)
{
    if(len <= 0 || len > CALL_PARAM_BUF_MAX)
        return -1;
    call_buf_clear();
    // 拷贝参数到统一调用缓存
    for(int i = 0; i < len; i++)
        call_param_buf[i] = data[i];
    // 下发至驱动核心分发处理
    return dev_core_dispatch(cmd, call_param_buf, len);
}

/**
 * @brief 快捷渲染文字封装
 * @param x X坐标
 * @param y Y坐标
 * @param str 字符串缓冲区
 * @param str_len 字符串长度
 */
int call_core_draw_string(int x, int y, unsigned char *str, int str_len)
{
    struct {
        int x;
        int y;
        int len;
        unsigned char str[512];
    } draw_param;
    draw_param.x = x;
    draw_param.y = y;
    draw_param.len = str_len;
    for(int i = 0; i < str_len && i < 512; i++)
        draw_param.str[i] = str[i];
    return call_core_invoke(CALL_CMD_DRAW_TEXT, (unsigned char *)&draw_param, sizeof(draw_param));
}

/**
 * @brief 快捷内存批量写入调用
 * @param addr 目标内存地址
 * @param buf 数据缓冲区
 * @param len 写入长度
 */
int call_core_mem_write(unsigned int addr, unsigned char *buf, int len)
{
    struct {
        unsigned int addr;
        int len;
        unsigned char data[512];
    } mem_param;
    mem_param.addr = addr;
    mem_param.len = len;
    int copy_len = len > 512 ? 512 : len;
    for(int i = 0; i < copy_len; i++)
        mem_param.data[i] = buf[i];
    return call_core_invoke(CALL_CMD_MEM_WRITE, (unsigned char *)&mem_param, sizeof(mem_param));
}

#endif
