// image_struct_parser.c 系统镜像结构解析器
#ifndef IMAGE_STRUCT_PARSER_H
#define IMAGE_STRUCT_PARSER_H

// 底层镜像文件读取接口
extern int fs_file_open(const char *path, int mode);
extern int fs_file_read(unsigned int fid, unsigned int offset, unsigned char *buf, int len);
extern void fs_file_close(unsigned int fid);

// 镜像魔数与段类型定义
#define IMAGE_MAGIC 0x494D4753
typedef enum {
    SEG_TYPE_TASK,
    SEG_TYPE_MEM_SNAP,
    SEG_TYPE_FONT_RES,
    SEG_TYPE_ARM_BIN
} seg_type_t;

// 镜像头部结构体
typedef struct {
    unsigned int magic;
    unsigned int seg_count;
    unsigned int header_size;
    unsigned int data_start_offset;
} image_header_t;

// 分段描述符
typedef struct {
    seg_type_t seg_type;
    unsigned int seg_offset;
    unsigned int seg_size;
    unsigned int load_addr;
} seg_desc_t;

// 解析缓存
#define IMAGE_HEADER_BUF 256
static unsigned char parse_buf[IMAGE_HEADER_BUF];
static seg_desc_t seg_cache[16];
static int seg_total = 0;

static inline void parser_clear(void)
{
    seg_total = 0;
    for(int i=0; i<IMAGE_HEADER_BUF; i++) parse_buf[i] = 0;
    for(int i=0; i<16; i++){
        seg_cache[i].seg_type = 0;
        seg_cache[i].seg_offset = 0;
        seg_cache[i].seg_size = 0;
        seg_cache[i].load_addr = 0;
    }
}

/**
 * @brief 读取并解析镜像头部
 * @param file_id 镜像文件句柄
 * @param out_header 输出头部信息
 * @return 1合法镜像，0损坏/非法镜像
 */
int image_parse_header(unsigned int file_id, image_header_t *out_header)
{
    parser_clear();
    int ret = fs_file_read(file_id, 0, parse_buf, sizeof(image_header_t));
    if(ret != sizeof(image_header_t)) return 0;
    out_header->magic = *(unsigned int*)(parse_buf);
    out_header->seg_count = *(unsigned int*)(parse_buf + 4);
    out_header->header_size = *(unsigned int*)(parse_buf + 8);
    out_header->data_start_offset = *(unsigned int*)(parse_buf + 12);
    if(out_header->magic != IMAGE_MAGIC) return 0;
    seg_total = out_header->seg_count;
    return 1;
}

/**
 * @brief 批量读取所有分段描述符
 * @param file_id 文件句柄
 * @param header 已解析镜像头部
 * @return 实际读取分段数量
 */
int image_load_seg_table(unsigned int file_id, image_header_t *header)
{
    int table_offset = sizeof(image_header_t);
    int table_len = header->seg_count * sizeof(seg_desc_t);
    if(table_len > IMAGE_HEADER_BUF) return 0;
    int ret = fs_file_read(file_id, table_offset, parse_buf, table_len);
    int read_cnt = ret / sizeof(seg_desc_t);
    for(int i=0; i<read_cnt; i++)
    {
        unsigned char *p = parse_buf + i * sizeof(seg_desc_t);
        seg_cache[i].seg_type = *(int*)(p);
        seg_cache[i].seg_offset = *(unsigned int*)(p + 4);
        seg_cache[i].seg_size = *(unsigned int*)(p + 8);
        seg_cache[i].load_addr = *(unsigned int*)(p + 12);
    }
    return read_cnt;
}

/**
 * @brief 根据分段索引获取分段信息
 * @param idx 分段序号
 * @param out_seg 输出分段描述符
 * @return 1有效，0索引越界
 */
int image_get_segment(int idx, seg_desc_t *out_seg)
{
    if(idx <0 || idx >= seg_total) return 0;
    *out_seg = seg_cache[idx];
    return 1;
}

/**
 * @brief 获取指定类型第一个分段索引
 * @param type 分段类型
 * @return 分段索引，-1无该类型段
 */
int image_find_first_seg(seg_type_t type)
{
    for(int i=0; i<seg_total; i++)
    {
        if(seg_cache[i].seg_type == type)
            return i;
    }
    return -1;
}

#endif
