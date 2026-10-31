// receive_port.c 指令接收端口驱动
#ifndef RECEIVE_PORT_H
#define RECEIVE_PORT_H

#include <stdint.h>

// 端口缓冲区规格
#define PORT_RX_BUFFER 16384
#define MAX_PORT_LISTEN 16

// 端口状态
typedef enum {
    PORT_CLOSED,
    PORT_LISTEN,
    PORT_DATA_READY
} port_state_t;

// 监听端口结构体
typedef struct {
    uint16_t port_num;
    port_state_t state;
    uint32_t data_len;
    unsigned char rx_buf[PORT_RX_BUFFER];
} port_entry_t;

// 全局端口池
static port_entry_t port_pool[MAX_PORT_LISTEN];
static int port_count = 0;

static inline void port_mgr_reset(void)
{
    port_count = 0;
    for(int i = 0; i < MAX_PORT_LISTEN; i++)
    {
        port_pool[i].port_num = 0;
        port_pool[i].state = PORT_CLOSED;
        port_pool[i].data_len = 0;
        memset(port_pool[i].rx_buf, 0, PORT_RX_BUFFER);
    }
}

// 底层内核端口IO系统调用
extern int sys_port_listen(uint16_t port);
extern int sys_port_recv(uint16_t port, unsigned char *out, int max_len);
extern void sys_port_close(uint16_t port);

/**
 * @brief 开启监听指定端口
 * @param port 端口号
 * @return 1监听成功，0端口池满/已占用
 */
int port_open(uint16_t port)
{
    // 查重
    for(int i = 0; i < port_count; i++)
        if(port_pool[i].port_num == port) return 0;
    if(port_count >= MAX_PORT_LISTEN) return 0;
    if(!sys_port_listen(port)) return 0;
    port_entry_t *ent = &port_pool[port_count++];
    ent->port_num = port;
    ent->state = PORT_LISTEN;
    return 1;
}

/**
 * @brief 阻塞接收端口X86指令流
 * @param port 端口号
 * @param out_data 输出指令缓冲区
 * @param out_len 输出有效长度
 * @return 1收到数据，0无数据/端口未打开
 */
int port_recv_x86_stream(uint16_t port, unsigned char *out_data, uint32_t *out_len)
{
    // 查找端口
    int idx = -1;
    for(int i = 0; i < port_count; i++)
    {
        if(port_pool[i].port_num == port)
        {
            idx = i;
            break;
        }
    }
    if(idx == -1) return 0;
    port_entry_t *ent = &port_pool[idx];
    int recv_len = sys_port_recv(port, ent->rx_buf, PORT_RX_BUFFER);
    if(recv_len <= 0) return 0;
    ent->data_len = recv_len;
    ent->state = PORT_DATA_READY;
    // 拷贝输出
    *out_len = recv_len;
    memcpy(out_data, ent->rx_buf, recv_len);
    return 1;
}

/**
 * @brief 关闭监听端口，释放资源
 * @param port 端口号
 * @return 1关闭成功
 */
int port_close(uint16_t port)
{
    int idx = -1;
    for(int i = 0; i < port_count; i++)
    {
        if(port_pool[i].port_num == port)
        {
            idx = i;
            break;
        }
    }
    if(idx == -1) return 0;
    sys_port_close(port);
    // 移除条目
    for(int i = idx; i < port_count - 1; i++)
        port_pool[i] = port_pool[i + 1];
    port_count--;
    return 1;
}

/**
 * @brief 批量关闭所有监听端口（系统退出）
 */
void port_close_all(void)
{
    for(int i = 0; i < port_count; i++)
        sys_port_close(port_pool[i].port_num);
    port_mgr_reset();
}

/**
 * @brief 查询端口当前状态
 * @param port 端口号
 * @return port_state_t
 */
port_state_t port_get_state(uint16_t port)
{
    for(int i = 0; i < port_count; i++)
        if(port_pool[i].port_num == port)
            return port_pool[i].state;
    return PORT_CLOSED;
}

#endif
