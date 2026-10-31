// disk_universal_drv.c 块设备磁盘通用底层驱动
#ifndef DISK_UNIVERSAL_DRV_H
#define DISK_UNIVERSAL_DRV_H

#include <stdint.h>
#include <string.h>

// 磁盘硬件常量
#define DISK_SECTOR_SIZE 512
#define DISK_DMA_BLOCK 32768
#define MAX_DISK_DEV 8
#define DISK_CACHE_SECTOR 128

// 磁盘设备访问权限
typedef enum {
    DISK_RD = 0x01,
    DISK_WR = 0x02,
    DISK_DIRECT_DMA = 0x04
} disk_access_t;

// 磁盘分区描述符
typedef struct {
    uint64_t start_lba;
    uint64_t total_sector;
    uint32_t part_id;
    uint8_t access_flag;
} disk_part_t;

// 磁盘硬件设备结构体
typedef struct {
    uint32_t dev_fd;
    char dev_path[64];
    uint8_t active;
    uint64_t total_sectors;
    uint32_t part_count;
    disk_part_t part_list[16];
    unsigned char sector_cache[DISK_CACHE_SECTOR * DISK_SECTOR_SIZE];
} disk_dev_t;

static disk_dev_t disk_pool[MAX_DISK_DEV];
static int disk_dev_cnt = 0;
static unsigned char disk_dma_buf[DISK_DMA_BLOCK];
static int disk_drv_inited = 0;

// 底层磁盘硬件系统调用
extern uint32_t sys_disk_open(const char *path);
extern void sys_disk_close(uint32_t fd);
extern int sys_disk_dma_read(uint32_t dev, uint64_t lba, unsigned char *out, uint32_t bytes);
extern int sys_disk_dma_write(uint32_t dev, uint64_t lba, unsigned char *in, uint32_t bytes);
extern uint64_t sys_get_kernel_tick(void);

static inline void disk_drv_reset(void)
{
    disk_dev_cnt = 0;
    memset(disk_dma_buf, 0, DISK_DMA_BLOCK);
    for(int i = 0; i < MAX_DISK_DEV; i++)
    {
        memset(&disk_pool[i], 0, sizeof(disk_dev_t));
        disk_pool[i].active = 0;
    }
}

/**
 * @brief 初始化磁盘驱动全局资源
 * @return 1成功 0失败
 */
int disk_universal_drv_init(void)
{
    if(disk_drv_inited) return 1;
    disk_drv_reset();
    disk_drv_inited = 1;
    return 1;
}

/**
 * @brief 打开磁盘硬件设备
 * @param dev_path 磁盘设备路径
 * @return 设备池索引，-1失败
 */
int disk_open_device(const char *dev_path)
{
    // 查重设备
    for(int i = 0; i < disk_dev_cnt; i++)
    {
        if(strcmp(disk_pool[i].dev_path, dev_path) == 0 && disk_pool[i].active)
            return i;
    }
    if(disk_dev_cnt >= MAX_DISK_DEV) return -1;
    uint32_t fd = sys_disk_open(dev_path);
    if(fd == 0) return -1;

    disk_dev_t *dev = &disk_pool[disk_dev_cnt];
    strcpy(dev->dev_path, dev_path);
    dev->dev_fd = fd;
    dev->active = 1;
    dev->part_count = 0;
    return disk_dev_cnt++;
}

/**
 * @brief 注册磁盘分区LBA区间与访问权限
 * @param dev_idx 磁盘设备索引
 * @param start_lba 分区起始扇区
 * @param sec_num 分区总扇区数
 * @param flag 读写权限
 * @return 分区唯一ID
 */
uint32_t disk_register_partition(int dev_idx, uint64_t start_lba, uint64_t sec_num, disk_access_t flag)
{
    if(dev_idx < 0 || dev_idx >= disk_dev_cnt) return 0;
    disk_dev_t *dev = &disk_pool[dev_idx];
    if(dev->part_count >= 16) return 0;

    disk_part_t *p = &dev->part_list[dev->part_count];
    p->part_id = dev->part_count + 1;
    p->start_lba = start_lba;
    p->total_sector = sec_num;
    p->access_flag = flag;
    dev->part_count++;
    return p->part_id;
}

/**
 * @brief 校验LBA扇区是否在合法分区内且具备对应权限
 */
int disk_check_lba_permission(int dev_idx, uint64_t lba, disk_access_t req_flag)
{
    disk_dev_t *dev = &disk_pool[dev_idx];
    for(int i = 0; i < dev->part_count; i++)
    {
        disk_part_t *pt = &dev->part_list[i];
        if(lba >= pt->start_lba && lba < pt->start_lba + pt->total_sector)
        {
            if((pt->access_flag & req_flag) == req_flag)
                return 1;
        }
    }
    return 0;
}

/**
 * @brief 按扇区LBA批量DMA读取磁盘数据
 * @param dev_idx 磁盘设备索引
 * @param lba 起始扇区号
 * @param read_bytes 读取总字节
 * @param out_buf 输出缓冲区
 * @return 实际读取字节
 */
int disk_lba_read(int dev_idx, uint64_t lba, uint32_t read_bytes, unsigned char *out_buf)
{
    if(!disk_drv_inited || dev_idx < 0 || dev_idx >= disk_dev_cnt) return 0;
    disk_dev_t *dev = &disk_pool[dev_idx];
    if(!dev->active) return 0;
    uint64_t cur_lba = lba;
    int total_read = 0;
    int remain = read_bytes;
    while(remain > 0)
    {
        int block_len = remain > DISK_DMA_BLOCK ? DISK_DMA_BLOCK : remain;
        if(!disk_check_lba_permission(dev_idx, cur_lba, DISK_RD)) break;
        int ret = sys_disk_dma_read(dev->dev_fd, cur_lba * DISK_SECTOR_SIZE, disk_dma_buf, block_len);
        if(ret <= 0) break;
        memcpy(out_buf + total_read, disk_dma_buf, ret);
        total_read += ret;
        cur_lba += block_len / DISK_SECTOR_SIZE;
        remain -= block_len;
    }
    return total_read;
}

/**
 * @brief 按扇区LBA批量DMA写入磁盘数据
 * @param dev_idx 磁盘设备索引
 * @param lba 起始扇区号
 * @param write_bytes 写入总字节
 * @param in_buf 待写入数据缓冲区
 * @return 实际写入字节
 */
int disk_lba_write(int dev_idx, uint64_t lba, uint32_t write_bytes, unsigned char *in_buf)
{
    if(!disk_drv_inited || dev_idx < 0 || dev_idx >= disk_dev_cnt) return 0;
    disk_dev_t *dev = &disk_pool[dev_idx];
    if(!dev->active) return 0;
    uint64_t cur_lba = lba;
    int total_write = 0;
    int remain = write_bytes;
    while(remain > 0)
    {
        int block_len = remain > DISK_DMA_BLOCK ? DISK_DMA_BLOCK : remain;
        if(!disk_check_lba_permission(dev_idx, cur_lba, DISK_WR)) break;
        memset(disk_dma_buf, 0, DISK_DMA_BLOCK);
        memcpy(disk_dma_buf, in_buf + total_write, block_len);
        int ret = sys_disk_dma_write(dev->dev_fd, cur_lba * DISK_SECTOR_SIZE, disk_dma_buf, block_len);
        if(ret <= 0) break;
        total_write += ret;
        cur_lba += block_len / DISK_SECTOR_SIZE;
        remain -= block_len;
    }
    return total_write;
}

/**
 * @brief 关闭单个磁盘设备
 */
void disk_close_device(int dev_idx)
{
    if(dev_idx < 0 || dev_idx >= disk_dev_cnt) return;
    disk_dev_t *dev = &disk_pool[dev_idx];
    if(dev->active)
    {
        sys_disk_close(dev->dev_fd);
        dev->active = 0;
    }
}

/**
 * @brief 关闭全部磁盘设备，销毁驱动
 */
void disk_universal_drv_deinit(void)
{
    if(!disk_drv_inited) return;
    for(int i = 0; i < disk_dev_cnt; i++)
        disk_close_device(i);
    disk_drv_reset();
    disk_drv_inited = 0;
}

#endif
