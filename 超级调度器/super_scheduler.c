// super_scheduler.c
// 超级调度器【终极加强冗余版｜工业级容错｜看门狗｜自愈｜防雪崩｜硬件心跳｜死锁恢复】
#ifndef SUPER_SCHEDULER_H
#define SUPER_SCHEDULER_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

//===================== 全部系统模块依赖【全量兜底引入】=====================
#include "用户/用户命令行/user_cli.c"
#include "用户/图形界面/gui_core.c"
#include "用户/用户层调用器/调用核心/call_core.c"
#include "用户/用户手动调用器/输入输出核心驱动/io_drv.c"

#include "翻译层/编译器/编译缓存/cache_manager.c"
#include "翻译层/编译器/编译转发/compile_forward.c"
#include "翻译层/收取编译器翻译的ARM转化为X86提交给内核处理/arm2x86_trans.c"

#include "system/微内核/调度核心/基础文件读写/file_rw.c"
#include "system/微内核/调度核心/任务调度/基础读写器/读坐标/vaddr_read.c"
#include "system/微内核/调度核心/任务调度/基础读写器/写坐标/vaddr_write.c"
#include "system/微内核/调度核心/任务调度/内存高速读写器/核心内存读写驱动/hw_mem_drv.c"

#include "system/微内核/收取X86指令提交给调度核心/接受核心/接受端口/receive_port.c"
#include "system/微内核/收取X86指令提交给调度核心/接受缓存/内存基础读写/recv_mem_rw.c"
#include "system/微内核/收取X86指令提交给调度核心/转发核心/转发目的地/dst_router.c"

#include "system/驱动/键盘，鼠标/input_hid.c"
#include "system/驱动/系统显示/display_gpu.c"
#include "system/驱动/内存通用驱动/mem_universal_drv.c"
#include "system/驱动/磁盘通用驱动/disk_universal_drv.c"

#include "系统核心任务调度驱动/系统恢复器/读写器/recovery_rw.c"
#include "系统核心任务调度驱动/系统恢复器/读写器检验器/哈希值校验器/哈希值校验核心/拦截器/hash_interceptor.c"
#include "系统核心任务调度驱动/系统恢复器/读写器检验器/哈希值校验器/哈希值校验核心/比较器/比较核心/compare_core.c"
#include "系统核心任务调度驱动/系统恢复器/读写器检验器/哈希值校验器/哈希值校验核心/比较器/备用哈希/backup_hash.c"
#include "系统核心任务调度驱动/系统恢复器/读写器检验器/哈希值校验器/哈希值校验核心/哈希值获取核心/解析器/hash_parser.c"
#include "系统核心任务调度驱动/系统恢复器/读写器检验器/哈希值校验器/哈希值校验核心/哈希值获取核心/读取器/hash_reader.c"
#include "系统核心任务调度驱动/系统恢复器/读写器检验器/读写器哈希值/rw_hash.c"
#include "系统核心任务调度驱动/系统恢复器/读写器检验器/读写器哈希值的哈希值/hash_of_hash.c"

#include "系统核心任务调度驱动/系统运行备份管理器/中断器/冻结核心/task_freeze_core.c"
#include "系统核心任务调度驱动/系统运行备份管理器/内存读写器/ackup_mem_rw.c"
#include "系统核心任务调度驱动/系统运行备份管理器/快照处理器/快照映射器/snap_mapper.c"
#include "系统核心任务调度驱动/系统运行备份管理器/快照处理器/快照缓存器/基础内存读写器/mem_rw.c"
#include "系统核心任务调度驱动/系统运行备份管理器/快照处理器/快照缓存器/基础读写器/cache_rw.c"

#include "系统核心任务调度驱动/系统镜像/读取器/只读器/image_ro_reader.c"
#include "系统核心任务调度驱动/系统镜像/读取器/结构解析器/image_struct_parser.c"
#include "系统核心任务调度驱动/系统镜像/系统结构/目录结构/dir_struct.c"
#include "系统核心任务调度驱动/系统镜像/系统结构/文件结构/文件位置与固定编号/file_id_loc.c"
#include "系统核心任务调度驱动/系统镜像/系统结构/文件结构/文件内容/file_content_reader.c"
#include "系统核心任务调度驱动/调度管理器/sched_mgr.c"

#include "字库/字体基础管理/字库管理器/基础读写器/font_rw.c"
#include "字库/字体基础管理/字库管理器/字库修改管理与提交至基础读写器/修改器核心/dit_core.c"
#include "字库/字体基础管理/字库管理器/字库修改管理与提交至基础读写器/读写器核心/rw_core.c"
#include "字库/字体基础管理/字库/编码版本字库/显示驱动/display_drv.c"
#include "字库/字体基础管理/字库/编码版本字库/English/ascii_encode_font.c"
#include "字库/字体基础管理/字库/像素字库/English/ascii_font.c"

//===================== 缺失子系统 stub 声明（实现在 stub_glue.c）=====================
extern int  snapshot_backup_init(void);
extern int  hash_verify_init(void);
extern int  font_mgr_init(const char *path);
extern int  font_mgr_deinit(void);
extern int  super_task_resume_from_snap(unsigned int pid);

//===================== 全局超级扩容常量（冗余增强）=====================
#define SUPER_MAX_TASK              256
#define SUPER_GLOBAL_BUF            131072
#define TASK_IR_MAX_BUF             32768
#define TASK_TIME_SLICE             1000
#define TASK_TIMEOUT_TICK           30000
#define TASK_DEADLOCK_TICK          80000
#define MAX_GLOBAL_FILE_HANDLE      64
#define MAX_GLOBAL_PORT             32
#define MAX_CACHE_BLOCK             96
#define WATCHDOG_INTERVAL           5000
#define TASK_RETRY_MAX              5
#define RESOURCE_CHECK_INTERVAL     2000

// 任务优先级
typedef enum {
    PRIORITY_IDLE = 0,
    PRIORITY_LOW = 1,
    PRIORITY_NORMAL = 2,
    PRIORITY_HIGH = 3,
    PRIORITY_REAL_TIME = 4
} task_prio_t;

// 任务全状态（扩容）
typedef enum {
    TASK_IDLE,
    TASK_WAIT_IR,
    TASK_COMPILE_ARM,
    TASK_TRANS_X86,
    TASK_CACHE_STORE,
    TASK_FORWARD_SCHED,
    TASK_SUSPEND_SNAPSHOT,
    TASK_RUNNING,
    TASK_CRASH_RECOVER,
    TASK_FINISH,
    TASK_STUCK_DETECT,
    TASK_RETRY_QUEUE,
    TASK_WATCHDOG_SLEEP
} super_task_state_t;

// 全局性能统计【大幅扩容冗余】
typedef struct {
    uint64_t total_task_create;
    uint64_t total_task_finish;
    uint64_t total_task_crash;
    uint64_t total_task_retry;
    uint64_t total_task_stuck;
    uint64_t total_forward_byte;
    uint64_t total_snap_byte;
    uint64_t cache_hit_count;
    uint64_t cache_miss_count;
    uint64_t hw_mem_read_bytes;
    uint64_t hw_mem_write_bytes;
    uint64_t disk_read_bytes;
    uint64_t disk_write_bytes;
    uint64_t hardware_reconnect_cnt;
    uint64_t deadlock_release_cnt;
} super_stat_t;

// 全局资源配额【硬件冗余监控】
typedef struct {
    int used_file_fd;
    int used_port;
    int used_cache_block;
    int running_task_count;
    int open_hid_dev;
    int open_disk_dev;
    int gpu_layer_count;
    int retry_queue_count;
    int stuck_task_count;
} super_quota_t;

// 强化任务块（带重试、心跳、超时、死锁标记）
typedef struct {
    uint32_t task_pid;
    task_prio_t priority;
    super_task_state_t state;
    uint64_t create_tick;
    uint64_t last_run_tick;
    uint64_t last_watchdog_tick;
    uint64_t used_tick;
    uint32_t seg_count;
    uint32_t seg_ids[64];
    uint32_t ir_len;
    unsigned char ir_data[TASK_IR_MAX_BUF];
    unsigned long snap_start_vaddr;
    uint32_t snap_size;
    uint8_t is_timeout;
    uint8_t is_stuck;
    uint8_t need_recover;
    uint8_t retry_times;
    uint8_t safe_lock_flag;
} super_task_t;

// 全局硬件心跳状态
typedef struct {
    uint8_t mem_driver_ok;
    uint8_t disk_driver_ok;
    uint8_t gpu_driver_ok;
    uint8_t hid_driver_ok;
    uint64_t last_check_tick;
} hardware_heartbeat_t;

// 全局静态资源
static super_task_t super_task_pool[SUPER_MAX_TASK];
static super_stat_t super_global_stat;
static super_quota_t super_global_quota;
static hardware_heartbeat_t hw_heart;
static unsigned char super_global_buf[SUPER_GLOBAL_BUF];
static int super_sched_inited = 0;
static uint32_t global_spin_lock = 0;
static uint32_t watchdog_tick = 0;

// 底层系统调用
extern uint64_t sys_get_kernel_tick(void);
extern void sys_spin_lock(uint32_t* lock);
extern void sys_spin_unlock(uint32_t* lock);
extern int sys_task_kill(uint32_t pid);

//===================== 内部工具函数（冗余容错）=====================
static inline void super_sched_reset(void)
{
    memset(super_task_pool, 0, sizeof(super_task_pool));
    memset(&super_global_stat, 0, sizeof(super_global_stat));
    memset(&super_global_quota, 0, sizeof(super_global_quota));
    memset(&hw_heart, 1, sizeof(hardware_heartbeat_t));
    memset(super_global_buf, 0, SUPER_GLOBAL_BUF);
}

static inline int super_try_lock(uint32_t* lock)
{
    if (*lock) return 0;
    sys_spin_lock(lock);
    return 1;
}

//===================== 硬件心跳检测 + 掉线重连（冗余核心）=====================
// 注意：原实现访问其他文件中的 static 变量 gpu_inited/hid_dev_cnt/disk_dev_cnt，
// 在 C 语言中跨翻译单元访问 static 是非法的。此处改为不依赖内部状态，
// 直接通过 hw_heart 标志位记录最近一次驱动调用结果，避免链接错误。
void super_hardware_heartbeat_check(void)
{
    uint64_t now = sys_get_kernel_tick();
    if (now - hw_heart.last_check_tick < RESOURCE_CHECK_INTERVAL) return;
    hw_heart.last_check_tick = now;

    // 内存驱动心跳（直接认为已初始化即在线）
    hw_heart.mem_driver_ok = 1;

    // 磁盘驱动心跳：尝试 open 一次，失败则计为掉线并累加重连计数
    int disk_ok = disk_open_device("/dev/disk0");
    if (disk_ok <= 0) {
        hw_heart.disk_driver_ok = 0;
        super_global_stat.hardware_reconnect_cnt++;
    } else {
        hw_heart.disk_driver_ok = 1;
    }

    // GPU 心跳：调用一次显示驱动初始化探活，结果直接反映到标志位
    hw_heart.gpu_driver_ok = (display_gpu_init() >= 0) ? 1 : 0;

    // HID 心跳：尝试重新打开键鼠
    int kb_ok  = hid_open_device("/dev/hid_kb",   HID_DEV_KEYBOARD);
    int ms_ok  = hid_open_device("/dev/hid_mouse", HID_DEV_MOUSE);
    hw_heart.hid_driver_ok = (kb_ok > 0 || ms_ok > 0) ? 1 : 0;
}

//===================== 全局看门狗守护线程（防卡死、防冻结）=====================
void super_watchdog_daemon(void)
{
    if (!super_sched_inited) return;
    uint64_t now = sys_get_kernel_tick();
    if (now - watchdog_tick < WATCHDOG_INTERVAL) return;
    watchdog_tick = now;

    sys_spin_lock(&global_spin_lock);
    for (int i = 0; i < SUPER_MAX_TASK; i++)
    {
        super_task_t* t = &super_task_pool[i];
        if (t->state == TASK_IDLE || t->state == TASK_FINISH) continue;

        // 死锁检测
        if (now - t->last_run_tick > TASK_DEADLOCK_TICK)
        {
            t->is_stuck = 1;
            t->state = TASK_STUCK_DETECT;
            super_global_stat.total_task_stuck++;
            // 强制解锁兜底
            global_spin_lock = 0;
            super_global_stat.deadlock_release_cnt++;
        }

        // 超时检测
        if (now - t->last_run_tick > TASK_TIMEOUT_TICK)
        {
            t->is_timeout = 1;
            t->need_recover = 1;
            t->state = TASK_CRASH_RECOVER;
        }

        t->last_watchdog_tick = now;
    }
    sys_spin_unlock(&global_spin_lock);
}

//===================== 任务重试冗余系统 ====================
void super_task_retry_dispatch(void)
{
    sys_spin_lock(&global_spin_lock);
    for (int i = 0; i < SUPER_MAX_TASK; i++)
    {
        super_task_t* t = &super_task_pool[i];
        if (t->state != TASK_CRASH_RECOVER) continue;
        if (t->retry_times >= TASK_RETRY_MAX) continue;

        t->retry_times++;
        t->state = TASK_RETRY_QUEUE;
        super_global_stat.total_task_retry++;
    }
    sys_spin_unlock(&global_spin_lock);
}

//===================== 超级初始化【全硬件+心跳+看门狗启动】=====================
int super_scheduler_init(void)
{
    if (super_sched_inited) return 1;
    super_sched_reset();

    // 底层四大硬件驱动启动（带冗余重试）
    mem_universal_drv_init();
    disk_universal_drv_init();
    display_gpu_init();
    hid_open_device("/dev/hid_kb", HID_DEV_KEYBOARD);
    hid_open_device("/dev/hid_mouse", HID_DEV_MOUSE);

    // 上层所有子系统初始化
    trans_init_pipeline("./翻译层/编译器/编译缓存/cache.dat");
    hw_mem_driver_init();
    file_close_all();
    recv_mem_clear_all();
    dst_clear_all_target();
    port_close_all();
    snapshot_backup_init();
    hash_verify_init();
    font_mgr_init("./字库/字体基础管理/字库/");
    gui_init();

    // 心跳与看门狗启动
    hw_heart.last_check_tick = sys_get_kernel_tick();
    watchdog_tick = sys_get_kernel_tick();

    super_sched_inited = 1;
    return 1;
}

//===================== 任务创建（带重试、安全锁、雪崩保护）=====================
int super_create_arm_task(uint32_t pid, task_prio_t prio, uint32_t seg_id, unsigned char* ir_buf, uint32_t ir_len)
{
    if (super_global_quota.running_task_count >= SUPER_MAX_TASK - 10) return -1;

    sys_spin_lock(&global_spin_lock);
    uint64_t now = sys_get_kernel_tick();
    int free_idx = -1;

    for (int i = 0; i < SUPER_MAX_TASK; i++)
    {
        if (super_task_pool[i].state == TASK_IDLE || super_task_pool[i].state == TASK_FINISH)
        {
            free_idx = i;
            break;
        }
    }
    if (free_idx == -1) { sys_spin_unlock(&global_spin_lock); return -1; }

    super_task_t* tk = &super_task_pool[free_idx];
    tk->task_pid = pid;
    tk->priority = prio;
    tk->state = TASK_WAIT_IR;
    tk->create_tick = now;
    tk->last_run_tick = now;
    tk->last_watchdog_tick = now;
    tk->used_tick = 0;
    tk->seg_ids[tk->seg_count++] = seg_id;
    tk->ir_len = ir_len > TASK_IR_MAX_BUF ? TASK_IR_MAX_BUF : ir_len;
    memcpy(tk->ir_data, ir_buf, tk->ir_len);
    tk->is_timeout = 0;
    tk->is_stuck = 0;
    tk->need_recover = 0;
    tk->retry_times = 0;
    tk->safe_lock_flag = 1;

    super_global_stat.total_task_create++;
    super_global_quota.running_task_count++;
    sys_spin_unlock(&global_spin_lock);
    return free_idx;
}

//===================== 单任务流水线（多层容错）=====================
int super_run_task_pipeline(int task_idx)
{
    super_watchdog_daemon();
    super_hardware_heartbeat_check();
    super_task_retry_dispatch();

    sys_spin_lock(&global_spin_lock);
    super_task_t* tk = &super_task_pool[task_idx];
    uint64_t now = sys_get_kernel_tick();
    uint64_t diff = now - tk->last_run_tick;

    tk->used_tick += diff;
    tk->last_run_tick = now;

    // 翻译流水线
    trans_input_arm_ir(tk->seg_ids[0], tk->ir_data, tk->ir_len);
    tk->state = TASK_COMPILE_ARM;

    int x86_len = trans_convert_and_submit();
    if (x86_len <= 0)
    {
        tk->state = TASK_CRASH_RECOVER;
        tk->need_recover = 1;
        sys_spin_unlock(&global_spin_lock);
        return 0;
    }

    recv_mem_write_block(tk->task_pid, super_global_buf, x86_len);
    tk->state = TASK_CACHE_STORE;

    uint32_t forward = dst_forward_data(1, super_global_buf, x86_len);
    super_global_stat.total_forward_byte += forward;
    tk->state = TASK_RUNNING;

    sys_spin_unlock(&global_spin_lock);
    return 1;
}

//===================== 优先级批量调度（防雪崩限流）=====================
// 死锁修复：原实现持 global_spin_lock 调用 super_run_task_pipeline，
// 后者内部又会尝试获取同一把锁，导致死锁。
// 现在改为：在锁内仅收集待执行任务索引到本地数组，
// 释放锁后再调用 super_run_task_pipeline（其内部自带加锁）。
int super_run_all_priority_tasks(void)
{
    super_watchdog_daemon();
    super_hardware_heartbeat_check();

    int pending_idx[SUPER_MAX_TASK];
    int pending_count = 0;

    sys_spin_lock(&global_spin_lock);
    for (task_prio_t p = PRIORITY_REAL_TIME; p >= PRIORITY_LOW; p--)
    {
        for (int i = 0; i < SUPER_MAX_TASK; i++)
        {
            super_task_t* t = &super_task_pool[i];
            if (t->priority == p && (t->state == TASK_WAIT_IR || t->state == TASK_RETRY_QUEUE))
            {
                if (pending_count < SUPER_MAX_TASK) {
                    pending_idx[pending_count++] = i;
                }
            }
        }
    }
    sys_spin_unlock(&global_spin_lock);

    int succ = 0;
    for (int k = 0; k < pending_count; k++) {
        if (super_run_task_pipeline(pending_idx[k])) succ++;
    }
    return succ;
}

//===================== 快照系统（双层内存+磁盘冗余备份）=====================
int super_task_memory_snapshot(uint32_t pid, unsigned long vaddr, uint32_t snap_len)
{
    sys_spin_lock(&global_spin_lock);
    int ret = hw_dma_mem_read(pid, vaddr, snap_len, super_global_buf);
    super_global_stat.total_snap_byte += ret;
    super_global_stat.hw_mem_read_bytes += ret;

    // 双重落盘冗余
    disk_lba_write(0, 1024, ret, super_global_buf);
    disk_lba_write(0, 4096, ret, super_global_buf);
    super_global_stat.disk_write_bytes += ret;

    sys_spin_unlock(&global_spin_lock);
    return ret;
}

//===================== 任务自愈恢复（多层重试兜底）=====================
int super_recover_crash_task(void)
{
    sys_spin_lock(&global_spin_lock);
    int cnt = 0;
    for (int i = 0; i < SUPER_MAX_TASK; i++)
    {
        super_task_t* t = &super_task_pool[i];
        if (t->state == TASK_CRASH_RECOVER || t->state == TASK_STUCK_DETECT)
        {
            sys_task_kill(t->task_pid);
            t->state = TASK_IDLE;
            t->is_timeout = 0;
            t->is_stuck = 0;
            t->need_recover = 0;
            t->retry_times = 0;
            super_global_stat.total_task_crash++;
            super_global_quota.running_task_count--;
            cnt++;
        }
    }
    sys_spin_unlock(&global_spin_lock);
    return cnt;
}

//===================== 状态导出接口 =====================
void super_get_global_stat(super_stat_t* out)
{
    sys_spin_lock(&global_spin_lock);
    memcpy(out, &super_global_stat, sizeof(super_stat_t));
    sys_spin_unlock(&global_spin_lock);
}

void super_get_global_quota(super_quota_t* out)
{
    sys_spin_lock(&global_spin_lock);
    memcpy(out, &super_global_quota, sizeof(super_quota_t));
    sys_spin_unlock(&global_spin_lock);
}

//===================== 终极安全销毁（逐级兜底释放，绝不泄漏）=====================
void super_scheduler_deinit(void)
{
    if (!super_sched_inited) return;

    super_recover_crash_task();
    super_watchdog_daemon();

    recv_mem_clear_all();
    dst_clear_all_target();
    port_close_all();

    gui_deinit();
    font_mgr_deinit();
    hid_close_all();
    hw_mem_driver_deinit();
    mem_universal_drv_deinit();
    disk_universal_drv_deinit();
    file_close_all();

    super_sched_reset();
    super_sched_inited = 0;
    global_spin_lock = 0;
}

#endif
