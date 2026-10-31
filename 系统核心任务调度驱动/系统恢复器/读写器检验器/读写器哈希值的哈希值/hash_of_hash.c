// hash_of_hash.c 哈希值的哈希（二次完整性校验）
#ifndef HASH_OF_HASH_H
#define HASH_OF_HASH_H

// 依赖底层单次哈希计算接口
extern unsigned int rw_hash_calc(unsigned char *data, int data_len);

// 单块哈希存储结构
typedef struct {
    unsigned int block_offset;
    unsigned int block_crc;
} hash_record_t;

// 最大快照分块哈希记录数量
#define MAX_HASH_RECORD 64
static hash_record_t hash_pool[MAX_HASH_RECORD];
static int record_count = 0;

// 清空哈希记录池
static inline void hash_record_clear(void)
{
    record_count = 0;
    for(int i = 0; i < MAX_HASH_RECORD; i++)
    {
        hash_pool[i].block_offset = 0;
        hash_pool[i].block_crc = 0;
    }
}

// 新增一条分块哈希记录
int hash_record_add(unsigned int offset, unsigned int crc_val)
{
    if(record_count >= MAX_HASH_RECORD)
        return 0;
    hash_record_t *rec = &hash_pool[record_count++];
    rec->block_offset = offset;
    rec->block_crc = crc_val;
    return 1;
}

// 将全部哈希记录打包为二进制缓冲区
static int hash_pool_pack(unsigned char *out_buf)
{
    int total_len = record_count * sizeof(hash_record_t);
    int ptr = 0;
    for(int i = 0; i < record_count; i++)
    {
        unsigned char *src = (unsigned char *)&hash_pool[i];
        for(int j = 0; j < sizeof(hash_record_t); j++)
            out_buf[ptr++] = src[j];
    }
    return total_len;
}

/**
 * @brief 计算所有分块哈希集合的总哈希（哈希的哈希）
 * @return 双层校验总CRC32
 */
unsigned int hash_of_hash_calc(void)
{
    if(record_count <= 0)
        return 0;
    unsigned char pack_buf[MAX_HASH_RECORD * 8];
    int pack_len = hash_pool_pack(pack_buf);
    // 复用底层CRC32函数计算二次哈希
    return rw_hash_calc(pack_buf, pack_len);
}

/**
 * @brief 双层校验：对比预期总哈希，判断哈希表是否篡改
 * @param expect_total_hash 保存的基准总哈希
 * @return 1双层校验通过，0哈希表被篡改
 */
int hash_of_hash_verify(unsigned int expect_total_hash)
{
    unsigned int real_total = hash_of_hash_calc();
    return (real_total == expect_total_hash) ? 1 : 0;
}

#endif
