// recovery_rw.c 系统恢复器专用读写器
#ifndef RECOVERY_RW_H
#define RECOVERY_RW_H

// 外部依赖
// 1、系统底层文件读写接口
extern int fs_file_open(const char *path, int mode);
extern int fs_file_read(unsigned int fid, unsigned int offset, unsigned char *buf, int len);
extern int fs_file_write(unsigned int fid, unsigned int offset, unsigned char *buf, int len);
extern void fs_file_close(unsigned int fid);
extern int fs_file_get_size(unsigned int fid);
// 2、用户层高速内存读写接口
extern int mem_load_pages(unsigned int src_addr, int load_len);
extern int mem_flush_all_pages(unsigned int target_addr);

// 文件访问模式常量（与 fs_file_open 配合使用）
// 原 recovery_rw.c 直接使用 FONT_READ/FONT_WRITE 而未定义，此处补全。
#ifndef FONT_READ
#define FONT_READ  0
#endif
#ifndef FONT_WRITE
#define FONT_WRITE 1
#endif

// 快照读写分块大小
#define RECOVERY_BLOCK_SIZE 4096
static unsigned char recovery_buf[RECOVERY_BLOCK_SIZE];

// 清空快照读写缓存
static inline void recovery_buf_clear(void)
{
    for(int i = 0; i < RECOVERY_BLOCK_SIZE; i++)
        recovery_buf[i] = 0x00;
}

/**
 * @brief 将内存任务快照写入快照文件
 * @param file_path 快照文件路径
 * @param mem_base 任务内存起始地址
 * @param total_size 总镜像大小
 * @return 1成功 0失败
 */
int recovery_write_snap(const char *file_path, unsigned int mem_base, int total_size)
{
    int file_id = fs_file_open(file_path, FONT_WRITE);
    if(file_id == 0)
        return 0;
    unsigned int cur_mem = mem_base;
    unsigned int cur_off = 0;
    int remain = total_size;
    while(remain > 0)
    {
        recovery_buf_clear();
        int read_len = remain > RECOVERY_BLOCK_SIZE ? RECOVERY_BLOCK_SIZE : remain;
        // 从物理内存读取数据到缓存
        mem_load_pages(cur_mem, read_len);
        // 写入快照文件
        int write_ret = fs_file_write(file_id, cur_off, recovery_buf, read_len);
        if(write_ret <= 0)
        {
            fs_file_close(file_id);
            return 0;
        }
        cur_mem += read_len;
        cur_off += read_len;
        remain -= read_len;
    }
    fs_file_close(file_id);
    return 1;
}

/**
 * @brief 从快照文件读取镜像并恢复到系统内存
 * @param file_path 快照文件路径
 * @param mem_target 恢复目标内存地址
 * @param max_load 最大读取字节数
 * @return 实际加载字节数
 */
int recovery_load_snap(const char *file_path, unsigned int mem_target, int max_load)
{
    int file_id = fs_file_open(file_path, FONT_READ);
    if(file_id == 0)
        return 0;
    int file_total = fs_file_get_size(file_id);
    int load_len = file_total > max_load ? max_load : file_total;
    unsigned int cur_addr = mem_target;
    unsigned int cur_off = 0;
    int remain = load_len;
    while(remain > 0)
    {
        recovery_buf_clear();
        int block_len = remain > RECOVERY_BLOCK_SIZE ? RECOVERY_BLOCK_SIZE : remain;
        int ret = fs_file_read(file_id, cur_off, recovery_buf, block_len);
        if(ret <= 0) break;
        // 批量写入系统内存
        mem_flush_all_pages(cur_addr);
        cur_addr += block_len;
        cur_off += block_len;
        remain -= ret;
    }
    fs_file_close(file_id);
    return load_len - remain;
}

/**
 * @brief 获取快照文件总大小
 */
int recovery_get_snap_size(const char *file_path)
{
    int fid = fs_file_open(file_path, FONT_READ);
    if(fid == 0) return -1;
    int size = fs_file_get_size(fid);
    fs_file_close(fid);
    return size;
}

#endif
