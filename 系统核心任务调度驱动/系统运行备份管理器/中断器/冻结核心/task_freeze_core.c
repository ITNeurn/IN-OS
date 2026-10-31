// task_freeze_core.c 任务冻结核心
#ifndef TASK_FREEZE_CORE_H
#define TASK_FREEZE_CORE_H

// 任务状态枚举
#define TASK_STATUS_RUNNING 0
#define TASK_STATUS_FROZEN 1
#define TASK_STATUS_TIMEOUT 2
#define TASK_STATUS_ERROR 3

// 冻结任务上下文
typedef struct {
    unsigned int pid;
    unsigned long freeze_tick;
    unsigned int timeout_ms;
    int status;
} freeze_ctx_t;

// 最大同时冻结任务池
#define MAX_FREEZE_TASK 32
static freeze_ctx_t freeze_pool[MAX_FREEZE_TASK];
static int freeze_cnt = 0;

static inline void freeze_pool_clear(void)
{
    freeze_cnt = 0;
    for(int i=0; i<MAX_FREEZE_TASK; i++)
    {
        freeze_pool[i].pid = 0;
        freeze_pool[i].freeze_tick = 0;
        freeze_pool[i].timeout_ms = 0;
        freeze_pool[i].status = TASK_STATUS_RUNNING;
    }
}

// 底层调度中断接口（内核调用）
extern int sys_interrupt_task(unsigned int pid);
extern int sys_resume_task(unsigned int pid);
extern unsigned long sys_get_tick(void);
extern int sys_task_is_running(unsigned int pid);

/**
 * @brief 冻结指定任务，发送调度中断暂停执行
 * @param pid 目标任务PID
 * @param timeout 冻结超时毫秒，0永久冻结
 * @return 1冻结成功，0任务不存在/已冻结
 */
int freeze_task(unsigned int pid, unsigned int timeout)
{
    // 查重已冻结任务
    for(int i=0; i<freeze_cnt; i++)
        if(freeze_pool[i].pid == pid) return 0;
    if(!sys_task_is_running(pid)) return 0;
    // 发送中断阻塞任务
    if(!sys_interrupt_task(pid)) return 0;
    // 保存冻结上下文
    freeze_ctx_t *ctx = &freeze_pool[freeze_cnt++];
    ctx->pid = pid;
    ctx->freeze_tick = sys_get_tick();
    ctx->timeout_ms = timeout;
    ctx->status = TASK_STATUS_FROZEN;
    return 1;
}

/**
 * @brief 解冻恢复任务运行
 * @param pid 任务PID
 * @return 1恢复成功，0未找到冻结记录
 */
int unfreeze_task(unsigned int pid)
{
    int idx = -1;
    for(int i=0; i<freeze_cnt; i++)
    {
        if(freeze_pool[i].pid == pid)
        {
            idx = i;
            break;
        }
    }
    if(idx == -1) return 0;
    // 内核恢复调度
    sys_resume_task(pid);
    // 移除冻结条目
    for(int i=idx; i<freeze_cnt-1; i++)
        freeze_pool[i] = freeze_pool[i+1];
    freeze_cnt--;
    return 1;
}

/**
 * @brief 批量检测超时冻结任务，自动解冻
 * @return 自动解冻任务数量
 */
int freeze_check_timeout(void)
{
    int auto_unfreeze = 0;
    unsigned long now = sys_get_tick();
    for(int i=0; i<freeze_cnt; i++)
    {
        freeze_ctx_t *ctx = &freeze_pool[i];
        if(ctx->timeout_ms == 0) continue;
        unsigned long tick_diff = now - ctx->freeze_tick;
        if(tick_diff >= ctx->timeout_ms)
        {
            unfreeze_task(ctx->pid);
            auto_unfreeze++;
            i--;
        }
    }
    return auto_unfreeze;
}

/**
 * @brief 查询任务当前冻结状态
 * @param pid 任务PID
 * @return 状态码 TASK_STATUS_*
 */
int freeze_get_task_status(unsigned int pid)
{
    for(int i=0; i<freeze_cnt; i++)
    {
        if(freeze_pool[i].pid == pid)
            return freeze_pool[i].status;
    }
    return TASK_STATUS_RUNNING;
}

/**
 * @brief 批量全部解冻（快照完成后统一释放）
 */
void freeze_unfreeze_all(void)
{
    for(int i=0; i<freeze_cnt; i++)
        sys_resume_task(freeze_pool[i].pid);
    freeze_pool_clear();
}

#endif
