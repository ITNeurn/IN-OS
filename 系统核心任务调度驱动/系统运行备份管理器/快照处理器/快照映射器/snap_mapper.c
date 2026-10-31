// snap_mapper.c 内存-快照双向映射管理器
#ifndef SNAP_MAPPER_H
#define SNAP_MAPPER_H

// 底层依赖接口
extern int mem_read_page(unsigned int pid, unsigned long vaddr, unsigned char *out);
extern int mem_write_data(unsigned int pid, unsigned long vaddr, unsigned char *buf, int len);
extern int cache_write(unsigned int offset, unsigned char *data, int len);
extern int cache_read(unsigned int offset, unsigned char *out, int len);
extern unsigned int hash_read_global_hash(unsigned int file_id, unsigned int hash_store_offset);

// 单页映射条目
typedef struct {
    unsigned long vpage_addr;
    unsigned int snap_block_offset;
    unsigned int block_crc;
    int valid;
} page_map_t;

// 最大映射条目
#define MAX_MAP_ENTRY 256
static page_map_t map_pool[MAX_MAP_ENTRY];
static int map_count = 0;

static inline void map_pool_reset(void)
{
    map_count = 0;
    for(int i=0; i<MAX_MAP_ENTRY; i++)
    {
        map_pool[i].vpage_addr = 0;
        map_pool[i].snap_block_offset = 0;
        map_pool[i].block_crc = 0;
        map_pool[i].valid = 0;
    }
}

/**
 * @brief 新增虚拟页-快照区块映射
 * @param vaddr 虚拟页起始地址
 * @param snap_off 该页在快照内偏移
 * @param crc 区块哈希值
 * @return 1添加成功，0映射池已满
 */
int mapper_add_entry(unsigned long vaddr, unsigned int snap_off, unsigned int crc)
{
    if(map_count >= MAX_MAP_ENTRY)
        return 0;
    page_map_t *ent = &map_pool[map_count++];
    ent->vpage_addr = vaddr;
    ent->snap_block_offset = snap_off;
    ent->block_crc = crc;
    ent->valid = 1;
    return 1;
}

/**
 * @brief 通过虚拟地址查询快照偏移与标准哈希
 * @param vaddr 虚拟页地址
 * @param out_snap_off 输出快照偏移
 * @param out_crc 输出预期哈希
 * @return 1映射存在，0无对应映射
 */
int mapper_lookup_by_vaddr(unsigned long vaddr, unsigned int *out_snap_off, unsigned int *out_crc)
{
    for(int i=0; i<map_count; i++)
    {
        if(map_pool[i].valid && map_pool[i].vpage_addr == vaddr)
        {
            *out_snap_off = map_pool[i].snap_block_offset;
            *out_crc = map_pool[i].block_crc;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 根据快照偏移反向查询虚拟页地址
 * @param snap_off 快照区块偏移
 * @return 对应虚拟地址，0无匹配
 */
unsigned long mapper_reverse_lookup(unsigned int snap_off)
{
    for(int i=0; i<map_count; i++)
    {
        if(map_pool[i].valid && map_pool[i].snap_block_offset == snap_off)
            return map_pool[i].vpage_addr;
    }
    return 0;
}

/**
 * @brief 同步抓取内存页并写入快照缓存
 * @param pid 任务PID
 * @param vaddr 虚拟页地址
 * @return 1缓存写入成功，0映射/内存读取失败
 */
int mapper_sync_memory_to_cache(unsigned int pid, unsigned long vaddr)
{
    unsigned int snap_off, crc;
    if(!mapper_lookup_by_vaddr(vaddr, &snap_off, &crc))
        return 0;
    unsigned char page_buf[4096];
    if(!mem_read_page(pid, vaddr, page_buf))
        return 0;
    return cache_write(snap_off, page_buf, 4096);
}

/**
 * @brief 从缓存恢复数据到任务内存
 * @param pid 目标PID
 * @param vaddr 虚拟页地址
 * @return 1恢复成功，0缓存缺失
 */
int mapper_restore_cache_to_memory(unsigned int pid, unsigned long vaddr)
{
    unsigned int snap_off, crc;
    if(!mapper_lookup_by_vaddr(vaddr, &snap_off, &crc))
        return 0;
    unsigned char page_buf[4096];
    if(!cache_read(snap_off, page_buf, 4096))
        return 0;
    // mem_write_data 已在文件顶部 extern 声明（原行内联 extern 声明存在参数无类型的语法错误，已删除）
    return mem_write_data(pid, vaddr, page_buf, 4096);
}

// 清空全部映射表
void mapper_clear_all(void)
{
    map_pool_reset();
}

#endif
