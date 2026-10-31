// cache_rw.c 快照缓存基础读写模块
#ifndef CACHE_RW_H
#define CACHE_RW_H

// 单缓存块大小
#define CACHE_BLOCK_SIZE 4096
// 最大缓存块数量
#define MAX_CACHE_BLOCK 64

// 缓存块状态标记
#define CACHE_STATUS_EMPTY 0
#define CACHE_STATUS_DIRTY 1
#define CACHE_STATUS_VALID 2

// 缓存块结构体
typedef struct {
    unsigned int block_offset;
    unsigned char data[CACHE_BLOCK_SIZE];
    int status;
} cache_block_t;

// 全局缓存池
static cache_block_t cache_pool[MAX_CACHE_BLOCK];
static int cache_used = 0;

static inline void cache_pool_clear(void)
{
    cache_used = 0;
    for(int i=0; i<MAX_CACHE_BLOCK; i++)
    {
        cache_pool[i].block_offset = 0;
        cache_pool[i].status = CACHE_STATUS_EMPTY;
        for(int j=0; j<CACHE_BLOCK_SIZE; j++)
            cache_pool[i].data[j] = 0x00;
    }
}

// 查找指定偏移缓存块索引
static int cache_find_block(unsigned int offset)
{
    for(int i=0; i<cache_used; i++)
    {
        if(cache_pool[i].block_offset == offset)
            return i;
    }
    return -1;
}

/**
 * @brief 写入数据到缓存块
 * @param offset 快照内字节偏移
 * @param data 待写入数据
 * @param len 数据长度
 * @return 1写入成功，0缓存已满
 */
int cache_write(unsigned int offset, unsigned char *data, int len)
{
    int idx = cache_find_block(offset);
    if(idx == -1)
    {
        if(cache_used >= MAX_CACHE_BLOCK)
            return 0;
        idx = cache_used++;
        cache_pool[idx].block_offset = offset;
    }
    // 拷贝数据并标记脏块
    int copy_len = len > CACHE_BLOCK_SIZE ? CACHE_BLOCK_SIZE : len;
    for(int i=0; i<copy_len; i++)
        cache_pool[idx].data[i] = data[i];
    cache_pool[idx].status = CACHE_STATUS_DIRTY;
    return 1;
}

/**
 * @brief 从缓存读取数据
 * @param offset 快照偏移
 * @param out_buf 输出缓冲区
 * @param len 读取长度
 * @return 1缓存命中，0无缓存
 */
int cache_read(unsigned int offset, unsigned char *out_buf, int len)
{
    int idx = cache_find_block(offset);
    if(idx == -1 || cache_pool[idx].status == CACHE_STATUS_EMPTY)
        return 0;
    int copy_len = len > CACHE_BLOCK_SIZE ? CACHE_BLOCK_SIZE : len;
    for(int i=0; i<copy_len; i++)
        out_buf[i] = cache_pool[idx].data[i];
    return 1;
}

/**
 * @brief 清空指定缓存块
 * @param offset 区块偏移
 */
void cache_invalidate_block(unsigned int offset)
{
    int idx = cache_find_block(offset);
    if(idx != -1)
        cache_pool[idx].status = CACHE_STATUS_EMPTY;
}

/**
 * @brief 获取脏块数量（需要刷盘）
 * @return 脏块个数
 */
int cache_get_dirty_count(void)
{
    int cnt = 0;
    for(int i=0; i<cache_used; i++)
    {
        if(cache_pool[i].status == CACHE_STATUS_DIRTY)
            cnt++;
    }
    return cnt;
}

/**
 * @brief 获取第N个脏块信息，用于刷盘
 * @param dirty_idx 脏块序号
 * @param out_offset 输出区块偏移
 * @param out_data 输出区块数据
 * @return 1获取成功，0无更多脏块
 */
int cache_get_dirty_block(int dirty_idx, unsigned int *out_offset, unsigned char *out_data)
{
    int cur_dirty = 0;
    for(int i=0; i<cache_used; i++)
    {
        if(cache_pool[i].status == CACHE_STATUS_DIRTY)
        {
            if(cur_dirty == dirty_idx)
            {
                *out_offset = cache_pool[i].block_offset;
                for(int j=0; j<CACHE_BLOCK_SIZE; j++)
                    out_data[j] = cache_pool[i].data[j];
                return 1;
            }
            cur_dirty++;
        }
    }
    return 0;
}

// 刷盘完成后标记为干净
void cache_mark_clean(unsigned int offset)
{
    int idx = cache_find_block(offset);
    if(idx != -1)
        cache_pool[idx].status = CACHE_STATUS_VALID;
}

#endif
