/* ============================================================
 * 文件: src/service/gh_service.h
 * 功能: 业务逻辑层接口（设备状态管理、命令调度、数据路由）
 * 说明: 连接 transport 层和 api 层，是系统的"控制中心"
 *       替换原 CardiffBTabWidget + ConfigManager 的业务行为
 * ============================================================ */

#ifndef GH_SERVICE_H
#define GH_SERVICE_H

#include "../protocol/gh_protocol.h"
#include "../transport/gh_transport.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 设备状态枚举
 * ============================================================ */
typedef enum {
    GH_DEV_STATE_DISCONNECTED = 0,  /* 未连接 */
    GH_DEV_STATE_CONNECTED    = 1,  /* 已连接，未采样 */
    GH_DEV_STATE_SAMPLING     = 2,  /* 采样中 */
    GH_DEV_STATE_ERROR        = 3,  /* 错误 */
} gh_device_state_t;

/* ============================================================
 * 配置结构（对应原 CommonConfig + SerialConfig，去Qt化）
 * ============================================================ */
typedef struct {
    char     data_save_path[256]; /* 数据保存路径 */
    char     mode[16];            /* 通信模式: "serial" 或 "ble" */
    bool     auto_reconnect;      /* 是否自动重连 */
} gh_common_config_t;

/* ============================================================
 * 实时数据帧（向 API 层推送的简化结构）
 * ============================================================ */
typedef struct {
    uint32_t func_id;              /* 功能ID */
    uint32_t frame_cnt;            /* 帧序号 */
    uint32_t raw_data[32];         /* 原始传感数据 */
    int32_t  algo_result[8];       /* 算法结果 */
    uint8_t  algo_result_num;      /* 有效算法结果数 */
    int16_t  gsensor[6];           /* G-sensor数据 */
    uint64_t timestamp_ms;         /* 时间戳（ms） */
} gh_data_frame_t;

/* ============================================================
 * 服务向 API 层推送的事件回调类型
 * ============================================================ */

/** 设备状态变化回调 */
typedef void (*gh_svc_on_state_cb)(gh_device_state_t state, void* ctx);

/** 数据帧到达回调（实时数据推送）*/
typedef void (*gh_svc_on_data_cb)(const gh_data_frame_t* frame, void* ctx);

/** 日志消息回调 */
typedef void (*gh_svc_on_log_cb)(const char* msg, void* ctx);

/* ============================================================
 * 服务层上下文
 * ============================================================ */
typedef struct {
    gh_transport_t   transport;    /* 通信层上下文（嵌入，非指针）*/
    gh_parser_ctx_t  parser;       /* 协议解析器上下文 */
    gh_common_config_t common_cfg; /* 通用配置 */
    gh_serial_config_t serial_cfg; /* 串口配置 */

    gh_device_state_t device_state; /* 当前设备状态 */

    /* 向 API 层的回调 */
    gh_svc_on_state_cb on_state;
    void*              on_state_ctx;
    gh_svc_on_data_cb  on_data;
    void*              on_data_ctx;
    gh_svc_on_log_cb   on_log;
    void*              on_log_ctx;
} gh_service_t;

/* ============================================================
 * 接口函数声明
 * ============================================================ */

/**
 * @brief 初始化服务层
 * @param svc          服务层上下文
 * @param on_state     状态变化回调
 * @param state_ctx    状态回调用户数据
 * @param on_data      数据帧到达回调
 * @param data_ctx     数据回调用户数据
 * @param on_log       日志回调
 * @param log_ctx      日志回调用户数据
 */
void gh_service_init(gh_service_t* svc,
                     gh_svc_on_state_cb on_state, void* state_ctx,
                     gh_svc_on_data_cb on_data, void* data_ctx,
                     gh_svc_on_log_cb on_log, void* log_ctx);

/**
 * @brief 连接串口设备（对应原 on_serialOpenBtn_clicked）
 * @param svc  服务层上下文
 * @param port 串口名（如 "/dev/ttyUSB0"）
 * @return true=连接成功
 */
bool gh_service_connect_serial(gh_service_t* svc, const char* port);

/**
 * @brief 断开连接（对应原 serial->close()）
 * @param svc  服务层上下文
 */
void gh_service_disconnect(gh_service_t* svc);

/**
 * @brief 是否已连接
 * @param svc  服务层上下文
 * @return true=已连接
 */
bool gh_service_is_connected(const gh_service_t* svc);

/**
 * @brief 发送 StartHBD 命令（开始/停止采样）
 * @param svc       服务层上下文
 * @param ctrl_bit  0=开启, 1=关闭
 * @param mode      采样模式
 * @param func_mask 功能掩码（使用 GH_FUNC_MASK_XXX）
 * @return true=发送成功
 */
bool gh_service_start_hbd(gh_service_t* svc,
                           uint8_t ctrl_bit, uint8_t mode, uint32_t func_mask);

/**
 * @brief 下发配置（寄存器列表）
 * @param svc   服务层上下文
 * @param regs  寄存器数组
 * @param count 寄存器数量
 * @return true=发送成功
 */
bool gh_service_config_download(gh_service_t* svc,
                                 const gh_reg_t* regs, uint8_t count);

/**
 * @brief 读写单个寄存器
 * @param svc    服务层上下文
 * @param param  命令参数
 * @return true=发送成功
 */
bool gh_service_register_rw(gh_service_t* svc, const gh_cmd_param_t* param);

/**
 * @brief 设置串口配置（不立即重连）
 * @param svc  服务层上下文
 * @param cfg  串口配置
 */
void gh_service_set_serial_config(gh_service_t* svc, const gh_serial_config_t* cfg);

/**
 * @brief 获取当前设备状态
 * @param svc  服务层上下文
 * @return 设备状态
 */
gh_device_state_t gh_service_get_state(const gh_service_t* svc);

/* 功能掩码常量 */
#define GH_FUNC_MASK_ADT   (0x01U)   /* ADT */
#define GH_FUNC_MASK_HR    (0x02U)   /* 心率 */
#define GH_FUNC_MASK_HRV   (0x04U)   /* HRV */
#define GH_FUNC_MASK_HSM   (0x08U)   /* HSM */
#define GH_FUNC_MASK_BP    (0x10U)   /* 血压 */
#define GH_FUNC_MASK_SPO2  (0x20U)   /* 血氧 */
#define GH_FUNC_MASK_ECG   (0x40U)   /* ECG */
#define GH_FUNC_MASK_PT    (0x80U)   /* 体温 */

#ifdef __cplusplus
}
#endif

#endif /* GH_SERVICE_H */
