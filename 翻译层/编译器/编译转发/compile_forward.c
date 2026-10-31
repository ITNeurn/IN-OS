// compile_forward.c 编译任务转发调度模块
#ifndef COMPILE_FORWARD_H
#define COMPILE_FORWARD_H

#include <stdint.h>
#include <string.h>

// 下层依赖：编译核心、缓存管理器
extern int cache_mgr_compile(uint32_t seg_id, int arch, unsigned char *ir_data, int ir_len, unsigned char *out_bin, uint32_t *out_len);
extern int cache_mgr_init(const char *cache_path);

// 编译任务类型
typedef enum {
    TASK_COMPILE_SINGLE,
    TASK_COMPILE_BATCH
} compile_task_type_t;

// 转发任务结构体
typedef struct {
    compile_task_type_t task_type;
    uint32_t seg_id;
    int target_arch;
    unsigned int ir_len;
    unsigned char ir_buf[4096];
} compile_task_t;

// 最大任务缓存池
#define MAX_FORWARD_TASK 64
static compile_task_t task_pool[MAX_FORWARD_TASK];
static int task_cnt = 0;
static char cache_path[128];

static inline void forward_mgr_reset(void)
{
    task_cnt = 0;
    for(int i = 0; i < MAX_FORWARD_TASK; i++)
    {
        task_pool[i].task_type = TASK_COMPILE_SINGLE;
        task_pool[i].seg_id = 0;
        task_pool[i].target_arch = 0;
        task_pool[i].ir_len = 0;
        memset(task_pool[i].ir_buf, 0, 4096);
    }
}

/**
 * @brief 初始化转发模块，绑定缓存路径
 * @param path 编译缓存文件路径
 * @return 缓存初始化结果
 */
int forward_init(const char *path)
{
    strcpy(cache_path, path);
    forward_mgr_reset();
    return cache_mgr_init(cache_path);
}

/**
 * @brief 新增单段编译任务
 * @param seg_id 分段编号
 * @param arch 目标架构
 * @param ir_data 中间指令数据
 * @param ir_len 数据长度
 * @return 1任务入队成功，0任务池已满
 */
int forward_add_single_task(uint32_t seg_id, int arch, unsigned char *ir_data, int ir_len)
{
    if(task_cnt >= MAX_FORWARD_TASK)
        return 0;
    compile_task_t *task = &task_pool[task_cnt++];
    task->task_type = TASK_COMPILE_SINGLE;
    task->seg_id = seg_id;
    task->target_arch = arch;
    task->ir_len = ir_len > 4096 ? 4096 : ir_len;
    memcpy(task->ir_buf, ir_data, task->ir_len);
    return 1;
}

/**
 * @brief 批量执行所有排队编译任务
 * @param out_result_bin 输出合并二进制缓冲区
 * @param out_total_len 输出总长度
 * @return 成功执行任务数量
 */
int forward_run_all_tasks(unsigned char *out_result_bin, uint32_t *out_total_len)
{
    int success_cnt = 0;
    int total_write = 0;
    unsigned char temp_bin[4096];
    uint32_t bin_len;
    for(int i = 0; i < task_cnt; i++)
    {
        compile_task_t *tk = &task_pool[i];
        if(cache_mgr_compile(tk->seg_id, tk->target_arch, tk->ir_buf, tk->ir_len, temp_bin, &bin_len))
        {
            memcpy(out_result_bin + total_write, temp_bin, bin_len);
            total_write += bin_len;
            success_cnt++;
        }
    }
    *out_total_len = total_write;
    return success_cnt;
}

/**
 * @brief 清空所有排队任务
 */
void forward_clear_tasks(void)
{
    forward_mgr_reset();
}

#endif
