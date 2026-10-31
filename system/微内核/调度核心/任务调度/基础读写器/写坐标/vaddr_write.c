// vaddr_write.c 任务内存虚拟地址写入核心
#ifndef VADDR_WRITE_H
#define VADDR_WRITE_H

#include <stdint.h>

// 内存页规格
#define PAGE_SIZE 4096
#define WRITE_BUF_MAX 8192
static unsigned char write_buf[WRITE_BUF_MAX];

// 复用已注册内存区间池（与读坐标共享）
typedef struct {
    uint32_t pid;
    unsigned long start_vaddr;
    unsigned long end_vaddr;
    uint32_t page_count;
} vaddr_range_t;
extern vaddr_range_t range_pool[];
extern int range_cnt;

// 内核底层系统调用
extern int sys_mem_write(uint32_t pid, unsigned long vaddr, unsigned char *buf, int len);
extern int sys_check_vaddr_valid(uint32_t pid, unsigned long vaddr);

static inline void write_buf_clear(void)
{
    memset(write_buf, 0, WRITE_BUF_MAX);
}

/**
 * @brief 单虚拟页完整写入
 * @param pid 目标进程PID
 * @param vpage 页对齐虚拟地址
 * @param page_data 4K页待写入数据
 * @return 1写入成功，0地址非法/不在注册区间
 */
int coord_write_single_page(uint32_t pid, unsigned long vpage, unsigned char *page_data)
{
    // 校验地址对齐
    if ((vpage & 0xFFF) != 0) return 0;
    // 校验是否在注册内存区间
    int match = 0;
    for(int i=0; i<range_cnt; i++)
    {
        if(range_pool[i].pid == pid && vpage >= range_pool[i].start_vaddr && vpage < range_pool[i].end_vaddr)
        {
            match = 1;
            break;
        }
    }
    if(!match) return 0;
    // 校验地址可写权限
    if(!sys_check_vaddr_valid(pid, vpage)) return 0;
    write_buf_clear();
    memcpy(write_buf, page_data, PAGE_SIZE);
    return sys_mem_write(pid, vpage, write_buf, PAGE_SIZE);
}

/**
 * @brief 批量连续内存区间写入（快照恢复专用）
 * @param pid 任务PID
 * @param start_vaddr 起始虚拟地址
 * @param data 待写入连续缓冲区
 * @param total_len 总长度
 * @return 实际写入字节数
 */
int coord_write_batch(uint32_t pid, unsigned long start_vaddr, unsigned char *data, int total_len)
{
    if(total_len > WRITE_BUF_MAX) return 0;
    write_buf_clear();
    int copy_len = total_len;
    for(int i=0; i<copy_len; i++)
        write_buf[i] = data[i];
    // 前置全段地址合法性校验
    unsigned long cur = start_vaddr;
    while(cur < start_vaddr + total_len)
    {
        if(!sys_check_vaddr_valid(pid, cur)) return 0;
        cur += 4096;
    }
    return sys_mem_write(pid, start_vaddr, write_buf, copy_len);
}

/**
 * @brief 偏移局部写入（不整页覆盖）
 * @param pid 进程PID
 * @param vaddr 写入虚拟地址
 * @param data 小段数据
 * @param len 数据长度
 * @return 写入字节
 */
int coord_write_partial(uint32_t pid, unsigned long vaddr, unsigned char *data, int len)
{
    if(!sys_check_vaddr_valid(pid, vaddr)) return 0;
    write_buf_clear();
    int copy = len > WRITE_BUF_MAX ? WRITE_BUF_MAX : len;
    memcpy(write_buf, data, copy);
    return sys_mem_write(pid, vaddr, write_buf, copy);
}

#endif
