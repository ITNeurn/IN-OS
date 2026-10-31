// backup_hash.c 备用轻量化哈希算法
#ifndef BACKUP_HASH_H
#define BACKUP_HASH_H

// 简易FNV-1a备用哈希，无预计算大表，适合应急降级
#define FNV_PRIME 16777619
#define FNV_OFFSET 2166136261

// 轻量化单数据块哈希计算
unsigned int backup_hash_calc(unsigned char *data, int len)
{
    unsigned int hash = FNV_OFFSET;
    for(int i = 0; i < len; i++)
    {
        hash ^= data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

// 批量多区块合并哈希（对应多层快照校验）
unsigned int backup_hash_batch(unsigned char *block_data[], int block_len[], int block_cnt)
{
    unsigned int total_hash = FNV_OFFSET;
    for(int b = 0; b < block_cnt; b++)
    {
        int len = block_len[b];
        unsigned char *buf = block_data[b];
        for(int i = 0; i < len; i++)
        {
            total_hash ^= buf[i];
            total_hash *= FNV_PRIME;
        }
    }
    return total_hash;
}

// 校验哈希匹配
int backup_hash_verify(unsigned char *data, int len, unsigned int expect_hash)
{
    unsigned int real = backup_hash_calc(data, len);
    return (real == expect_hash) ? 1 : 0;
}

// 批量区块哈希校验
int backup_hash_batch_verify(unsigned char *block_data[], int block_len[], int block_cnt, unsigned int expect_total)
{
    unsigned int real_total = backup_hash_batch(block_data, block_len, block_cnt);
    return (real_total == expect_total) ? 1 : 0;
}

// 判断主哈希模块是否可用（对接状态检测接口）
extern int main_hash_check_status(void);
// 自动切换哈希计算入口，优先主CRC32，故障切备用
unsigned int hash_auto_switch(unsigned char *data, int len)
{
    if(main_hash_check_status() == 1)
    {
        extern unsigned int rw_hash_calc(unsigned char *data, int data_len);
        return rw_hash_calc(data, len);
    }
    return backup_hash_calc(data, len);
}

#endif
