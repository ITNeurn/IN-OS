// vaddr_read.c 进程虚拟内存坐标读取模块
#ifndef VADDR_READ_H
#define VADDR_READ_H

#include <stdint.h>

// 单页大小
#define PAGE_SIZE 4096
// 坐标读取缓存
#define COORD_BUF 8192
static unsigned char coord_buf[COORD_BUF];

// 虚拟区间描述符
typedef struct {
    uint32_t pid;
    unsigned long start_vaddr;
    unsigned long end_vaddr;
    uint32_t page_count;
} vaddr_range_t;

// 已加载区间缓存池
#define MAX_RANGE_ENTRY 32
static vaddr_range_t range_pool[MAX_RANGE_ENTRY];
static int range_cnt = 0;

static inline void range_pool_reset(void)
{
    range_cnt = 0;
    memset(coord_buf, 0, COORD_BUF);
    for(int i=0; i<MAX_RANGE_ENTRY; i++)
    {
        range_pool[i].pid = 0;
        range_pool[i].start_vaddr = 0;
        range_pool[i].end_vaddr = 0;
        range_pool[i].page_count = 0;
    }
}

// 底层内核内存读接口
extern int sys_mem_read(uint32_t pid, unsigned long vaddr, unsigned char *buf, int len);
extern int sys_check_vaddr(uint32_t pid, unsigned long vaddr);

/**
 * @brief 注册进程内存区间，生成坐标映射
 * @param pid 目标任务PID
 * @param start 起始虚拟地址
 * @param len 区间总长度
 * @return 1注册成功，0缓存池满
 */
int coord_register_range(uint32_t pid, unsigned long start, uint32_t len)
{
    if(range_cnt >= MAX_RANGE_ENTRY) return 0;
    vaddr_range_t *ent = &range_pool[range_cnt++];
    ent->pid = pid;
    ent->start_vaddr = start;
    ent->end_vaddr = start + len;
    ent->page_count = len / PAGE_SIZE;
    return 1;
}

/**
 * @brief 根据PID+偏移读取对应虚拟页坐标数据
 * @param pid 进程PID
 * @param vaddr 待读取虚拟地址
 * @param out_page 输出4K页数据
 * @return 1读取成功，0地址非法/无注册区间
 */
int coord_read_page(uint32_t pid, unsigned long vaddr, unsigned char *out_page)
{
    // 校验是否属于已注册区间
    int match = 0;
    for(int i=0; i<range_cnt; i++)
    {
        vaddr_range_t *r = &range_pool[i];
        if(r->pid == pid && vaddr >= r->start_vaddr && vaddr < r->end_vaddr)
        {
            match = 1;
            break;
        }
    }
    if(!match) return 0;
    // 地址合法性校验
    if(!sys_check_vaddr(pid, vaddr)) return 0;
    // 对齐页首地址
    unsigned long page_base = vaddr & (~0xFFFUL);
    memset(coord_buf, 0, COORD_BUF);
    int ret = sys_mem_read(pid, page_base, coord_buf, PAGE_SIZE);
    if(ret != PAGE_SIZE) return 0;
    for(int i=0; i<PAGE_SIZE; i++)
        out_page[i] = coord_buf[i];
    return 1;
}

/**
 * @brief 批量读取一段连续内存坐标
 * @param pid 任务PID
 * @param start_vaddr 起始地址
 * @param total_len 总长度
 * @param out_data 输出完整缓冲区
 * @return 读取字节总数
 */
int coord_read_batch(uint32_t pid, unsigned long start_vaddr, int total_len, unsigned char *out_data)
{
    if(total_len > COORD_BUF) return 0;
    memset(coord_buf, 0, COORD_BUF);
    int real_read = sys_mem_read(pid, start_vaddr, coord_buf, total_len);
    for(int i=0; i<real_read; i++)
        out_data[i] = coord_buf[i];
    return real_read;
}

/**
 * @brief 清空全部注册区间缓存
 */
void coord_clear_all_range(void)
{
    range_pool_reset();
}

#endif
