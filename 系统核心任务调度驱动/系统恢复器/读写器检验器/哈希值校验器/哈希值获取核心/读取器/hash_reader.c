// hash_reader.c 哈希记录读取器
#ifndef HASH_READER_H
#define HASH_READER_H

// 依赖系统快照读写底层接口
extern int recovery_file_open(const char *path, int mode);
extern int recovery_file_read(unsigned int fid, unsigned int offset, unsigned char *buf, int len);
extern void recovery_file_close(unsigned int fid);

// 哈希记录单条结构（与写入端完全对齐）
typedef struct {
    unsigned int block_offset;
    unsigned int block_crc;
} hash_record_t;

// 单次读取缓存
#define HASH_RD_BUF_MAX 1024
static unsigned char rd_buf[HASH_RD_BUF_MAX];

static inline void hash_rd_buf_clear(void)
{
    for(int i=0; i<HASH_RD_BUF_MAX; i++)
        rd_buf[i] = 0x00;
}

/**
 * @brief 从快照文件指定偏移读取一条哈希记录
 * @param file_id 已打开快照文件句柄
 * @param rec_offset 哈希记录在文件内偏移
 * @param out_rec 输出哈希记录结构体
 * @return 1读取成功，0失败
 */
int hash_read_single_record(unsigned int file_id, unsigned int rec_offset, hash_record_t *out_rec)
{
    hash_rd_buf_clear();
    int ret = recovery_file_read(file_id, rec_offset, rd_buf, sizeof(hash_record_t));
    if(ret != sizeof(hash_record_t))
        return 0;
    out_rec->block_offset = *(unsigned int*)(rd_buf);
    out_rec->block_crc = *(unsigned int*)(rd_buf + 4);
    return 1;
}

/**
 * @brief 批量读取连续多条哈希记录到缓存
 * @param file_id 文件句柄
 * @param base_off 起始偏移
 * @param rec_count 读取记录条数
 * @param out_rec_arr 输出记录数组
 * @return 实际读取条数
 */
int hash_read_batch_record(unsigned int file_id, unsigned int base_off, int rec_count, hash_record_t *out_rec_arr)
{
    if(rec_count <= 0 || rec_count * sizeof(hash_record_t) > HASH_RD_BUF_MAX)
        return 0;
    hash_rd_buf_clear();
    int total_byte = rec_count * sizeof(hash_record_t);
    int ret = recovery_file_read(file_id, base_off, rd_buf, total_byte);
    int read_cnt = ret / sizeof(hash_record_t);
    for(int i=0; i<read_cnt; i++)
    {
        unsigned char *p = rd_buf + i * sizeof(hash_record_t);
        out_rec_arr[i].block_offset = *(unsigned int*)(p);
        out_rec_arr[i].block_crc = *(unsigned int*)(p + 4);
    }
    return read_cnt;
}

/**
 * @brief 读取快照全局二次哈希（哈希的哈希）
 * @param file_id 文件句柄
 * @param hash_store_offset 全局哈希存储偏移
 * @return 全局CRC32值，读取失败返回0
 */
unsigned int hash_read_global_hash(unsigned int file_id, unsigned int hash_store_offset)
{
    hash_rd_buf_clear();
    int ret = recovery_file_read(file_id, hash_store_offset, rd_buf, 4);
    if(ret != 4)
        return 0;
    return *(unsigned int*)rd_buf;
}

#endif
