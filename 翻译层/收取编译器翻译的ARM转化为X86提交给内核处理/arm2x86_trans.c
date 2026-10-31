// arm2x86_trans.c ARM机器码转X86分发提交内核模块
#ifndef ARM2X86_TRANS_H
#define ARM2X86_TRANS_H

#include <stdint.h>
#include <string.h>

// 下层编译转发接口
extern int forward_run_all_tasks(unsigned char *out_bin, uint32_t *out_total_len);
extern int forward_init(const char *cache_path);
extern int forward_add_single_task(uint32_t seg_id, int arch, unsigned char *ir_data, int ir_len);

// 内核底层执行提交系统调用
extern int sys_submit_exec_bin(uint32_t pid, unsigned char *bin_data, uint32_t bin_len);

// 翻译缓存缓冲区
#define TRANS_BUF_MAX 16384
static unsigned char trans_buf[TRANS_BUF_MAX];
static uint32_t target_task_pid = 0;

static inline void trans_buf_clear(void)
{
    memset(trans_buf, 0, TRANS_BUF_MAX);
}

/**
 * @brief 设置目标运行任务PID
 * @param pid 系统运行进程PID
 */
void trans_set_target_pid(uint32_t pid)
{
    target_task_pid = pid;
}

/**
 * @brief 初始化翻译转发流水线
 * @param cache_path 编译缓存文件路径
 * @return 缓存初始化结果
 */
int trans_init_pipeline(const char *cache_path)
{
    trans_buf_clear();
    return forward_init(cache_path);
}

/**
 * @brief 送入ARM中间代码编译任务
 * @param seg_id 代码分段编号
 * @param ir_data ARM中间指令数据
 * @param ir_len 数据长度
 * @return 1任务入队成功
 */
int trans_input_arm_ir(uint32_t seg_id, unsigned char *ir_data, int ir_len)
{
    // 送入编译器，目标架构标记ARM
    return forward_add_single_task(seg_id, ARCH_ARM, ir_data, ir_len);
}

/**
 * @brief 执行批量编译，将ARM二进制转译X86并提交内核
 * @return 提交内核成功字节总数，0无有效代码
 */
int trans_convert_and_submit(void)
{
    trans_buf_clear();
    uint32_t total_bin_len = 0;
    // 运行编译流水线，输出ARM原生二进制
    int task_ok = forward_run_all_tasks(trans_buf, &total_bin_len);
    if(task_ok == 0 || total_bin_len == 0)
        return 0;

    // ARM → X86 二进制转译（内置跨架构转换）
    extern int arm_to_x86_convert(unsigned char *arm_bin, uint32_t arm_len, unsigned char *x86_out, uint32_t *x86_len);
    unsigned char x86_buf[TRANS_BUF_MAX];
    uint32_t x86_final_len = 0;
    int convert_ok = arm_to_x86_convert(trans_buf, total_bin_len, x86_buf, &x86_final_len);
    if(!convert_ok || x86_final_len == 0)
        return 0;

    // 向系统内核提交X86可执行镜像
    int submit_len = sys_submit_exec_bin(target_task_pid, x86_buf, x86_final_len);
    return submit_len;
}

/**
 * @brief 清空当前流水线所有编译任务
 */
void trans_pipeline_clear(void)
{
    extern void forward_clear_tasks(void);
    forward_clear_tasks();
    trans_buf_clear();
}

#endif
