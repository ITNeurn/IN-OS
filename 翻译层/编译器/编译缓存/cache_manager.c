// cache_manager.c 编译缓存顶层调度总入口
#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include <stdint.h>
#include <string.h>

// 下层持久化缓存读写
extern int cache_load_from_disk(const char *path);
extern int cache_store_segment(const char *path, uint32_t seg_id, uint32_t arch, unsigned char *ir, int ir_len, unsigned char *bin, int bin_len);
extern int cache_query_hit(uint32_t seg_id, uint64_t ir_hash, unsigned char *out_bin, uint32_t *out_len);
extern void cache_clear_all(const char *path);
extern int cache_evict_expire(uint64_t expire_tick);

// 编译核心
extern int compile_single_segment(int seg_id, arch_t arch, unsigned char *ir_buf, int ir_len);
extern uint64_t rw_hash_calc(unsigned char *data, int len);

// 全局缓存文件路径
static char global_cache_path[128];
static int cache_init_flag = 0;

static inline void cache_set_path(const char *path)
{
    memset(global_cache_path, 0, sizeof(global_cache_path));
    strcpy(global_cache_path, path);
}

/**
 * @brief 初始化编译缓存，加载磁盘缓存池
 * @param cache_path 缓存文件完整路径
 * @return 已加载缓存条目数量
 */
int cache_mgr_init(const char *cache_path)
{
    cache_set_path(cache_path);
    int entry_count = cache_load_from_disk(global_cache_path);
    cache_init_flag = 1;
    return entry_count;
}

/**
 * @brief 带缓存命中判断的编译统一入口
 * @param seg_id 分段编号
 * @param arch 目标架构
 * @param ir_data 中间代码缓冲区
 * @param ir_len 中间代码长度
 * @param out_bin 输出编译二进制
 * @param out_len 输出二进制长度
 * @return 1编译/缓存命中成功，0失败
 */
int cache_mgr_compile(uint32_t seg_id, arch_t arch, unsigned char *ir_data, int ir_len, unsigned char *out_bin, uint32_t *out_len)
{
    if(!cache_init_flag) return 0;
    uint64_t ir_hash = rw_hash_calc(ir_data, ir_len);
    // 优先查询缓存，命中直接返回
    if(cache_query_hit(seg_id, ir_hash, out_bin, out_len))
        return 1;
    // 缓存未命中，执行真实编译
    if(!compile_single_segment(seg_id, arch, ir_data, ir_len))
        return 0;
    // 编译成功，写入缓存持久化
    cache_store_segment(global_cache_path, seg_id, arch, ir_data, ir_len, out_bin, *out_len);
    return 1;
}

/**
 * @brief 清理过期缓存条目
 * @param expire_tick 过期时间戳
 * @return 清理条目数量
 */
int cache_mgr_clean_expire(uint64_t expire_tick)
{
    return cache_evict_expire(expire_tick);
}

/**
 * @brief 清空全部编译缓存
 */
void cache_mgr_flush_all(void)
{
    if(cache_init_flag)
        cache_clear_all(global_cache_path);
}

#endif
