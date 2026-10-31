// hash_parser.c 快照哈希元数据解析器
#ifndef HASH_PARSER_H
#define HASH_PARSER_H

// 外部依赖：系统快照文件底层读取接口（实现在 stub_glue.c）
extern int recovery_file_read(unsigned int fid, unsigned int offset, unsigned char *buf, int len);

// 快照头部固定魔数
#define SNAP_MAGIC 0x534E4150
// 快照头部结构体
typedef struct {
    unsigned int magic;
    unsigned int block_count;
    unsigned int single_block_size;
    unsigned int hash_record_offset;
    unsigned int total_hash_offset;
    unsigned int data_start_offset;
} snap_header_t;

// 头部解析缓存
static unsigned char header_buf[256];

// 清空头部缓存
static inline void parser_clear_header(void)
{
    for(int i = 0; i < 256; i++)
        header_buf[i] = 0x00;
}

/**
 * @brief 读取并解析快照文件头部
 * @param file_handle 快照文件句柄
 * @param out_header 输出头部解析结果
 * @return 1合法快照，0损坏/非快照文件
 */
int hash_parser_read_header(int file_handle, snap_header_t *out_header)
{
    parser_clear_header();
    // 读取头部256字节
    int ret = recovery_file_read(file_handle, 0, header_buf, 256);
    if(ret < sizeof(snap_header_t))
        return 0;
    // 拷贝头部字段
    out_header->magic = *(unsigned int*)(header_buf);
    out_header->block_count = *(unsigned int*)(header_buf + 4);
    out_header->single_block_size = *(unsigned int*)(header_buf + 8);
    out_header->hash_record_offset = *(unsigned int*)(header_buf + 12);
    out_header->total_hash_offset = *(unsigned int*)(header_buf + 16);
    out_header->data_start_offset = *(unsigned int*)(header_buf + 20);
    // 校验魔数判断是否为合法快照
    if(out_header->magic != SNAP_MAGIC)
        return 0;
    return 1;
}

/**
 * @brief 根据区块序号获取区块数据偏移地址
 * @param header 已解析快照头部
 * @param block_idx 区块序号
 * @return 区块起始字节偏移
 */
unsigned int hash_parser_get_block_offset(snap_header_t *header, int block_idx)
{
    return header->data_start_offset + block_idx * header->single_block_size;
}

/**
 * @brief 获取指定序号哈希记录在文件内的偏移
 * @param header 快照头部
 * @param rec_idx 哈希记录序号
 * @return 哈希记录偏移
 */
unsigned int hash_parser_get_hash_record_offset(snap_header_t *header, int rec_idx)
{
    return header->hash_record_offset + rec_idx * 8;
}

/**
 * @brief 获取全局总哈希（哈希的哈希）存储偏移
 * @param header 快照头部
 * @return 全局哈希偏移
 */
unsigned int hash_parser_get_global_hash_offset(snap_header_t *header)
{
    return header->total_hash_offset;
}

#endif
