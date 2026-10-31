// file_content_reader.c 镜像虚拟文件内容读取器
#ifndef FILE_CONTENT_READER_H
#define FILE_CONTENT_READER_H

// 上层目录寻址接口
extern dir_node_t* dir_find_node_by_name(const char *name);
extern int dir_get_node_seg_index(const char *name);
// 底层镜像只读读取接口
extern int image_ro_read_segment(unsigned int fid, int seg_idx, unsigned char *out_data, int buf_max);

// 文件读取分块缓存
#define FILE_CONTENT_BUF 4096
static unsigned char content_buf[FILE_CONTENT_BUF];

static inline void content_buf_clear(void)
{
    for(int i=0; i<FILE_CONTENT_BUF; i++)
        content_buf[i] = 0x00;
}

/**
 * @brief 根据文件名读取完整文件内容
 * @param file_id 已打开镜像只读句柄
 * @param file_name 虚拟文件名称
 * @param out_buf 输出内容缓冲区
 * @param buf_cap 缓冲区最大容量
 * @return 实际读取字节数，-1文件不存在
 */
int file_content_read_full(unsigned int file_id, const char *file_name, unsigned char *out_buf, int buf_cap)
{
    dir_node_t *node = dir_find_node_by_name(file_name);
    if(!node || node->node_type != DIR_NODE_FILE)
        return -1;
    int seg_idx = node->seg_index;
    int file_len = node->file_size;
    if(file_len > buf_cap)
        file_len = buf_cap;
    content_buf_clear();
    // 读取文件对应镜像分段
    int read_ret = image_ro_read_segment(file_id, seg_idx, content_buf, file_len);
    for(int i=0; i<read_ret; i++)
        out_buf[i] = content_buf[i];
    return read_ret;
}

/**
 * @brief 偏移分块读取文件（大文件分片读取）
 * @param file_id 镜像句柄
 * @param file_name 文件名
 * @param offset 文件内起始偏移
 * @param read_len 期望读取长度
 * @param out_buf 输出缓冲区
 * @return 实际读取字节
 */
int file_content_read_part(unsigned int file_id, const char *file_name, unsigned int offset, int read_len, unsigned char *out_buf)
{
    dir_node_t *node = dir_find_node_by_name(file_name);
    if(!node || node->node_type != DIR_NODE_FILE)
        return -1;
    unsigned int total_len = node->file_size;
    if(offset >= total_len)
        return 0;
    int avail = total_len - offset;
    int real_read = avail > read_len ? read_len : avail;
    content_buf_clear();
    // 完整分段读取后偏移截取
    image_ro_read_segment(file_id, node->seg_index, content_buf, total_len);
    for(int i=0; i<real_read; i++)
        out_buf[i] = content_buf[offset + i];
    return real_read;
}

/**
 * @brief 获取文件原始字节长度
 * @param file_name 虚拟文件名
 * @return 文件大小，-1不存在
 */
int file_content_get_size(const char *file_name)
{
    dir_node_t *node = dir_find_node_by_name(file_name);
    if(!node || node->node_type != DIR_NODE_FILE)
        return -1;
    return node->file_size;
}

#endif
