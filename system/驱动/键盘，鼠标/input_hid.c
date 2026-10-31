// input_hid.c 键鼠HID底层输入驱动
#ifndef INPUT_HID_H
#define INPUT_HID_H

#include <stdint.h>
#include <string.h>

// 缓冲区常量
#define HID_RAW_BUF 1024
#define MAX_HID_DEVICE 4

// 设备类型
typedef enum {
    HID_DEV_KEYBOARD,
    HID_DEV_MOUSE,
    HID_DEV_COMBO
} hid_dev_type_t;

// 按键掩码定义
#define KEY_CTRL    (1 << 0)
#define KEY_SHIFT   (1 << 1)
#define KEY_ALT     (1 << 2)
#define KEY_WIN     (1 << 3)

// 鼠标状态结构体
typedef struct {
    int16_t dx;
    int16_t dy;
    int8_t btn_left;
    int8_t btn_right;
    int8_t btn_mid;
    int16_t scroll;
} mouse_state_t;

// 键盘状态结构体
typedef struct {
    uint8_t modifier_mask;
    uint8_t keycode_list[6];
} kbd_state_t;

// HID设备描述符
typedef struct {
    uint32_t dev_fd;
    hid_dev_type_t dev_type;
    uint8_t active;
    mouse_state_t mouse;
    kbd_state_t kbd;
} hid_device_t;

static hid_device_t hid_pool[MAX_HID_DEVICE];
static int hid_dev_cnt = 0;
static unsigned char hid_raw_buf[HID_RAW_BUF];

// 底层硬件中断系统调用
extern uint32_t sys_hid_open(const char *dev_path);
extern void sys_hid_close(uint32_t fd);
extern int sys_hid_poll(uint32_t fd, unsigned char *out, int max);

static inline void hid_reset_pool(void)
{
    hid_dev_cnt = 0;
    memset(hid_raw_buf, 0, HID_RAW_BUF);
    for(int i = 0; i < MAX_HID_DEVICE; i++)
    {
        hid_pool[i].dev_fd = 0;
        hid_pool[i].dev_type = HID_DEV_KEYBOARD;
        hid_pool[i].active = 0;
        memset(&hid_pool[i].mouse, 0, sizeof(mouse_state_t));
        memset(&hid_pool[i].kbd, 0, sizeof(kbd_state_t));
    }
}

/**
 * @brief 打开HID键鼠设备
 * @param dev_path 硬件设备路径
 * @param type 设备类型
 * @return 设备池索引，-1失败
 */
int hid_open_device(const char *dev_path, hid_dev_type_t type)
{
    if(hid_dev_cnt >= MAX_HID_DEVICE) return -1;
    uint32_t fd = sys_hid_open(dev_path);
    if(fd == 0) return -1;

    hid_device_t *dev = &hid_pool[hid_dev_cnt];
    dev->dev_fd = fd;
    dev->dev_type = type;
    dev->active = 1;
    return hid_dev_cnt++;
}

/**
 * @brief 轮询读取原始HID数据包，解析键鼠状态
 * @param dev_idx 设备索引
 * @return 1有新输入，0无数据
 */
int hid_poll_input(int dev_idx)
{
    if(dev_idx < 0 || dev_idx >= hid_dev_cnt) return 0;
    hid_device_t *dev = &hid_pool[dev_idx];
    if(!dev->active) return 0;

    int recv = sys_hid_poll(dev->dev_fd, hid_raw_buf, HID_RAW_BUF);
    if(recv <= 0) return 0;

    // 键盘解析
    if(dev->dev_type == HID_DEV_KEYBOARD || dev->dev_type == HID_DEV_COMBO)
    {
        dev->kbd.modifier_mask = hid_raw_buf[0];
        memset(dev->kbd.keycode_list, 0, 6);
        for(int i = 0; i < 6; i++)
            dev->kbd.keycode_list[i] = hid_raw_buf[2 + i];
    }

    // 鼠标解析
    if(dev->dev_type == HID_DEV_MOUSE || dev->dev_type == HID_DEV_COMBO)
    {
        dev->mouse.btn_left  = hid_raw_buf[0] & 0x01;
        dev->mouse.btn_right = hid_raw_buf[0] & 0x02;
        dev->mouse.btn_mid   = hid_raw_buf[0] & 0x04;
        dev->mouse.dx = (int16_t)((hid_raw_buf[2] << 8) | hid_raw_buf[1]);
        dev->mouse.dy = (int16_t)((hid_raw_buf[4] << 8) | hid_raw_buf[3]);
        dev->mouse.scroll = (int8_t)hid_raw_buf[5];
    }
    return 1;
}

/**
 * @brief 获取当前键盘完整状态
 */
void hid_get_kbd_state(int dev_idx, kbd_state_t *out)
{
    if(dev_idx < 0 || dev_idx >= hid_dev_cnt) return;
    memcpy(out, &hid_pool[dev_idx].kbd, sizeof(kbd_state_t));
}

/**
 * @brief 获取当前鼠标完整状态
 */
void hid_get_mouse_state(int dev_idx, mouse_state_t *out)
{
    if(dev_idx < 0 || dev_idx >= hid_dev_cnt) return;
    memcpy(out, &hid_pool[dev_idx].mouse, sizeof(mouse_state_t));
}

/**
 * @brief 关闭指定HID设备
 */
void hid_close_device(int dev_idx)
{
    if(dev_idx < 0 || dev_idx >= hid_dev_cnt) return;
    hid_device_t *dev = &hid_pool[dev_idx];
    if(dev->active)
    {
        sys_hid_close(dev->dev_fd);
        dev->active = 0;
    }
}

/**
 * @brief 全部设备释放（系统退出）
 */
void hid_close_all(void)
{
    for(int i = 0; i < hid_dev_cnt; i++)
        hid_close_device(i);
    hid_reset_pool();
}

#endif
