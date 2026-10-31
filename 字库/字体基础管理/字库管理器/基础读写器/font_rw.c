// font_edit_commit.c
// 字库修改管理、批量提交模块
#ifndef FONT_EDIT_COMMIT_H
#define FONT_EDIT_COMMIT_H

// 引入基础读写器接口
extern int font_file_open(const char *path, int mode);
extern int font_file_write(int handle_idx, unsigned char *buf, int len);
extern void font_file_seek(int handle_idx, unsigned int off);
extern void font_file_close(int handle_idx);

// 单次修改最大缓存大小
#define EDIT_CACHE_MAX 1024
typedef struct {
    unsigned int file_offset;
    unsigned int data_len;
    unsigned char data[EDIT_CACHE_MAX];
} font_edit_item_t;

#define MAX_EDIT_ITEM 16
static font_edit_item_t edit_cache[MAX_EDIT_ITEM];
static int edit_item_count = 0;

// 清空编辑缓存
static inline void edit_cache_clear(void)
{
    edit_item_count = 0;
    for(int i=0;i<MAX_EDIT_ITEM;i++){
        edit_cache[i].file_offset = 0;
        edit_cache[i].data_len = 0;
        for(int j=0;j<EDIT_CACHE_MAX;j++) edit_cache[i].data[j]=0;
    }
}

// 新增一条修改缓存
int font_edit_add(unsigned int offset, unsigned char *data, int len)
{
    if(edit_item_count >= MAX_EDIT_ITEM || len > EDIT_CACHE_MAX) return 0;
    font_edit_item_t *item = &edit_cache[edit_item_count];
    item->file_offset = offset;
    item->data_len = len;
    for(int i=0;i<len;i++) item->data[i] = data[i];
    edit_item_count++;
    return 1;
}

// 批量提交所有修改到基础读写器写入磁盘
int font_edit_submit(int file_handle)
{
    int success = 1;
    for(int i=0;i<edit_item_count;i++){
        font_edit_item_t *item = &edit_cache[i];
        font_file_seek(file_handle, item->file_offset);
        int w_ret = font_file_write(file_handle, item->data, item->data_len);
        if(w_ret != item->data_len) success = 0;
    }
    // 提交完成清空缓存
    edit_cache_clear();
    return success;
}

// 丢弃未提交修改
void font_edit_discard(void)
{
    edit_cache_clear();
}

#endif
