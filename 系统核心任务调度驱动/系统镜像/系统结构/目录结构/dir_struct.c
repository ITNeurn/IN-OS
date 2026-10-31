// dir_struct.c 镜像虚拟目录解析器
#ifndef DIR_STRUCT_H
#define DIR_STRUCT_H

// 依赖镜像底层读取接口
extern int image_ro_load_meta(unsigned int fid, image_header_t *hdr);
extern int image_ro_read_segment(unsigned int fid, int seg_idx, unsigned char *buf, int max_len);

// 目录节点类型
typedef enum {
    DIR_NODE_FILE,
    DIR_NODE_FOLDER
} dir_node_type_t;

// 目录节点结构体
typedef struct {
    dir_node_type_t node_type;
    char node_name[64];
    unsigned int child_count;
    unsigned int seg_index;
    unsigned int file_size;
} dir_node_t;

// 最大目录节点缓存
#define MAX_DIR_NODE 128
static dir_node_t dir_pool[MAX_DIR_NODE];
static int dir_node_cnt = 0;

static inline void dir_node_clear(dir_node_t *node)
{
    node->node_type = DIR_NODE_FILE;
    node->child_count = 0;
    node->seg_index = 0;
    node->file_size = 0;
    for(int i=0; i<64; i++) node->node_name[i] = 0;
}

static inline void dir_mgr_reset(void)
{
    dir_node_cnt = 0;
    for(int i=0; i<MAX_DIR_NODE; i++)
        dir_node_clear(&dir_pool[i]);
}

// 从镜像目录分段加载全部目录节点
int dir_load_all_nodes(unsigned int fid)
{
    dir_mgr_reset();
    image_header_t hdr;
    if(!image_ro_load_meta(fid, &hdr))
        return 0;
    // 查找目录描述分段
    int dir_seg_idx = -1;
    for(int i=0; i<hdr.seg_count; i++)
    {
        seg_desc_t seg;
        image_get_segment(i, &seg);
        if(seg.seg_type == SEG_TYPE_FONT_RES)
        {
            dir_seg_idx = i;
            break;
        }
    }
    if(dir_seg_idx == -1) return 0;
    // 读取目录分段原始数据
    unsigned char dir_buf[4096];
    int read_len = image_ro_read_segment(fid, dir_seg_idx, dir_buf, 4096);
    int ptr = 0;
    while(ptr < read_len && dir_node_cnt < MAX_DIR_NODE)
    {
        dir_node_t *nd = &dir_pool[dir_node_cnt++];
        nd->node_type = *(int*)(dir_buf + ptr);
        ptr += 4;
        nd->child_count = *(unsigned int*)(dir_buf + ptr);
        ptr += 4;
        nd->seg_index = *(unsigned int*)(dir_buf + ptr);
        ptr += 4;
        nd->file_size = *(unsigned int*)(dir_buf + ptr);
        ptr += 4;
        // 拷贝文件名
        for(int i=0; i<64; i++)
        {
            nd->node_name[i] = dir_buf[ptr++];
            if(nd->node_name[i] == 0) break;
        }
    }
    return dir_node_cnt;
}

// 根据名称查找目录节点
dir_node_t* dir_find_node_by_name(const char *name)
{
    for(int i=0; i<dir_node_cnt; i++)
    {
        if(strcmp(dir_pool[i].node_name, name) == 0)
            return &dir_pool[i];
    }
    return 0;
}

// 获取节点对应镜像分段索引
int dir_get_node_seg_index(const char *name)
{
    dir_node_t *nd = dir_find_node_by_name(name);
    if(!nd) return -1;
    return nd->seg_index;
}

// 获取子节点数量
int dir_get_child_count(const char *folder_name)
{
    dir_node_t *nd = dir_find_node_by_name(folder_name);
    if(!nd || nd->node_type != DIR_NODE_FOLDER) return -1;
    return nd->child_count;
}

#endif
