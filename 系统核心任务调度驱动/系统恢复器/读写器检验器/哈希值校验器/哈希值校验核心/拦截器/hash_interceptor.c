// hash_interceptor.c 快照读写拦截器
#ifndef HASH_INTERCEPTOR_H
#define HASH_INTERCEPTOR_H

// 下层哈希、比较接口
extern int compare_single_block(int block_idx, unsigned char *data, int len, unsigned int expect_crc, int mode);
extern unsigned int rw_hash_calc(unsigned char *data, int data_len);
extern int hash_record_add(unsigned int offset, unsigned int crc_val);

// 拦截缓冲区
#define INTERCEPT_BLOCK_MAX 4096
static unsigned char intercept_buf[INTERCEPT_BLOCK_MAX];
static int buf_used = 0;

static inline void intercept_buf_clear(void)
{
    buf_used = 0;
    for(int i=0; i<INTERCEPT_BLOCK_MAX; i++)
        intercept_buf[i] = 0x00;
}

// 拦截写入数据，缓存并计算哈希
int intercept_write_capture(unsigned char *data, int data_len)
{
    int remain = data_len;
    int off = 0;
    while(remain > 0)
    {
        int free = INTERCEPT_BLOCK_MAX - buf_used;
        int copy = remain > free ? free : remain;
        for(int i=0; i<copy; i++)
            intercept_buf[buf_used + i] = data[off + i];
        buf_used += copy;
        remain -= copy;
        off += copy;
        // 缓存满则自动计算哈希并提交记录
        if(buf_used >= INTERCEPT_BLOCK_MAX)
        {
            unsigned int crc = rw_hash_calc(intercept_buf, buf_used);
            hash_record_add(0, crc);
            intercept_buf_clear();
        }
    }
    return data_len;
}

// 拦截读取数据并实时校验
int intercept_read_verify(int block_idx, unsigned char *data, int data_len, unsigned int std_crc)
{
    intercept_buf_clear();
    int copy = data_len > INTERCEPT_BLOCK_MAX ? INTERCEPT_BLOCK_MAX : data_len;
    for(int i=0; i<copy; i++)
        intercept_buf[i] = data[i];
    // 实时比对哈希，损坏直接返回0拦截
    return compare_single_block(block_idx, intercept_buf, copy, std_crc, 0);
}

// 刷新缓存内剩余数据哈希记录
void intercept_flush_hash(unsigned int block_offset)
{
    if(buf_used == 0) return;
    unsigned int crc = rw_hash_calc(intercept_buf, buf_used);
    hash_record_add(block_offset, crc);
    intercept_buf_clear();
}

// 拦截校验总入口，读写操作前置调用
int intercept_check_before_io(int io_type, int block_idx, unsigned char *data, int len, unsigned int ref_crc)
{
    if(io_type == 0)
    {
        // 读操作：校验拦截
        return intercept_read_verify(block_idx, data, len, ref_crc);
    }
    else
    {
        // 写操作：捕获数据缓存哈希
        intercept_write_capture(data, len);
        return 1;
    }
}

#endif
