/* ============================================================
 * 文件: src/api/gh_http_server.h
 * 功能: 基于 mongoose 的 HTTP + WebSocket 服务器接口
 *       - HTTP REST API 供前端控制设备
 *       - WebSocket 实时推送传感器数据帧
 *       - 线程安全的帧队列（模拟器线程 / 串口线程 → 主循环广播）
 * 依赖: mongoose.h（CMake 自动下载）
 * ============================================================ */

#ifndef GH_HTTP_SERVER_H
#define GH_HTTP_SERVER_H

#include "../service/gh_service.h"
#include "../port/gh_os_port.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 帧队列容量（环形缓冲） */
#define GH_API_QUEUE_SIZE   64

/* ============================================================
 * 线程安全帧队列
 * 模拟器线程 / 串口接收线程 向此队列 push，
 * mongoose 主事件循环 drain 队列后 broadcast 到所有 WS 客户端
 * ============================================================ */
typedef struct {
    gh_data_frame_t frames[GH_API_QUEUE_SIZE];
    int             head;       /* 读指针 */
    int             tail;       /* 写指针 */
    int             count;      /* 当前队列长度 */
    gh_mutex_t      lock;       /* 保护并发访问 */
} gh_frame_queue_t;

/* ============================================================
 * API 服务器上下文
 * ============================================================ */
typedef struct {
    void*            mgr;           /* struct mg_mgr*（不暴露mongoose类型）*/
    gh_service_t*    service;       /* 业务层指针（可为NULL，使用模拟器）*/
    gh_frame_queue_t queue;         /* 待广播帧队列 */
    char             web_root[512]; /* 前端静态文件目录路径 */
    char             listen_url[64];/* 监听地址，如 "http://0.0.0.0:8080" */
    volatile bool    running;       /* 事件循环开关 */

    /* 最新一帧（供 GET /api/device/data 轮询使用）*/
    gh_data_frame_t  latest_frame;
    bool             has_frame;
} gh_api_t;

/* ============================================================
 * 接口函数
 * ============================================================ */

/**
 * @brief 初始化 HTTP/WebSocket 服务器
 * @param api       API上下文指针
 * @param service   业务层指针（可NULL）
 * @param http_port 监听端口，如 8080
 * @param web_root  前端目录路径，如 "web/frontend"
 * @return true=成功
 */
bool gh_api_init(gh_api_t *api, gh_service_t *service,
                 int http_port, const char *web_root);

/**
 * @brief 运行事件循环（阻塞，直到 gh_api_stop() 被调用）
 *        在此函数内部 drain 帧队列并 WebSocket 广播
 * @param api  API上下文
 */
void gh_api_run(gh_api_t *api);

/**
 * @brief 停止事件循环（可从任意线程或信号处理器调用）
 * @param api  API上下文
 */
void gh_api_stop(gh_api_t *api);

/**
 * @brief 推送数据帧（线程安全）
 *        可从串口接收线程、模拟器线程调用
 *        实际广播在 mongoose 主线程的 poll 间隙完成
 * @param api   API上下文
 * @param frame 要广播的帧
 */
void gh_api_push_frame(gh_api_t *api, const gh_data_frame_t *frame);

/**
 * @brief 释放资源
 * @param api  API上下文
 */
void gh_api_destroy(gh_api_t *api);

#ifdef __cplusplus
}
#endif

#endif /* GH_HTTP_SERVER_H */
