// hw_mem_drv.c 硬件级内存高速读写驱动
#ifndef HW_MEM_DRV_H
#define HW_MEM_DRV_H

#include <stdint.h>

// 硬件DMA读写块大小
#define DMA_BLOCK_SIZE 16384
// 硬件读写缓冲区
static unsigned char dma_read_buf[DMA_BLOCK_SIZE];
static unsigned char dma_write_buf[DMA_BLOCK_SIZE];

// 硬件设备句柄
static uint32_t hw_mem_dev_fd = 0;
static int hw_driver_inited = 0;

// 底层硬件寄存器操作系统调用
extern int sys_hw_mem_open(void);
extern void sys_hw_mem_close(void);
extern int sys_dma_read(uint32_t dev, uint64_t phys_addr, unsigned char *out, int len);
extern int sys_dma_write(uint32_t dev, uint64_t phys_addr, unsigned char *in, int len);
extern uint64_t sys_virt_to_phys(uint32_t pid, unsigned long vaddr);

static inline void hw_buf_clear(void)
{
    memset(dma_read_buf, 0, DMA_BLOCK_SIZE);
    memset(dma_write_buf, 0, DMA_BLOCK_SIZE);
}

/**
 * @brief 初始化硬件内存读写驱动，打开DMA设备
 * @return 初始化成功1，失败0
 */
int hw_mem_driver_init(void)
{
    if(hw_driver_inited) return 1;
    hw_mem_dev_fd = sys_hw_mem_open();
    if(hw_mem_dev_fd == 0) return 0;
    hw_buf_clear();
    hw_driver_inited = 1;
    return 1;
}

/**
 * @brief 硬件DMA批量读取进程虚拟内存（物理地址直读）
 * @param pid 目标进程PID
 * @param vaddr 起始虚拟地址
 * @param total_len 读取总长度
 * @param out_user_buf 输出用户缓冲区
 * @return 实际读取字节
 */
int hw_dma_mem_read(uint32_t pid, unsigned long vaddr, int total_len, unsigned char *out_user_buf)
{
    if(!hw_driver_inited) return 0;
    int total_read = 0;
    unsigned long cur_v = vaddr;
    int remain = total_len;
    while(remain > 0)
    {
        int block_len = remain > DMA_BLOCK_SIZE ? DMA_BLOCK_SIZE : remain;
        // 虚拟地址转物理地址
        uint64_t paddr = sys_virt_to_phys(pid, cur_v);
        if(paddr == 0) break;
        // DMA硬件读取物理内存
        int ret = sys_dma_read(hw_mem_dev_fd, paddr, dma_read_buf, block_len);
        if(ret <= 0) break;
        // 拷贝到上层输出缓冲区
        for(int i = 0; i < ret; i++)
            out_user_buf[total_read++] = dma_read_buf[i];
        cur_v += block_len;
        remain -= block_len;
    }
    return total_read;
}

/**
 * @brief 硬件DMA批量写入进程虚拟内存
 * @param pid 目标进程PID
 * @param vaddr 写入起始虚拟地址
 * @param data 待写入数据缓冲区
 * @param total_len 写入总长度
 * @return 实际写入字节
 */
int hw_dma_mem_write(uint32_t pid, unsigned long vaddr, unsigned char *data, int total_len)
{
    if(!hw_driver_inited) return 0;
    int total_write = 0;
    unsigned long cur_v = vaddr;
    int remain = total_len;
    while(remain > 0)
    {
        int block_len = remain > DMA_BLOCK_SIZE ? DMA_BLOCK_SIZE : remain;
        memset(dma_write_buf, 0, DMA_BLOCK_SIZE);
        for(int i = 0; i < block_len; i++)
            dma_write_buf[i] = data[total_write + i];
        uint64_t paddr = sys_virt_to_phys(pid, cur_v);
        if(paddr == 0) break;
        int ret = sys_dma_write(hw_mem_dev_fd, paddr, dma_write_buf, block_len);
        if(ret <= 0) break;
        total_write += ret;
        cur_v += block_len;
        remain -= block_len;
    }
    return total_write;
}

/**
 * @brief 关闭硬件DMA设备，释放驱动
 */
void hw_mem_driver_deinit(void)
{
    if(hw_driver_inited)
    {
        sys_hw_mem_close();
        hw_mem_dev_fd = 0;
        hw_driver_inited = 0;
    }
    hw_buf_clear();
}

#endif
