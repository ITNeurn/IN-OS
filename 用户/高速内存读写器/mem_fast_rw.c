// mem_fast_rw.c 用户层高速内存读写器
#ifndef MEM_FAST_RW_H
#define MEM_FAST_RW_H

// 依赖内核底层内存驱动接口
extern int mem_driver_rw(unsigned int addr, unsigned char *buf, int len, int write_flag);

// 分页内存缓存池，单页4KB
#define MEM_FAST_PAGE_SIZE 4096
#define MEM_FAST_PAGE_CNT 4
static unsigned char mem_page_pool[MEM_FAST_PAGE_CNT][MEM_FAST_PAGE_SIZE];
static unsigned int page_used_len[MEM_FAST_PAGE_CNT] = {0};
static int cur_page_idx = 0;

// 清空指定缓存页
static inline void mem_page_clear(int idx)
{
    if(idx <0 || idx >= MEM_FAST_PAGE_CNT) return;
    for(int i=0; i<MEM_FAST_PAGE_SIZE; i++)
        mem_page_pool[idx][i] = 0x00;
    page_used_len[idx] = 0;
}

// 清空全部缓存页
static inline void mem_pool_full_clear(void)
{
    cur_page_idx = 0;
    for(int i=0; i<MEM_FAST_PAGE_CNT; i++)
        mem_page_clear(i);
}

// 写入当前缓存页，自动切换下一页
int mem_pool_write(unsigned char *data, int data_len)
{
    if(data_len <= 0) return 0;
    int write_remain = data_len;
    int data_off = 0;
    while(write_remain > 0)
    {
        int page_free = MEM_FAST_PAGE_SIZE - page_used_len[cur_page_idx];
        if(page_free <= 0)
        {
            cur_page_idx = (cur_page_idx + 1) % MEM_FAST_PAGE_CNT;
            mem_page_clear(cur_page_idx);
            page_free = MEM_FAST_PAGE_SIZE;
        }
        int copy_len = write_remain > page_free ? page_free : write_remain;
        for(int i=0; i<copy_len; i++)
            mem_page_pool[cur_page_idx][page_used_len[cur_page_idx] + i] = data[data_off + i];
        page_used_len[cur_page_idx] += copy_len;
        write_remain -= copy_len;
        data_off += copy_len;
    }
    return data_len;
}

// 批量将全部缓存页写入目标物理内存地址
int mem_flush_all_pages(unsigned int target_base_addr)
{
    int total_written = 0;
    unsigned int cur_addr = target_base_addr;
    for(int i=0; i<MEM_FAST_PAGE_CNT; i++)
    {
        int len = page_used_len[i];
        if(len == 0) continue;
        int ret = mem_driver_rw(cur_addr, mem_page_pool[i], len, 1);
        total_written += ret;
        cur_addr += MEM_FAST_PAGE_SIZE;
    }
    mem_pool_full_clear();
    return total_written;
}

// 批量从物理内存读取多页到缓存池
int mem_load_pages(unsigned int src_base_addr, int total_load_len)
{
    if(total_load_len <= 0 || total_load_len > MEM_FAST_PAGE_SIZE * MEM_FAST_PAGE_CNT)
        return 0;
    mem_pool_full_clear();
    int remain = total_load_len;
    unsigned int cur_addr = src_base_addr;
    for(int i=0; i<MEM_FAST_PAGE_CNT && remain>0; i++)
    {
        int read_len = remain > MEM_FAST_PAGE_SIZE ? MEM_FAST_PAGE_SIZE : remain;
        int ret = mem_driver_rw(cur_addr, mem_page_pool[i], read_len, 0);
        page_used_len[i] = ret;
        remain -= ret;
        cur_addr += MEM_FAST_PAGE_SIZE;
    }
    cur_page_idx = 0;
    return total_load_len - remain;
}

// 读取缓存池内指定偏移的数据
int mem_pool_read(unsigned char *out_buf, int read_len, int pool_offset)
{
    if(read_len <=0 || pool_offset <0 || pool_offset + read_len > MEM_FAST_PAGE_SIZE * MEM_FAST_PAGE_CNT)
        return 0;
    int page_idx = pool_offset / MEM_FAST_PAGE_SIZE;
    int page_off = pool_offset % MEM_FAST_PAGE_SIZE;
    int remain = read_len;
    int out_off = 0;
    while(remain > 0 && page_idx < MEM_FAST_PAGE_CNT)
    {
        int avail = page_used_len[page_idx] - page_off;
        if(avail <= 0) break;
        int copy = avail > remain ? remain : avail;
        for(int i=0; i<copy; i++)
            out_buf[out_off + i] = mem_page_pool[page_idx][page_off + i];
        out_off += copy;
        remain -= copy;
        page_idx++;
        page_off = 0;
    }
    return read_len - remain;
}

#endif
