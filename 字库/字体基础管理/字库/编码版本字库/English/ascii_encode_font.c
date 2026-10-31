
// ascii_encode_font.c
// 英文 ASCII 编码版本字库层
// 所属目录：编码版本字库/English
// 职责：ASCII编码解析、字符调取、输出标准8*16点阵

#ifndef ASCII_ENCODE_FONT_H
#define ASCII_ENCODE_FONT_H

// 与底层像素字库规格严格对齐
#define ASCII_ENC_WIDTH     8
#define ASCII_ENC_HEIGHT    16
#define ASCII_ENC_BUFLEN    (ASCII_ENC_HEIGHT)

// 外部引用：底层英文像素字库读取接口
extern int ascii_get_dot(unsigned char code, unsigned char *out_buf);

// 清空英文点阵缓冲区
static inline void ascii_encode_buf_clear(unsigned char *buf)
{
    for(int i = 0; i < ASCII_ENC_BUFLEN; i++)
        buf[i] = 0x00;
}

// 对外统一接口（与中文接口命名风格对称，方便管理器统一调用）
// 输入：单字节ASCII编码
// 输出：8*16 标准英文字符点阵
int english_encode_get_font(unsigned char ascii_code, unsigned char *out_dot)
{
    ascii_encode_buf_clear(out_dot);

    // 合法ASCII范围 0~127
    if(ascii_code > 127)
    {
        return 0;
    }

    // 调用底层像素字库获取点阵
    return ascii_get_dot(ascii_code, out_dot);
}

#endif
