// sched_mgr.c 全局任务调度管理器
#ifndef SCHED_MGR_H
#define SCHED_MGR_H

// 任务优先级定义
#define TASK_PRIO_IDLE    0
#define TASK_PRIO_NORMAL  1
#define TASK_PRIO_HIGH    2
#define TASK_PRIO_CRIT    3

// 任务状态枚举
typedef enum {
    TASK_STAT_READY,
    TASK_STAT_RUN,
    TASK_STAT_BLOCK,
    TASK_STAT_SUSPEND,
    TASK_STAT_EXIT
} task_stat_t;

// 任务控制块TCB
typedef struct {
    unsigned int task_id;
    char task_name[32];
    int priority;
    task_stat_t status;
    unsigned int mem_base;
    unsigned int mem_size;
    // 任务入口函数指针
    void (*task_entry)(void);
} task_tcb_t;

// 最大同时运行任务数量
#define MAX_TASK_COUNT 16
static task_tcb_t task_pool[MAX_TASK_COUNT];
static int task_active_cnt = 0;
static unsigned int task_auto_id = 1;

// 清空单个TCB
static inline void tcb_clear(task_tcb_t *tcb)
{
    tcb->task_id = 0;
    tcb->priority = TASK_PRIO_IDLE;
    tcb->status = TASK_STAT_EXIT;
    tcb->mem_base = 0;
    tcb->mem_size = 0;
    tcb->task_entry = 0;
    for(int i=0; i<32; i++) tcb->task_name[i] = 0;
}

// 初始化调度管理器
void sched_mgr_init(void)
{
    task_active_cnt = 0;
    task_auto_id = 1;
    for(int i=0; i<MAX_TASK_COUNT; i++)
        tcb_clear(&task_pool[i]);
}

// 创建新任务，返回任务ID，0代表失败
unsigned int sched_task_create(const char *name, int prio, unsigned int mem_base, unsigned int mem_size, void (*entry)(void))
{
    if(task_active_cnt >= MAX_TASK_COUNT)
        return 0;
    int idx = task_active_cnt++;
    task_tcb_t *t = &task_pool[idx];
    t->task_id = task_auto_id++;
    t->priority = prio;
    t->status = TASK_STAT_READY;
    t->mem_base = mem_base;
    t->mem_size = mem_size;
    t->task_entry = entry;
    for(int i=0; name[i] && i<31; i++)
        t->task_name[i] = name[i];
    return t->task_id;
}

// 根据任务ID查找TCB指针
static task_tcb_t *sched_find_tcb(unsigned int tid)
{
    for(int i=0; i<task_active_cnt; i++)
    {
        if(task_pool[i].task_id == tid)
            return &task_pool[i];
    }
    return 0;
}

// 设置任务状态
int sched_task_set_stat(unsigned int tid, task_stat_t new_stat)
{
    task_tcb_t *t = sched_find_tcb(tid);
    if(!t) return 0;
    t->status = new_stat;
    return 1;
}

// 获取任务当前状态
task_stat_t sched_task_get_stat(unsigned int tid)
{
    task_tcb_t *t = sched_find_tcb(tid);
    if(!t) return TASK_STAT_EXIT;
    return t->status;
}

// 调度核心：按优先级选出最高就绪任务
int sched_switch(void)
{
    int best_idx = -1;
    int best_prio = -1;
    for(int i=0; i<task_active_cnt; i++)
    {
        task_tcb_t *t = &task_pool[i];
        if(t->status == TASK_STAT_READY && t->priority > best_prio)
        {
            best_prio = t->priority;
            best_idx = i;
        }
    }
    if(best_idx == -1)
        return -1;
    // 切换为运行态
    task_tcb_t *run_t = &task_pool[best_idx];
    run_t->status = TASK_STAT_RUN;
    // 执行任务入口
    run_t->task_entry();
    return run_t->task_id;
}

// 销毁任务，回收TCB资源
int sched_task_destroy(unsigned int tid)
{
    for(int i=0; i<task_active_cnt; i++)
    {
        if(task_pool[i].task_id == tid)
        {
            tcb_clear(&task_pool[i]);
            // 简单回收（末尾前移压缩）
            for(int j=i; j<task_active_cnt-1; j++)
                task_pool[j] = task_pool[j+1];
            task_active_cnt--;
            return 1;
        }
    }
    return 0;
}

#endif
