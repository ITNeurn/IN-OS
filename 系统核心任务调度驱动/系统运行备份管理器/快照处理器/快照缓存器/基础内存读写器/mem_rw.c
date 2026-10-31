// mem_rw.c 任务内存基础读写器
#ifndef MEM_RW_H
#define MEM_RW_H

// 分页大小与缓存规格
#define MEM_PAGE_SIZE 4096
#define MEM_RW_BUF_MAX 16384

// 内存读写缓存
static unsigned char mem_buf[MEM_RW_BUF_MAX];

static inline void mem_buf_clear(void)
{
    for(int i = 0; i < MEM_RW_BUF_MAX; i++)
        mem_buf[i] = 0x00;
}

/**
 * @brief 读取进程指定虚拟内存分页数据
 * @param pid 目标任务PID
 * @param page_addr 虚拟页起始地址
 * @param out_page 输出一页内存数据缓冲区
 * @return 1读取成功，0内存页不可访问/无效地址
 */
int mem_read_page(unsigned int pid, unsigned long page_addr, unsigned char *out_page)
{
    mem_buf_clear();
    // 调用内核底层内存读取接口
    int ret = sys_mem_read(pid, page_addr, mem_buf, MEM_PAGE_SIZE);
    if(ret != MEM_PAGE_SIZE)
        return 0;
    for(int i = 0; i < MEM_PAGE_SIZE; i++)
        out_page[i] = mem_buf[i];
    return 1;
}

/**
 * @brief 批量连续分页读取，一次性拉取多页内存
 * @param pid 任务PID
 * @param start_addr 连续区间起始虚拟地址
 * @param page_count 读取页数
 * @param out_data 输出连续内存缓冲区
 * @return 实际成功读取页数
 */
int mem_read_batch_page(unsigned int pid, unsigned long start_addr, int page_count, unsigned char *out_data)
{
    if(page_count * MEM_PAGE_SIZE > MEM_RW_BUF_MAX)
        return 0;
    mem_buf_clear();
    int total_byte = page_count * MEM_PAGE_SIZE;
    int ret = sys_mem_read(pid, start_addr, mem_buf, total_byte);
    int read_page = ret / MEM_PAGE_SIZE;
    int ptr = 0;
    for(int p = 0; p < read_page; p++)
    {
        for(int i = 0; i < MEM_PAGE_SIZE; i++)
            out_data[ptr++] = mem_buf[p * MEM_PAGE_SIZE + i];
    }
    return read_page;
}

/**
 * @brief 向虚拟内存写入快照恢复数据（仅恢复流程使用）
 * @param pid 目标任务PID
 * @param vaddr 写入虚拟地址
 * @param data 待写入数据
 * @param len 数据长度
 * @return 写入字节数
 */
int mem_write_data(unsigned int pid, unsigned long vaddr, unsigned char *data, int len)
{
    mem_buf_clear();
    int copy_len = len > MEM_RW_BUF_MAX ? MEM_RW_BUF_MAX : len;
    for(int i = 0; i < copy_len; i++)
        mem_buf[i] = data[i];
    return sys_mem_write(pid, vaddr, mem_buf, copy_len);
}

/**
 * @brief 校验虚拟地址是否合法可读
 * @param pid 任务PID
 * @param vaddr 待检测地址
 * @return 1地址有效，0地址非法/受保护
 */
int mem_check_vaddr_valid(unsigned int pid, unsigned long vaddr)
{
    return sys_mem_check_perm(pid, vaddr);
}

// 底层内核系统调用声明
extern int sys_mem_read(unsigned int pid, unsigned long vaddr, unsigned char *buf, int len);
extern int sys_mem_write(unsigned int pid, unsigned long vaddr, unsigned char *buf, int len);
extern int sys_mem_check_perm(unsigned int pid, unsigned long vaddr);

#endif
