// edit_core.c 修改器核心
#ifndef EDIT_CORE_H
#define EDIT_CORE_H

// 引入读写器核心
extern int rw_core_read_block(int handle, unsigned int offset, int read_len, unsigned char *out);
extern int rw_core_write_block(int handle, unsigned int offset, int write_len, unsigned char *in);

// 单条修改最大块
#define EDIT_BLOCK_MAX 512
typedef struct {
    unsigned int block_off;
    int block_len;
    unsigned char block_data[EDIT_BLOCK_MAX];
} edit_block_t;

#define MAX_EDIT_BLOCK 24
static edit_block_t edit_pool[MAX_EDIT_BLOCK];
static int edit_count = 0;

// 清空修改缓存池
static inline void edit_pool_clear(void)
{
    edit_count = 0;
    for (int i = 0; i < MAX_EDIT_BLOCK; i++)
    {
        edit_pool[i].block_off = 0;
        edit_pool[i].block_len = 0;
        for (int j = 0; j < EDIT_BLOCK_MAX; j++)
            edit_pool[i].block_data[j] = 0;
    }
}

// 新增修改区块缓存
int edit_core_add_block(unsigned int off, unsigned char *data, int len)
{
    if (edit_count >= MAX_EDIT_BLOCK || len > EDIT_BLOCK_MAX)
        return 0;
    edit_block_t *blk = &edit_pool[edit_count];
    blk->block_off = off;
    blk->block_len = len;
    for (int i = 0; i < len; i++)
        blk->block_data[i] = data[i];
    edit_count++;
    return 1;
}

// 批量提交全部修改到读写器核心写入磁盘
int edit_core_commit_all(int file_handle)
{
    int all_ok = 1;
    for (int i = 0; i < edit_count; i++)
    {
        edit_block_t *blk = &edit_pool[i];
        int ret = rw_core_write_block(file_handle, blk->block_off, blk->block_len, blk->block_data);
        if (ret != blk->block_len)
            all_ok = 0;
    }
    edit_pool_clear();
    return all_ok;
}

// 丢弃未提交修改
void edit_core_cancel_all(void)
{
    edit_pool_clear();
}

// 读取原始区块用于预览修改
int edit_core_read_origin(int file_handle, unsigned int off, int len, unsigned char *out_buf)
{
    return rw_core_read_block(file_handle, off, len, out_buf);
}

#endif
