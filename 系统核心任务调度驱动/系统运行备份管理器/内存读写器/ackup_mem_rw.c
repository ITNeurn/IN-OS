// backup_mem_rw.c 备份管理器全局内存读写顶层封装
#ifndef BACKUP_MEM_RW_H
#define BACKUP_MEM_RW_H

// 底层分页内存读写依赖
extern int mem_read_page(unsigned int pid, unsigned long vaddr, unsigned char *out_page);
extern int mem_write_data(unsigned int pid, unsigned long vaddr, unsigned char *data, int len);
extern int mem_check_vaddr_valid(unsigned int pid, unsigned long vaddr);

// 读写模式
#define MEM_RW_SAFE 0
#define MEM_RW_FAST 1
// 内存读写缓冲区
#define GLOBAL_MEM_BUF 16384
static unsigned char global_buf[GLOBAL_MEM_BUF];

static inline void global_buf_clear(void)
{
    for(int i=0; i<GLOBAL_MEM_BUF; i++)
        global_buf[i] = 0x00;
}

/**
 * @brief 标准化批量读取任务内存区间
 * @param pid 目标进程PID
 * @param start_vaddr 起始虚拟地址
 * @param total_len 读取总长度
 * @param out_buf 输出数据缓冲区
 * @param mode 安全/快速模式
 * @return 成功读取字节数
 */
int backup_mem_read(unsigned int pid, unsigned long start_vaddr, int total_len, unsigned char *out_buf, int mode)
{
    global_buf_clear();
    int read_total = 0;
    unsigned long cur_addr = start_vaddr;
    int remain = total_len;
    while(remain > 0)
    {
        // 安全模式提前校验地址合法性
        if(mode == MEM_RW_SAFE && !mem_check_vaddr_valid(pid, cur_addr))
            break;
        int page_off = cur_addr & 0xFFF;
        int page_read = 4096 - page_off;
        if(page_read > remain) page_read = remain;
        unsigned char page_cache[4096];
        if(!mem_read_page(pid, cur_addr - page_off, page_cache))
        {
            if(mode == MEM_RW_SAFE) break;
            cur_addr += page_read;
            remain -= page_read;
            continue;
        }
        // 拷贝当前页有效数据
        for(int i=0; i<page_read; i++)
            out_buf[read_total++] = page_cache[page_off + i];
        cur_addr += page_read;
        remain -= page_read;
    }
    return read_total;
}

/**
 * @brief 标准化写入内存（快照恢复专用）
 * @param pid 目标PID
 * @param vaddr 写入起始虚拟地址
 * @param data 待写入数据
 * @param len 数据长度
 * @param mode 模式
 * @return 实际写入字节
 */
int backup_mem_write(unsigned int pid, unsigned long vaddr, unsigned char *data, int len, int mode)
{
    if(mode == MEM_RW_SAFE && !mem_check_vaddr_valid(pid, vaddr))
        return 0;
    global_buf_clear();
    int copy_len = len > GLOBAL_MEM_BUF ? GLOBAL_MEM_BUF : len;
    for(int i=0; i<copy_len; i++)
        global_buf[i] = data[i];
    return mem_write_data(pid, vaddr, global_buf, copy_len);
}

/**
 * @brief 单虚拟页完整读取快捷接口
 * @param pid 进程PID
 * @param vpage 页对齐虚拟地址
 * @param out_page 4K页缓冲区
 * @return 1读取成功，0页面不可访问
 */
int backup_mem_read_single_page(unsigned int pid, unsigned long vpage, unsigned char *out_page)
{
    if((vpage & 0xFFF) != 0) return 0;
    return mem_read_page(pid, vpage, out_page);
}

/**
 * @brief 批量校验一段内存区间全部地址合法性
 * @param pid 进程PID
 * @param start_vaddr 起始地址
 * @param len 区间长度
 * @return 1全段合法，0存在非法地址
 */
int backup_mem_check_range_valid(unsigned int pid, unsigned long start_vaddr, int len)
{
    unsigned long cur = start_vaddr;
    while(cur < start_vaddr + len)
    {
        if(!mem_check_vaddr_valid(pid, cur))
            return 0;
        cur += 4096;
    }
    return 1;
}

#endif
