// mem_universal_drv.c 全系统通用底层内存硬件驱动
#ifndef MEM_UNIVERSAL_DRV_H
#define MEM_UNIVERSAL_DRV_H

#include <stdint.h>
#include <string.h>

// 硬件内存块规格
#define MEM_BLOCK_SIZE 16384
#define MAX_MEM_BLOCK_POOL 64
#define MEM_GLOBAL_TMP_BUF 32768

// 内存访问权限标记
typedef enum {
    MEM_FLAG_READ = 0x01,
    MEM_FLAG_WRITE = 0x02,
    MEM_FLAG_EXEC = 0x04,
    MEM_FLAG_DMA = 0x08
} mem_flag_t;

// 内存块管理描述符
typedef struct {
    uint64_t phys_start;
    uint64_t phys_end;
    uint32_t block_id;
    mem_flag_t access_flag;
    uint8_t in_use;
    uint64_t alloc_tick;
} mem_block_t;

// 全局内存块静态池
static mem_block_t mem_pool[MAX_MEM_BLOCK_POOL];
static int mem_block_count = 0;
static unsigned char mem_tmp_buf[MEM_GLOBAL_TMP_BUF];
static uint32_t mem_hw_dev = 0;
static int mem_drv_init_flag = 0;

// 底层硬件内存系统调用
extern uint32_t sys_mem_hw_open(void);
extern void sys_mem_hw_close(uint32_t dev);
extern int sys_mem_phys_read(uint32_t dev, uint64_t paddr, unsigned char *out, int len);
extern int sys_mem_phys_write(uint32_t dev, uint64_t paddr, unsigned char *in, int len);
extern uint64_t sys_virt_to_phys(uint32_t pid, unsigned long vaddr);
extern uint64_t sys_get_kernel_tick(void);

static inline void mem_drv_reset_pool(void)
{
    mem_block_count = 0;
    memset(mem_tmp_buf, 0, MEM_GLOBAL_TMP_BUF);
    for(int i = 0; i < MAX_MEM_BLOCK_POOL; i++)
    {
        mem_pool[i].phys_start = 0;
        mem_pool[i].phys_end = 0;
        mem_pool[i].block_id = 0;
        mem_pool[i].access_flag = 0;
        mem_pool[i].in_use = 0;
        mem_pool[i].alloc_tick = 0;
    }
}

/**
 * @brief 初始化全局内存硬件驱动设备
 * @return 初始化成功返回1，失败0
 */
int mem_universal_drv_init(void)
{
    if(mem_drv_init_flag) return 1;
    mem_hw_dev = sys_mem_hw_open();
    if(mem_hw_dev == 0) return 0;
    mem_drv_reset_pool();
    mem_drv_init_flag = 1;
    return 1;
}

/**
 * @brief 注册一段物理内存区间，分配唯一块ID
 * @param p_start 物理起始地址
 * @param p_len 区间总长度
 * @param flag 访问权限标记
 * @return 内存块ID，-1分配失败
 */
int mem_register_phys_block(uint64_t p_start, uint64_t p_len, mem_flag_t flag)
{
    if(mem_block_count >= MAX_MEM_BLOCK_POOL) return -1;
    // 查重防止重复注册
    for(int i = 0; i < mem_block_count; i++)
    {
        if(mem_pool[i].phys_start == p_start && mem_pool[i].phys_end == p_start + p_len)
            return mem_pool[i].block_id;
    }
    mem_block_t *blk = &mem_pool[mem_block_count];
    blk->block_id = mem_block_count + 1;
    blk->phys_start = p_start;
    blk->phys_end = p_start + p_len;
    blk->access_flag = flag;
    blk->in_use = 1;
    blk->alloc_tick = sys_get_kernel_tick();
    mem_block_count++;
    return blk->block_id;
}

/**
 * @brief 校验物理地址是否在注册区间且拥有对应权限
 * @param paddr 目标物理地址
 * @param req_flag 需要的访问权限
 * @return 1合法可访问，0非法
 */
int mem_check_access(uint64_t paddr, mem_flag_t req_flag)
{
    for(int i = 0; i < mem_block_count; i++)
    {
        mem_block_t *b = &mem_pool[i];
        if(!b->in_use) continue;
        if(paddr >= b->phys_start && paddr < b->phys_end)
        {
            if((b->access_flag & req_flag) == req_flag)
                return 1;
        }
    }
    return 0;
}

/**
 * @brief 虚拟地址批量读取，自动转换物理地址
 * @param pid 目标进程PID
 * @param vaddr 起始虚拟地址
 * @param read_len 读取总长度
 * @param out_buf 输出缓冲区
 * @return 实际读取字节数
 */
int mem_virtual_read(uint32_t pid, unsigned long vaddr, uint32_t read_len, unsigned char *out_buf)
{
    if(!mem_drv_init_flag) return 0;
    int total_read = 0;
    unsigned long cur_v = vaddr;
    int remain = read_len;
    while(remain > 0)
    {
        int block_len = remain > MEM_BLOCK_SIZE ? MEM_BLOCK_SIZE : remain;
        uint64_t paddr = sys_virt_to_phys(pid, cur_v);
        if(paddr == 0 || !mem_check_access(paddr, MEM_FLAG_READ)) break;
        int ret = sys_mem_phys_read(mem_hw_dev, paddr, mem_tmp_buf, block_len);
        if(ret <= 0) break;
        memcpy(out_buf + total_read, mem_tmp_buf, ret);
        total_read += ret;
        cur_v += block_len;
        remain -= block_len;
    }
    return total_read;
}

/**
 * @brief 虚拟地址批量写入
 * @param pid 目标进程PID
 * @param vaddr 起始虚拟地址
 * @param in_data 待写入数据
 * @param write_len 写入总长度
 * @return 实际写入字节数
 */
int mem_virtual_write(uint32_t pid, unsigned long vaddr, unsigned char *in_data, uint32_t write_len)
{
    if(!mem_drv_init_flag) return 0;
    int total_write = 0;
    unsigned long cur_v = vaddr;
    int remain = write_len;
    while(remain > 0)
    {
        int block_len = remain > MEM_BLOCK_SIZE ? MEM_BLOCK_SIZE : remain;
        uint64_t paddr = sys_virt_to_phys(pid, cur_v);
        if(paddr == 0 || !mem_check_access(paddr, MEM_FLAG_WRITE)) break;
        memset(mem_tmp_buf, 0, MEM_BLOCK_SIZE);
        memcpy(mem_tmp_buf, in_data + total_write, block_len);
        int ret = sys_mem_phys_write(mem_hw_dev, paddr, mem_tmp_buf, block_len);
        if(ret <= 0) break;
        total_write += ret;
        cur_v += block_len;
        remain -= block_len;
    }
    return total_write;
}

/**
 * @brief 释放指定内存块注册记录
 * @param block_id 块ID
 */
void mem_unregister_block(int block_id)
{
    for(int i = 0; i < mem_block_count; i++)
    {
        if(mem_pool[i].block_id == block_id)
        {
            mem_pool[i].in_use = 0;
            break;
        }
    }
}

/**
 * @brief 销毁内存驱动，关闭硬件设备、清空全部资源
 */
void mem_universal_drv_deinit(void)
{
    if(!mem_drv_init_flag) return;
    sys_mem_hw_close(mem_hw_dev);
    mem_hw_dev = 0;
    mem_drv_init_flag = 0;
    mem_drv_reset_pool();
}

#endif
