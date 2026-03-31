/* ============================================================
 * 文件: src/transport/gh_transport.h
 * 功能: 通信层抽象接口（串口 / BLE 统一抽象）
 * 说明: 纯C，使用POSIX API，替换原 QSerialPort + BLEManager
 *       分帧逻辑（原MainWindow::handleSerialReadyRead）也在此层
 * ============================================================ */

#ifndef GH_TRANSPORT_H
#define GH_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../port/gh_os_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 帧起始标志 */
#define GH_FRAME_MARKER0    (0xAAU)
#define GH_FRAME_MARKER1    (0x11U)

/* 接收缓冲区大小 */
#define GH_TRANSPORT_RX_BUF_SIZE    (8192U)

/* 帧超时时间（毫秒），原 frameTimeoutMs = 100 */
#define GH_FRAME_TIMEOUT_MS         (100U)

/* ============================================================
 * 通信层回调
 * ============================================================ */

/**
 * @brief 完整帧接收回调（对应原 emit reciveData()）
 * @param frame     帧数据指针（包含 0xAA11 帧头）
 * @param len       帧长度
 * @param user_ctx  用户上下文
 */
typedef void (*gh_on_frame_cb)(const uint8_t* frame, uint16_t len, void* user_ctx);

/**
 * @brief 连接状态变化回调
 * @param connected  是否已连接
 * @param user_ctx   用户上下文
 */
typedef void (*gh_on_connect_cb)(bool connected, void* user_ctx);

/**
 * @brief 日志回调（向上层汇报通信层日志）
 */
typedef void (*gh_log_cb)(const char* msg, void* user_ctx);

/* ============================================================
 * 串口配置（对应原 SerialConfig，去Qt化）
 * ============================================================ */

typedef struct {
    char     port[64];       /* 串口设备名，如 "/dev/ttyUSB0" 或 "COM3" */
    int      baud_rate;      /* 波特率，如 115200 */
    int      data_bits;      /* 数据位，通常 8 */
    char     parity;         /* 校验: 'N'=None, 'E'=Even, 'O'=Odd */
    int      stop_bits;      /* 停止位: 1 或 2 */
    bool     flow_control;   /* 流控: false=无, true=硬件流控 */
} gh_serial_config_t;

/* ============================================================
 * Transport 上下文
 * ============================================================ */

typedef struct {
    /* 串口文件描述符或句柄（-1 = 未打开）*/
    intptr_t serial_fd;

    /* 串口配置 */
    gh_serial_config_t serial_cfg;

    /* 接收缓冲区（环形or线性，用于帧分割）*/
    uint8_t  rx_buf[GH_TRANSPORT_RX_BUF_SIZE];
    uint16_t rx_head;    /* 有效数据起始偏移 */
    uint16_t rx_tail;    /* 有效数据尾偏移（写入位置）*/

    /* 帧超时状态（替代 QTimer）*/
    uint32_t last_rx_time_ms; /* 最近一次收到数据的时间戳（ms）*/
    bool     frame_timeout_armed; /* 是否在等待超时 */

    /* 回调 */
    gh_on_frame_cb   on_frame;
    void*            on_frame_ctx;
    gh_on_connect_cb on_connect;
    void*            on_connect_ctx;
    gh_log_cb        log_cb;
    void*            log_ctx;

    /* 接收线程运行标志 */
    volatile bool rx_thread_running;
    gh_thread_t    rx_thread;
    bool           rx_thread_started;
} gh_transport_t;

/* ============================================================
 * 接口函数声明
 * ============================================================ */

/**
 * @brief 初始化通信层
 * @param t          通信层上下文指针
 * @param on_frame   帧接收回调
 * @param frame_ctx  回调用户数据
 * @param on_connect 连接状态回调
 * @param conn_ctx   回调用户数据
 * @param log_cb     日志回调（可为NULL）
 * @param log_ctx    日志回调用户数据
 */
void gh_transport_init(gh_transport_t* t,
                       gh_on_frame_cb on_frame, void* frame_ctx,
                       gh_on_connect_cb on_connect, void* conn_ctx,
                       gh_log_cb log_cb, void* log_ctx);

/**
 * @brief 打开串口（对应原 serial->open()）
 * @param t    通信层上下文
 * @param cfg  串口配置
 * @return true=打开成功
 */
bool gh_transport_open_serial(gh_transport_t* t, const gh_serial_config_t* cfg);

/**
 * @brief 关闭串口（对应原 serial->close()）
 * @param t    通信层上下文
 */
void gh_transport_close(gh_transport_t* t);

/**
 * @brief 发送数据（对应原 serial->write()）
 * @param t    通信层上下文
 * @param data 数据指针
 * @param len  数据长度
 * @return true=发送成功
 */
bool gh_transport_send(gh_transport_t* t, const uint8_t* data, uint16_t len);

/**
 * @brief 是否已连接
 * @param t    通信层上下文
 * @return true=已连接
 */
bool gh_transport_is_open(const gh_transport_t* t);

/**
 * @brief 启动接收线程（使用pthread，对应原 QSerialPort::readyRead 信号机制）
 * @param t    通信层上下文
 * @return true=成功
 */
bool gh_transport_start_rx_thread(gh_transport_t* t);

/**
 * @brief 停止接收线程
 * @param t    通信层上下文
 */
void gh_transport_stop_rx_thread(gh_transport_t* t);

/**
 * @brief 处理接收到的原始字节（分帧逻辑，原 handleSerialReadyRead）
 *
 * 此函数从接收缓冲区中扫描 0xAA11 帧头，
 * 提取完整帧并通过 on_frame 回调通知上层。
 * 若有残余数据超过 GH_FRAME_TIMEOUT_MS 无新数据，触发超时处理。
 *
 * @param t     通信层上下文
 * @param data  新收到的原始字节
 * @param len   字节长度
 */
void gh_transport_feed(gh_transport_t* t, const uint8_t* data, uint16_t len);

/**
 * @brief 获取当前时间戳（毫秒，平台相关实现）
 * @return 单调时间（ms）
 */
uint32_t gh_platform_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* GH_TRANSPORT_H */
