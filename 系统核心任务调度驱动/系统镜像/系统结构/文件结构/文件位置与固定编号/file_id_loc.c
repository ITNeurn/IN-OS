// file_id_loc.c 文件唯一编号与位置映射模块
#ifndef FILE_ID_LOC_H
#define FILE_ID_LOC_H

// 依赖目录解析接口
extern dir_node_t* dir_find_node_by_name(const char *name);
extern int dir_load_all_nodes(unsigned int fid, image_header_t *header);

// 单条文件ID映射记录
typedef struct {
    unsigned int file_id;
    char file_name[64];
    unsigned int seg_index;
    unsigned int file_size;
} file_id_map_t;

// 最大文件映射缓存
#define MAX_FILE_MAP 128
static file_id_map_t map_pool[MAX_FILE_MAP];
static int map_count = 0;

static inline void map_pool_clear(void)
{
    map_count = 0;
    for(int i=0; i<MAX_FILE_MAP; i++)
    {
        map_pool[i].file_id = 0;
        map_pool[i].file_name[0] = 0;
        map_pool[i].seg_index = 0;
        map_pool[i].file_size = 0;
    }
}

/**
 * @brief 批量生成全部文件固定ID映射表
 * @param image_fid 镜像文件句柄
 * @param header 已解析镜像头部
 * @return 生成映射条目总数
 */
int file_id_build_map(unsigned int image_fid, image_header_t *header)
{
    map_pool_clear();
    // 加载全部分段目录节点
    dir_load_all_nodes(image_fid, header);
    // 遍历所有文件节点分配自增唯一ID
    unsigned int auto_id = 1;
    for(int i=0; i<dir_node_cnt; i++)
    {
        dir_node_t *nd = &dir_pool[i];
        if(nd->node_type != DIR_NODE_FILE)
            continue;
        file_id_map_t *entry = &map_pool[map_count++];
        entry->file_id = auto_id++;
        strcpy(entry->file_name, nd->node_name);
        entry->seg_index = nd->seg_index;
        entry->file_size = nd->file_size;
    }
    return map_count;
}

/**
 * @brief 通过文件名查询文件唯一ID
 * @param name 虚拟文件名
 * @return 全局固定ID，0无此文件
 */
unsigned int file_id_get_by_name(const char *name)
{
    for(int i=0; i<map_count; i++)
    {
        if(strcmp(map_pool[i].file_name, name) == 0)
            return map_pool[i].file_id;
    }
    return 0;
}

/**
 * @brief 通过文件ID反向查询文件信息
 * @param fid 固定文件ID
 * @param out_map 输出映射信息
 * @return 1查询成功，0ID不存在
 */
int file_id_get_by_fid(unsigned int fid, file_id_map_t *out_map)
{
    for(int i=0; i<map_count; i++)
    {
        if(map_pool[i].file_id == fid)
        {
            *out_map = map_pool[i];
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 根据ID获取文件对应镜像分段索引
 * @param fid 文件固定ID
 * @return 分段索引，-1无效ID
 */
int file_id_get_seg_idx(unsigned int fid)
{
    file_id_map_t tmp;
    if(!file_id_get_by_fid(fid, &tmp))
        return -1;
    return tmp.seg_index;
}

#endif
