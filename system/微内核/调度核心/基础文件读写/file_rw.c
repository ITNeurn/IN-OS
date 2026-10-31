// file_rw.c 微内核调度核心底层文件读写模块
#ifndef FILE_RW_H
#define FILE_RW_H

#include <stdint.h>

// 文件操作缓冲区
#define FILE_RW_BUFFER 8192
static unsigned char file_buf[FILE_RW_BUFFER];

// 文件打开模式
#define FILE_MODE_RO 0
#define FILE_MODE_RW 1

// 内核文件句柄结构
typedef struct {
    uint32_t fd;
    uint32_t mode;
    uint64_t file_size;
    uint64_t offset;
} file_handle_t;

// 最大同时打开文件句柄池
#define MAX_FILE_HANDLE 32
static file_handle_t fd_pool[MAX_FILE_HANDLE];
static int fd_count = 0;

static inline void fd_pool_clear(void)
{
    fd_count = 0;
    for(int i = 0; i < MAX_FILE_HANDLE; i++)
    {
        fd_pool[i].fd = 0;
        fd_pool[i].mode = 0;
        fd_pool[i].file_size = 0;
        fd_pool[i].offset = 0;
    }
    memset(file_buf, 0, FILE_RW_BUFFER);
}

// 底层内核系统调用
extern uint32_t sys_file_open(const char *path, int mode);
extern int sys_file_close(uint32_t fd);
extern int sys_file_read(uint32_t fd, uint64_t off, unsigned char *buf, int len);
extern int sys_file_write(uint32_t fd, uint64_t off, unsigned char *buf, int len);
extern uint64_t sys_file_get_size(uint32_t fd);

/**
 * @brief 打开文件，分配句柄缓存
 * @param path 文件路径
 * @param mode 读写模式 FILE_MODE_RO / FILE_MODE_RW
 * @return 内核fd，0打开失败
 */
uint32_t file_open(const char *path, int mode)
{
    if(fd_count >= MAX_FILE_HANDLE)
        return 0;
    uint32_t fd = sys_file_open(path, mode);
    if(fd == 0) return 0;
    file_handle_t *hd = &fd_pool[fd_count++];
    hd->fd = fd;
    hd->mode = mode;
    hd->file_size = sys_file_get_size(fd);
    hd->offset = 0;
    return fd;
}

/**
 * @brief 读取文件指定偏移数据
 * @param fd 文件句柄
 * @param off 读取偏移
 * @param out 输出缓冲区
 * @param len 读取长度
 * @return 实际读取字节
 */
int file_read(uint32_t fd, uint64_t off, unsigned char *out, int len)
{
    int copy_len = len > FILE_RW_BUFFER ? FILE_RW_BUFFER : len;
    int ret = sys_file_read(fd, off, file_buf, copy_len);
    for(int i = 0; i < ret; i++)
        out[i] = file_buf[i];
    return ret;
}

/**
 * @brief 写入文件数据
 * @param fd 文件句柄
 * @param off 写入偏移
 * @param data 待写入数据
 * @param len 长度
 * @return 写入字节
 */
int file_write(uint32_t fd, uint64_t off, unsigned char *data, int len)
{
    int copy_len = len > FILE_RW_BUFFER ? FILE_RW_BUFFER : len;
    memset(file_buf, 0, FILE_RW_BUFFER);
    for(int i = 0; i < copy_len; i++)
        file_buf[i] = data[i];
    return sys_file_write(fd, off, file_buf, copy_len);
}

/**
 * @brief 关闭文件，释放句柄
 * @param fd 文件句柄
 * @return 1关闭成功
 */
int file_close(uint32_t fd)
{
    int idx = -1;
    for(int i = 0; i < fd_count; i++)
    {
        if(fd_pool[i].fd == fd)
        {
            idx = i;
            break;
        }
    }
    if(idx == -1) return 0;
    sys_file_close(fd);
    // 移除条目
    for(int i = idx; i < fd_count - 1; i++)
        fd_pool[i] = fd_pool[i + 1];
    fd_count--;
    return 1;
}

/**
 * @brief 获取文件总大小
 * @param fd 文件句柄
 * @return 文件字节大小
 */
uint64_t file_get_size(uint32_t fd)
{
    for(int i = 0; i < fd_count; i++)
    {
        if(fd_pool[i].fd == fd)
            return fd_pool[i].file_size;
    }
    return 0;
}

// 清空所有打开文件句柄（系统退出/重启）
void file_close_all(void)
{
    for(int i = 0; i < fd_count; i++)
        sys_file_close(fd_pool[i].fd);
    fd_pool_clear();
}

#endif
