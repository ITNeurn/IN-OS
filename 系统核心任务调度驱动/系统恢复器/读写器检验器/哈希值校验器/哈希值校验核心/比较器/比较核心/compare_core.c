// compare_core.c 哈希比对核心
#ifndef COMPARE_CORE_H
#define COMPARE_CORE_H

// 引入哈希计算接口
extern unsigned int rw_hash_calc(unsigned char *data, int data_len);
extern unsigned int backup_hash_calc(unsigned char *data, int len);
extern unsigned int hash_of_hash_calc(void);
extern int hash_of_hash_verify(unsigned int expect_total_hash);

// 哈希模式标记
#define HASH_MODE_MAIN 0
#define HASH_MODE_BACKUP 1

// 单块校验结果结构体
typedef struct {
    int block_idx;
    unsigned int expect_crc;
    unsigned int real_crc;
    int pass;
} block_check_result_t;

// 最大同时缓存校验结果
#define MAX_CHECK_BLOCK 64
static block_check_result_t check_result_pool[MAX_CHECK_BLOCK];
static int check_cnt = 0;

static inline void check_pool_clear(void)
{
    check_cnt = 0;
    for(int i = 0; i < MAX_CHECK_BLOCK; i++)
    {
        check_result_pool[i].block_idx = -1;
        check_result_pool[i].expect_crc = 0;
        check_result_pool[i].real_crc = 0;
        check_result_pool[i].pass = 0;
    }
}

// 自动选择哈希算法计算
static unsigned int compare_calc_auto(int mode, unsigned char *data, int len)
{
    if(mode == HASH_MODE_MAIN)
        return rw_hash_calc(data, len);
    else
        return backup_hash_calc(data, len);
}

/**
 * @brief 单区块哈希比对
 * @param block_idx 区块编号
 * @param data 区块原始数据
 * @param data_len 数据长度
 * @param expect_crc 文件保存的标准哈希
 * @param hash_mode 哈希模式
 * @return 1校验通过，0区块损坏
 */
int compare_single_block(int block_idx, unsigned char *data, int data_len, unsigned int expect_crc, int hash_mode)
{
    unsigned int real = compare_calc_auto(hash_mode, data, data_len);
    block_check_result_t *res = &check_result_pool[check_cnt++];
    res->block_idx = block_idx;
    res->expect_crc = expect_crc;
    res->real_crc = real;
    res->pass = (real == expect_crc) ? 1 : 0;
    return res->pass;
}

/**
 * @brief 批量校验全部区块，返回损坏区块数量
 * @return 损坏区块个数
 */
int compare_get_fail_count(void)
{
    int fail = 0;
    for(int i = 0; i < check_cnt; i++)
    {
        if(check_result_pool[i].pass == 0)
            fail++;
    }
    return fail;
}

/**
 * @brief 获取指定索引损坏区块编号
 * @param fail_idx 第N个损坏区块
 * @return 区块序号，-1无更多损坏
 */
int compare_get_fail_block(int fail_idx)
{
    int cur_fail = 0;
    for(int i = 0; i < check_cnt; i++)
    {
        if(check_result_pool[i].pass == 0)
        {
            if(cur_fail == fail_idx)
                return check_result_pool[i].block_idx;
            cur_fail++;
        }
    }
    return -1;
}

/**
 * @brief 双层全局校验：哈希集合二次哈希比对
 * @param expect_global_hash 快照存储全局总哈希
 * @return 1哈希表完整，0哈希记录被篡改
 */
int compare_global_double_check(unsigned int expect_global_hash)
{
    return hash_of_hash_verify(expect_global_hash);
}

// 清空本次全部校验缓存
void compare_reset(void)
{
    check_pool_clear();
}

#endif
