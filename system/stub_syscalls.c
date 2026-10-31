// stub_syscalls.c
// 系统调用与翻译层缓存等未实现函数的最小 stub 实现。
// 目的：在缺少真实内核态实现的当前阶段，保证整个 super_scheduler.c 翻译单元
// 在链接阶段符号可解析。所有 stub 均返回"成功"或零值，行为可预测。
//
// 注意：这些函数原本应在内核态实现，此处仅为编译/链接占位。
#include <stdint.h>
#include <string.h>

//------------- 单调时钟（用于 sys_get_kernel_tick）-------------
// 宿主环境无内核态时钟，使用一个进程内自增计数器模拟 tick。
static uint64_t g_fake_tick = 1;
uint64_t sys_get_kernel_tick(void)
{
    return ++g_fake_tick;
}

//------------- 自旋锁 stub -------------
// 单线程/无 SMP 环境下，自旋锁退化为空操作。
// 为防止 super_watchdog_daemon 把 global_spin_lock 永久置 1 导致死锁，
// 这里在 lock 时先把 *lock 置 1，unlock 时置 0。
void sys_spin_lock(uint32_t* lock)
{
    if (lock) *lock = 1;
}
void sys_spin_unlock(uint32_t* lock)
{
    if (lock) *lock = 0;
}

//------------- 任务管理 stub -------------
int sys_task_kill(uint32_t pid)
{
    // 真实实现应向调度核心发送终止信号；stub 视为总是成功。
    (void)pid;
    return 1;
}

int sys_submit_exec_bin(const char* path, uint32_t* out_pid)
{
    // 提交可执行文件到调度器，stub 返回固定 pid=1。
    (void)path;
    if (out_pid) *out_pid = 1;
    return 1;
}

//------------- 编译缓存 stub（cache_manager.c 调用）-------------
int cache_load_from_disk(const char* path)
{
    (void)path;
    return 1;
}
int cache_store_segment(uint32_t seg_id, const unsigned char* buf, int len)
{
    (void)seg_id; (void)buf; (void)len;
    return len;
}
int cache_query_hit(uint32_t seg_id)
{
    (void)seg_id;
    return 0; // 永远 miss，触发后续真实翻译
}
void cache_clear_all(void)
{
    // no-op
}
void cache_evict_expire(void)
{
    // no-op
}

//------------- ARM→X86 翻译 stub（arm2x86_trans.c 调用）-------------
// 真实实现需要解码 ARM IR 并生成 X86 机器码；
// 此处 stub 直接把 IR 原样拷贝到输出，返回字节数，保证流水线不中断。
int ir_to_machine(const unsigned char* ir_buf, int ir_len,
                  unsigned char* out_buf, int out_max)
{
    if (!ir_buf || !out_buf || ir_len <= 0) return 0;
    int n = ir_len > out_max ? out_max : ir_len;
    memcpy(out_buf, ir_buf, n);
    return n;
}

int arm_to_x86_convert(const unsigned char* arm_ir, int arm_len,
                       unsigned char* x86_out, int x86_max)
{
    return ir_to_machine(arm_ir, arm_len, x86_out, x86_max);
}
