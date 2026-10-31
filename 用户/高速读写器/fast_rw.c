// fast_rw.c 用户层高速读写器
#ifndef FAST_RW_H
#define FAST_RW_H

// 依赖内核底层内存读写接口
extern void mem_block_rw(int x, int y, int w, int h, unsigned char *buf);
extern int mem_driver_rw(unsigned int addr, unsigned char *buf, int len, int is_write);

// 高速读写内存池规格
#define FAST_RW_POOL_SIZE 4096
static unsigned char fast_pool[FAST_RW_POOL_SIZE];
static unsigned int pool_used = 0;

// 清空内存池
static inline void fast_pool_clear(void)
{
    for(int i = 0; i < FAST_RW_POOL_SIZE; i++)
        fast_pool[i] = 0x00;
    pool_used = 0;
}

// 向高速池写入数据，返回写入字节数
int fast_pool_write(unsigned char *data, int len)
{
    if(len <= 0 || pool_used + len > FAST_RW_POOL_SIZE)
        return 0;
    for(int i = 0; i < len; i++)
        fast_pool[pool_used + i] = data[i];
    pool_used += len;
    return len;
}

// 从高速池读取指定长度数据
int fast_pool_read(unsigned char *out, int len)
{
    if(len <= 0 || len > pool_used)
        return 0;
    for(int i = 0; i < len; i++)
        out[i] = fast_pool[i];
    return len;
}

// 批量同步写入内核内存（单次提交，减少内核调用次数）
int fast_flush_to_mem(unsigned int target_addr)
{
    if(pool_used == 0)
        return 1;
    int ret = mem_driver_rw(target_addr, fast_pool, pool_used, 1);
    fast_pool_clear();
    return ret;
}

// 批量从内核内存加载到高速池
int fast_load_from_mem(unsigned int src_addr, int load_len)
{
    if(load_len <= 0 || load_len > FAST_RW_POOL_SIZE)
        return 0;
    fast_pool_clear();
    int ret = mem_driver_rw(src_addr, fast_pool, load_len, 0);
    pool_used = ret;
    return ret;
}

// 坐标式批量显存写入（对接字库显示驱动）
void fast_write_coord(int x, int y, int width, int height, unsigned char *data)
{
    // 先存入高速池缓冲
    int total = width * height / 8;
    fast_pool_write(data, total);
    // 批量一次性写入显存坐标
    mem_block_rw(x, y, width, height, fast_pool);
}

#endif
