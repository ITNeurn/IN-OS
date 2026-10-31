// compile_core.c 轻量编译核心
#ifndef COMPILE_CORE_H
#define COMPILE_CORE_H

// 编译缓存分块大小
#define COMPILE_BLOCK 4096
// 最大编译缓存条目
#define MAX_COMPILE_CACHE 32
// 架构类型
typedef enum {
    ARCH_X86,
    ARCH_ARM,
    ARCH_RISCV
} arch_t;

// 单段编译缓存
typedef struct {
    unsigned int seg_id;
    arch_t target_arch;
    unsigned char bin_data[COMPILE_BLOCK];
    int data_len;
    int compile_pass;
} compile_cache_t;

// 全局编译缓存池
static compile_cache_t compile_pool[MAX_COMPILE_CACHE];
static int compile_cnt = 0;
static unsigned char compile_buf[COMPILE_BLOCK];

static inline void compile_mgr_reset(void)
{
    compile_cnt = 0;
    for(int i=0; i<MAX_COMPILE_CACHE; i++)
    {
        compile_pool[i].seg_id = 0;
        compile_pool[i].target_arch = 0;
        compile_pool[i].data_len = 0;
        compile_pool[i].compile_pass = 0;
        for(int j=0; j<COMPILE_BLOCK; j++)
            compile_pool[i].bin_data[j] = 0x00;
    }
}

// 底层中间码转机器码转换接口
extern int ir_to_machine(arch_t arch, unsigned char *ir_data, int ir_len, unsigned char *out_bin);

/**
 * @brief 单段中间代码编译
 * @param seg_id 分段编号
 * @param arch 目标架构
 * @param ir_buf 中间指令缓冲区
 * @param ir_len 中间码长度
 * @return 1编译成功，0编译报错
 */
int compile_single_segment(int seg_id, arch_t arch, unsigned char *ir_buf, int ir_len)
{
    if(compile_cnt >= MAX_COMPILE_CACHE) return 0;
    compile_cache_t *res = &compile_pool[compile_cnt++];
    res->seg_id = seg_id;
    res->target_arch = arch;
    // 调用底层翻译转换
    int bin_len = ir_to_machine(arch, ir_buf, ir_len, compile_buf);
    res->data_len = bin_len;
    res->compile_pass = (bin_len > 0) ? 1 : 0;
    // 拷贝编译后二进制
    for(int i=0; i<bin_len; i++)
        res->bin_data[i] = compile_buf[i];
    return res->compile_pass;
}

/**
 * @brief 通过分段ID获取编译产物
 * @param seg_id 分段编号
 * @param out_bin 输出二进制缓冲区
 * @param out_len 输出长度
 * @return 1存在缓存，0无此分段编译结果
 */
int compile_get_output(int seg_id, unsigned char *out_bin, int *out_len)
{
    for(int i=0; i<compile_cnt; i++)
    {
        if(compile_pool[i].seg_id == seg_id)
        {
            *out_len = compile_pool[i].data_len;
            for(int j=0; j<*out_len; j++)
                out_bin[j] = compile_pool[i].bin_data[j];
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 批量获取编译失败分段数量
 * @return 失败段总数
 */
int compile_get_fail_count(void)
{
    int fail = 0;
    for(int i=0; i<compile_cnt; i++)
        if(compile_pool[i].compile_pass == 0) fail++;
    return fail;
}

/**
 * @brief 清空全部编译缓存，释放内存
 */
void compile_cache_flush(void)
{
    compile_mgr_reset();
}

/**
 * @brief 增量重编译指定分段（更新中间码后复用缓存）
 * @param seg_id 分段ID
 * @param arch 目标架构
 * @param ir_data 新中间码
 * @param ir_len 中间码长度
 * @return 重编译结果
 */
int compile_recompile_segment(int seg_id, arch_t arch, unsigned char *ir_data, int ir_len)
{
    // 移除旧缓存条目
    for(int i=0; i<compile_cnt; i++)
    {
        if(compile_pool[i].seg_id == seg_id)
        {
            for(int j=i; j<compile_cnt-1; j++)
                compile_pool[j] = compile_pool[j+1];
            compile_cnt--;
            break;
        }
    }
    return compile_single_segment(seg_id, arch, ir_data, ir_len);
}

#endif
