// image_ro_reader.c 系统镜像只读读取器
#ifndef IMAGE_RO_READER_H
#define IMAGE_RO_READER_H

// 依赖上层镜像结构解析器
extern int image_parse_header(unsigned int file_id, image_header_t *out_header);
extern int image_load_seg_table(unsigned int file_id, image_header_t *header);
extern int image_get_segment(int idx, seg_desc_t *out_seg);
// 底层文件只读接口
extern int fs_file_open_ro(const char *path);
extern int fs_file_read(unsigned int fid, unsigned int offset, unsigned char *buf, int len);
extern void fs_file_close(unsigned int fid);

// 只读读取缓存
#define RO_READ_BUF 4096
static unsigned char ro_buf[RO_READ_BUF];

static inline void ro_buf_clear(void)
{
    for(int i = 0; i < RO_READ_BUF; i++)
        ro_buf[i] = 0x00;
}

/**
 * @brief 以只读模式打开镜像文件
 * @param path 镜像文件路径
 * @return 文件句柄，0打开失败
 */
unsigned int image_ro_open(const char *path)
{
    return fs_file_open_ro(path);
}

/**
 * @brief 读取镜像头部与分段表，完成镜像预解析
 * @param fid 镜像只读句柄
 * @param out_header 输出镜像头部信息
 * @return 1镜像合法，0损坏/非法镜像
 */
int image_ro_load_meta(unsigned int fid, image_header_t *out_header)
{
    if(!image_parse_header(fid, out_header))
        return 0;
    image_load_seg_table(fid, out_header);
    return 1;
}

/**
 * @brief 读取指定分段完整数据到输出缓冲区
 * @param fid 文件句柄
 * @param seg_idx 分段索引
 * @param out_data 输出数据缓冲区
 * @param buf_max 缓冲区最大长度
 * @return 实际读取字节数
 */
int image_ro_read_segment(unsigned int fid, int seg_idx, unsigned char *out_data, int buf_max)
{
    seg_desc_t seg;
    if(!image_get_segment(seg_idx, &seg))
        return 0;
    int read_len = seg.seg_size > buf_max ? buf_max : seg.seg_size;
    ro_buf_clear();
    int ret = fs_file_read(fid, seg.seg_offset, ro_buf, read_len);
    for(int i = 0; i < ret; i++)
        out_data[i] = ro_buf[i];
    return ret;
}

/**
 * @brief 关闭只读镜像句柄
 */
void image_ro_close(unsigned int fid)
{
    fs_file_close(fid);
}

/**
 * @brief 读取指定类型首个分段数据（快捷接口）
 * @param fid 文件句柄
 * @param seg_type 分段类型
 * @param out_data 输出缓冲区
 * @param buf_max 缓冲区上限
 * @return 读取长度，-1无对应类型分段
 */
int image_ro_read_first_type(unsigned int fid, seg_type_t seg_type, unsigned char *out_data, int buf_max)
{
    image_header_t hdr;
    image_ro_load_meta(fid, &hdr);
    for(int i = 0; i < hdr.seg_count; i++)
    {
        seg_desc_t seg;
        image_get_segment(i, &seg);
        if(seg.seg_type == seg_type)
            return image_ro_read_segment(fid, i, out_data, buf_max);
    }
    return -1;
}

#endif
