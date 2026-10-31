// dst_router.c 转发目的地路由管理模块
#ifndef DST_ROUTER_H
#define DST_ROUTER_H

#include <stdint.h>
#include <string.h>

// 最大可注册转发目标
#define MAX_DST_TARGET 16
// 单次转发数据缓冲区
#define DST_TRANS_BUF 16384

// 目的地类型枚举
typedef enum {
    DST_TYPE_TASK_PID,    // 转发至指定任务进程
    DST_TYPE_SCHED_CORE,  // 转发至全局调度核心
    DST_TYPE_CACHE_POOL   // 转发至接收缓存池
} dst_type_t;

// 转发目的地描述结构体
typedef struct {
    uint32_t dst_id;
    dst_type_t dst_type;
    uint32_t target_val; // PID/内核句柄/缓存ID
    uint8_t enable;
    uint64_t forward_total; // 累计转发字节统计
} dst_target_t;

// 全局目的地池
static dst_target_t dst_pool[MAX_DST_TARGET];
static int dst_count = 0;
static unsigned char trans_buf[DST_TRANS_BUF];

static inline void dst_pool_reset(void)
{
    dst_count = 0;
    memset(trans_buf, 0, DST_TRANS_BUF);
    for(int i = 0; i < MAX_DST_TARGET; i++)
    {
        dst_pool[i].dst_id = 0;
        dst_pool[i].dst_type = DST_TYPE_SCHED_CORE;
        dst_pool[i].target_val = 0;
        dst_pool[i].enable = 0;
        dst_pool[i].forward_total = 0;
    }
}

// 底层系统转发调用
extern int sys_forward_to_pid(uint32_t pid, unsigned char *data, uint32_t len);
extern int sys_forward_to_sched_core(unsigned char *data, uint32_t len);
extern int cache_write_block(uint32_t block_id, unsigned char *ins, uint32_t ins_len);

/**
 * @brief 注册新转发目的地
 * @param dst_id 唯一目标ID
 * @param type 目标类型
 * @param target 目标标识(PID/句柄/缓存ID)
 * @return 1注册成功，0池满/ID重复
 */
int dst_register_target(uint32_t dst_id, dst_type_t type, uint32_t target)
{
    // 查重
    for(int i = 0; i < dst_count; i++)
        if(dst_pool[i].dst_id == dst_id) return 0;
    if(dst_count >= MAX_DST_TARGET) return 0;

    dst_target_t *t = &dst_pool[dst_count++];
    t->dst_id = dst_id;
    t->dst_type = type;
    t->target_val = target;
    t->enable = 1;
    t->forward_total = 0;
    return 1;
}

/**
 * @brief 按目标ID转发X86指令流
 * @param dst_id 注册好的目的地ID
 * @param x86_data 待转发二进制指令
 * @param data_len 指令长度
 * @return 实际转发字节，0失败
 */
uint32_t dst_forward_data(uint32_t dst_id, unsigned char *x86_data, uint32_t data_len)
{
    // 查找目标
    int idx = -1;
    for(int i = 0; i < dst_count; i++)
    {
        if(dst_pool[i].dst_id == dst_id && dst_pool[i].enable)
        {
            idx = i;
            break;
        }
    }
    if(idx == -1) return 0;

    dst_target_t *target = &dst_pool[idx];
    uint32_t send_len = data_len > DST_TRANS_BUF ? DST_TRANS_BUF : data_len;
    memcpy(trans_buf, x86_data, send_len);
    int ret = 0;

    // 根据目标类型分发
    switch(target->dst_type)
    {
        case DST_TYPE_TASK_PID:
            ret = sys_forward_to_pid(target->target_val, trans_buf, send_len);
            break;
        case DST_TYPE_SCHED_CORE:
            ret = sys_forward_to_sched_core(trans_buf, send_len);
            break;
        case DST_TYPE_CACHE_POOL:
            ret = cache_write_block(target->target_val, trans_buf, send_len);
            break;
        default:
            return 0;
    }
    if(ret > 0) target->forward_total += ret;
    return ret;
}

/**
 * @brief 禁用指定转发目标
 * @param dst_id 目标ID
 * @return 1操作成功
 */
int dst_disable_target(uint32_t dst_id)
{
    for(int i = 0; i < dst_count; i++)
    {
        if(dst_pool[i].dst_id == dst_id)
        {
            dst_pool[i].enable = 0;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 获取目标累计转发流量统计
 * @param dst_id 目标ID
 * @return 累计转发字节数
 */
uint64_t dst_get_forward_stat(uint32_t dst_id)
{
    for(int i = 0; i < dst_count; i++)
    {
        if(dst_pool[i].dst_id == dst_id)
            return dst_pool[i].forward_total;
    }
    return 0;
}

/**
 * @brief 清空全部转发目的地配置（系统重置）
 */
void dst_clear_all_target(void)
{
    dst_pool_reset();
}

#endif
