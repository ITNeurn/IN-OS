
// chinses_encode_font.c
// 中文 GB2312 编码版本字库层
// 所属目录：编码版本字库/chinses
// 职责：编码解析、编码→五笔字根映射、统一输出点阵给显示驱动

#ifndef CHINESE_ENCODE_FONT_H
#define CHINESE_ENCODE_FONT_H

// 统一系统中文字体规格（与像素字库一致）
#define CN_ENC_WIDTH     16
#define CN_ENC_HEIGHT    16
#define CN_ENC_BUFLEN    (CN_ENC_WIDTH * CN_ENC_HEIGHT / 8)

// GB2312 区位码结构
typedef struct {
    unsigned char qu;     // 区码
    unsigned char wei;    // 位码
} gb2312_pos_t;

// 汉字编码映射表：GB2312 区位码 → 五笔字根组合
typedef struct {
    gb2312_pos_t  pos;
    unsigned int  root_idx[4];
    int           root_cnt;
} gb2312_wubi_map_t;

// 外部引用：像素字库五笔拼装接口
// （自动联动你之前的 wubi_chinese_font.c）
extern void wubi_make_char(unsigned char *out_buf, const unsigned int *root_idx, int root_cnt);

// ===================== 常用中文 GB2312-五笔映射表 =====================
static const gb2312_wubi_map_t encode_wubi_table[] =
{
    // 一 (GB2312 0101)
    {{0x01,0x01}, {0}, 1},
    // 人 (0144)
    {{0x01,0x44}, {14}, 1},
    // 口 (0375)
    {{0x03,0x75}, {10}, 1},
    // 日 (2528)
    {{0x25,0x28}, {11}, 1},
    // 月 (5238)
    {{0x52,0x38}, {12}, 1},
    // 木 (3192)
    {{0x31,0x92}, {13}, 1},
    // 仁 (0145)
    {{0x01,0x45}, {7,0}, 2},
    // 冰 (1603)
    {{0x16,0x03}, {5,0}, 2},
    // 江 (2913)
    {{0x29,0x13}, {6,10}, 2},
    // 打 (2134)
    {{0x21,0x34}, {8,0}, 2}
};
#define ENCODE_WUBI_COUNT (sizeof(encode_wubi_table) / sizeof(gb2312_wubi_map_t))

// ===================== 内部工具函数 =====================
static inline void encode_buf_clear(unsigned char *buf)
{
    for(int i = 0; i < CN_ENC_BUFLEN; i++)
        buf[i] = 0x00;
}

// 将两字节GB2312编码转成区位码
static void gb2312_to_pos(unsigned char h, unsigned char l, gb2312_pos_t *out)
{
    // GB2312 编码规则：0xA1 起始偏移
    out->qu  = h - 0xA1;
    out->wei = l - 0xA1;
}

// ===================== 系统对外核心接口（给字库管理器调用） =====================
// 输入：GB2312 高字节、低字节
// 输出：16*16 完整汉字点阵
int chinese_encode_get_font(unsigned char h_byte, unsigned char l_byte, unsigned char *out_dot)
{
    encode_buf_clear(out_dot);

    gb2312_pos_t pos;
    gb2312_to_pos(h_byte, l_byte, &pos);

    // 遍历编码表匹配五笔字根
    for(int i = 0; i < ENCODE_WUBI_COUNT; i++)
    {
        if(encode_wubi_table[i].pos.qu == pos.qu &&
           encode_wubi_table[i].pos.wei == pos.wei)
        {
            // 调用像素字库拼装点阵
            wubi_make_char(out_dot,
                           encode_wubi_table[i].root_idx,
                           encode_wubi_table[i].root_cnt);
            return 1;
        }
    }

    // 无匹配字符
    return 0;
}

#endif
