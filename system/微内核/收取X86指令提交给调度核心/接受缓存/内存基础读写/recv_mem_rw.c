// recv_mem_rw.c 指令接收缓存内存基础读写模块
#ifndef RECV_MEM_RW_H
#define RECV_MEM_RW_H

#include <stdint.h>
#include <string.h>

// 缓存内存块参数
#define RECV_BLOCK_SIZE 16384
#define MAX_RECV_BLOCK 48

// 缓存内存块描述符
typedef struct {
    uint32_t block_id;
    uint32_t data_len;
    uint64_t recv_tick;
    unsigned char data_buf[RECV_BLOCK_SIZE];
} recv_block_t;

// 全局接收缓存块池
static recv_block_t recv_pool[MAX_RECV_BLOCK];
static int block_cnt = 0;
static unsigned char temp_io_buf[RECV_BLOCK_SIZE];

static inline void recv_pool_reset(void)
{
    block_cnt = 0;
    memset(temp_io_buf, 0, RECV_BLOCK_SIZE);
    for(int i = 0; i < MAX_RECV_BLOCK; i++)
    {
        recv_pool[i].block_id = 0;
        recv_pool[i].data_len = 0;
        recv_pool[i].recv_tick = 0;
        memset(recv_pool[i].data_buf, 0, RECV_BLOCK_SIZE);
    }
}

// 底层内存系统调用
extern int sys_mem_alloc(uint32_t size);
extern void sys_mem_free(int mem_fd);
extern int sys_mem_buf_read(int mem_fd, uint64_t off, unsigned char *out, int len);
extern int sys_mem_buf_write(int mem_fd, uint64_t off, unsigned char *in, int len);
extern uint64_t sys_get_kernel_tick(void);

/**
 * @brief 分配新接收缓存块，写入X86指令流
 * @param block_id 缓存块唯一编号
 * @param x86_ins 待存入X86指令二进制数据
 * @param ins_len 指令长度
 * @return 1分配写入成功，0缓存池已满
 */
int recv_mem_write_block(uint32_t block_id, unsigned char *x86_ins, uint32_t ins_len)
{
    if(block_cnt >= MAX_RECV_BLOCK)
        return 0;
    recv_block_t *blk = &recv_pool[block_cnt++];
    blk->block_id = block_id;
    blk->data_len = ins_len > RECV_BLOCK_SIZE ? RECV_BLOCK_SIZE : ins_len;
    blk->recv_tick = sys_get_kernel_tick();
    memcpy(blk->data_buf, x86_ins, blk->data_len);
    return 1;
}

/**
 * @brief 根据块ID读取缓存内存储的X86指令
 * @param block_id 目标缓存块ID
 * @param out_ins 输出指令缓冲区
 * @param out_len 输出有效数据长度
 * @return 1读取命中，0无对应缓存块
 */
int recv_mem_read_block(uint32_t block_id, unsigned char *out_ins, uint32_t *out_len)
{
    for(int i = 0; i < block_cnt; i++)
    {
        recv_block_t *blk = &recv_pool[i];
        if(blk->block_id == block_id)
        {
            *out_len = blk->data_len;
            memcpy(out_ins, blk->data_buf, blk->data_len);
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 批量读取全部缓存块，合并完整X86指令流
 * @param merge_out 合并输出大缓冲区
 * @param total_out_len 合并后总字节长度
 * @return 有效缓存块数量
 */
int recv_mem_read_all_merge(unsigned char *merge_out, uint32_t *total_out_len)
{
    int total_bytes = 0;
    for(int i = 0; i < block_cnt; i++)
    {
        recv_block_t *blk = &recv_pool[i];
        memcpy(merge_out + total_bytes, blk->data_buf, blk->data_len);
        total_bytes += blk->data_len;
    }
    *total_out_len = total_bytes;
    return block_cnt;
}

/**
 * @brief 释放指定缓存块
 * @param block_id 待释放块ID
 * @return 1释放成功
 */
int recv_mem_free_block(uint32_t block_id)
{
    int target_idx = -1;
    for(int i = 0; i < block_cnt; i++)
    {
        if(recv_pool[i].block_id == block_id)
        {
            target_idx = i;
            break;
        }
    }
    if(target_idx == -1) return 0;
    // 数组前移覆盖删除条目
    for(int i = target_idx; i < block_cnt - 1; i++)
        recv_pool[i] = recv_pool[i + 1];
    block_cnt--;
    return 1;
}

/**
 * @brief 清空全部接收缓存（提交调度核心后调用）
 */
void recv_mem_clear_all(void)
{
    recv_pool_reset();
}

#endif
