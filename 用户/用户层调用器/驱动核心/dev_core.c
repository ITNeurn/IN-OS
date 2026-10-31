// dev_core.c 用户层驱动分发核心
#ifndef DEV_CORE_H
#define DEV_CORE_H

// 上层调用核心外部声明
extern int call_core_invoke(int cmd, unsigned char *data, int len);

// 引入各下层功能模块接口
// 字库渲染
extern int drv_draw_string(int start_x, int start_y, unsigned char *str, int str_len, int gap);
// 高速内存读写
extern int mem_flush_all_pages(unsigned int target_base_addr);
extern int mem_load_pages(unsigned int src_base_addr, int total_load_len);
// 高速文件/显存读写
extern int fast_flush_to_mem(unsigned int target_addr);
extern int fast_load_from_mem(unsigned int src_addr, int load_len);
// 翻译层ARM转译接口
extern int arm2x86_submit(unsigned char *arm_bin, int bin_len);

// 与调用核心统一命令码
#define CALL_CMD_DRAW_TEXT    0x01
#define CALL_CMD_MEM_WRITE    0x02
#define CALL_CMD_MEM_READ     0x03
#define CALL_CMD_FONT_LOAD    0x04
#define CALL_CMD_FILE_RW      0x05
#define CALL_CMD_ARM_RUN      0x06

// 最大参数缓冲区
#define DEV_PARAM_BUF 1024
static unsigned char dev_param_buf[DEV_PARAM_BUF];

static inline void dev_buf_clear(void)
{
    for(int i = 0; i < DEV_PARAM_BUF; i++)
        dev_param_buf[i] = 0;
}

/**
 * @brief 指令分发入口，接收调用核心下发请求
 * @param cmd 功能命令码
 * @param param_buf 指令参数数据
 * @param param_len 参数长度
 * @return 执行结果 1成功 / 0失败 / -1未知指令
 */
int dev_core_dispatch(int cmd, unsigned char *param_buf, int param_len)
{
    if(param_len <= 0 || param_len > DEV_PARAM_BUF)
        return -1;
    dev_buf_clear();
    // 拷贝参数至本地缓存
    for(int i = 0; i < param_len; i++)
        dev_param_buf[i] = param_buf[i];

    switch(cmd)
    {
        case CALL_CMD_DRAW_TEXT:
        {
            typedef struct {
                int x, y, len, gap;
                unsigned char str[512];
            } draw_param_t;
            draw_param_t *p = (draw_param_t *)dev_param_buf;
            return drv_draw_string(p->x, p->y, p->str, p->len, p->gap);
        }
        case CALL_CMD_MEM_WRITE:
        {
            typedef struct {
                unsigned int addr;
                int len;
            } mem_param_t;
            mem_param_t *p = (mem_param_t *)dev_param_buf;
            return mem_flush_all_pages(p->addr);
        }
        case CALL_CMD_MEM_READ:
        {
            typedef struct {
                unsigned int addr;
                int len;
            } mem_param_t;
            mem_param_t *p = (mem_param_t *)dev_param_buf;
            return mem_load_pages(p->addr, p->len);
        }
        case CALL_CMD_FILE_RW:
        {
            typedef struct {
                unsigned int addr;
                int len;
                int is_write;
            } file_param_t;
            file_param_t *p = (file_param_t *)dev_param_buf;
            if(p->is_write)
                return fast_flush_to_mem(p->addr);
            else
                return fast_load_from_mem(p->addr, p->len);
        }
        case CALL_CMD_ARM_RUN:
        {
            typedef struct {
                int bin_len;
                unsigned char bin_data[512];
            } arm_param_t;
            arm_param_t *p = (arm_param_t *)dev_param_buf;
            return arm2x86_submit(p->bin_data, p->bin_len);
        }
        default:
            return -1;
    }
}

#endif
