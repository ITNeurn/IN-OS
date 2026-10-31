// rw_core.c 读写器核心
#ifndef RW_CORE_H
#define RW_CORE_H

// 依赖上层基础读写器接口
extern int font_file_open(const char *path, int mode);
extern int font_file_read(int handle_idx, unsigned char *buf, int len);
extern int font_file_write(int handle_idx, unsigned char *buf, int len);
extern void font_file_seek(int handle_idx, unsigned int off);
extern void font_file_close(int handle_idx);

// 读写缓冲区大小
#define RW_CORE_BUF_SIZE 2048
static unsigned char rw_buf[RW_CORE_BUF_SIZE];

// 整块读取字体文件区块
int rw_core_read_block(int handle, unsigned int offset, int read_len, unsigned char *out)
{
    if (read_len > RW_CORE_BUF_SIZE)
        return 0;
    font_file_seek(handle, offset);
    int ret = font_file_read(handle, rw_buf, read_len);
    for (int i = 0; i < ret; i++)
        out[i] = rw_buf[i];
    return ret;
}

// 整块写入字体文件区块
int rw_core_write_block(int handle, unsigned int offset, int write_len, unsigned char *in)
{
    if (write_len > RW_CORE_BUF_SIZE)
        return 0;
    for (int i = 0; i < write_len; i++)
        rw_buf[i] = in[i];
    font_file_seek(handle, offset);
    return font_file_write(handle, rw_buf, write_len);
}

// 清空内部读写缓存
static inline void rw_core_clear_buf(void)
{
    for (int i = 0; i < RW_CORE_BUF_SIZE; i++)
        rw_buf[i] = 0x00;
}

#endif
