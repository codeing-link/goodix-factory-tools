/* ============================================================
 * 文件: src/service/gh_service.c
 * 功能: 业务服务层实现
 * ============================================================ */

#include "gh_service.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ============================================================
 * 内部：transport → service 的帧接收回调
 * 当 transport 层组装好一帧后，调用此函数
 * 对应原 handleSerialReadyRead 中 emit reciveData() 之后的处理流程
 * ============================================================ */
static void s_on_transport_frame(const uint8_t* frame, uint16_t len, void* ctx) {
    gh_service_t* svc = (gh_service_t*)ctx;
    if (!svc || !frame || len < 4) return;

    /* 跳过外层 0xAA11 帧头，送入协议解析器 */
    const uint8_t* inner_pkt = frame + 2;
    uint16_t       inner_len = len - 2;

    gh_parse_packet(&svc->parser, inner_pkt, inner_len);
}

/* ============================================================
 * 内部：protocol → service 的帧解析完成回调
 * 协议层解析完整帧之后调用此函数，向 API 层推数据
 * ============================================================ */
static void s_on_frame_parsed(const gh_frame_t* frame, void* ctx) {
    gh_service_t* svc = (gh_service_t*)ctx;
    if (!svc || !frame) return;

    /* 封装成 API 层使用的精简结构 */
    gh_data_frame_t data;
    memset(&data, 0, sizeof(data));

    data.func_id   = frame->func_id;
    data.frame_cnt = frame->frame_cnt;
    memcpy(data.raw_data, frame->raw_data, sizeof(data.raw_data));
    memcpy(data.algo_result, frame->algo_result.results, sizeof(data.algo_result));
    data.algo_result_num = frame->algo_result.result_num;
    memcpy(data.gsensor, frame->gsensor_data, sizeof(data.gsensor));

    /* 获取当前时间戳 */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    data.timestamp_ms = (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;

    /* 通知 API 层 */
    if (svc->on_data) {
        svc->on_data(&data, svc->on_data_ctx);
    }
}

/* ============================================================
 * 内部：transport → service 的发送函数（协议层回调）
 * protocol 层打包完命令后，通过此函数发送
 * ============================================================ */
static void s_protocol_send(const uint8_t* data, uint16_t len, void* ctx) {
    gh_service_t* svc = (gh_service_t*)ctx;
    if (!svc) return;
    gh_transport_send(&svc->transport, data, len);
}

/* ============================================================
 * 内部：连接状态变化回调
 * ============================================================ */
static void s_on_connect_change(bool connected, void* ctx) {
    gh_service_t* svc = (gh_service_t*)ctx;
    if (!svc) return;

    if (connected) {
        svc->device_state = GH_DEV_STATE_CONNECTED;
        if (svc->on_log) {
            svc->on_log("[Service] Device connected", svc->on_log_ctx);
        }
    } else {
        svc->device_state = GH_DEV_STATE_DISCONNECTED;
        if (svc->on_log) {
            svc->on_log("[Service] Device disconnected", svc->on_log_ctx);
        }
    }

    if (svc->on_state) {
        svc->on_state(svc->device_state, svc->on_state_ctx);
    }
}

/* ============================================================
 * 公开API实现
 * ============================================================ */

void gh_service_init(gh_service_t* svc,
                     gh_svc_on_state_cb on_state, void* state_ctx,
                     gh_svc_on_data_cb on_data, void* data_ctx,
                     gh_svc_on_log_cb on_log, void* log_ctx) {
    if (!svc) return;
    memset(svc, 0, sizeof(gh_service_t));

    svc->on_state     = on_state;
    svc->on_state_ctx = state_ctx;
    svc->on_data      = on_data;
    svc->on_data_ctx  = data_ctx;
    svc->on_log       = on_log;
    svc->on_log_ctx   = log_ctx;
    svc->device_state = GH_DEV_STATE_DISCONNECTED;

    /* 初始化默认串口配置 */
    strncpy(svc->serial_cfg.port, "/dev/ttyUSB0", sizeof(svc->serial_cfg.port));
    svc->serial_cfg.baud_rate  = 400000;
    svc->serial_cfg.data_bits  = 8;
    svc->serial_cfg.parity     = 'N';
    svc->serial_cfg.stop_bits  = 1;
    svc->serial_cfg.flow_control = false;

    /* 初始化 transport 层 */
    gh_transport_init(&svc->transport,
                      s_on_transport_frame, svc,
                      s_on_connect_change, svc,
                      (gh_log_cb)on_log, log_ctx);

    /* 初始化 protocol 解析器，注入发送回调和帧解析回调 */
    gh_parser_init(&svc->parser,
                   s_protocol_send, svc,
                   s_on_frame_parsed, svc);
}

bool gh_service_connect_serial(gh_service_t* svc, const char* port) {
    if (!svc || !port) return false;

    /* 更新端口名 */
    strncpy(svc->serial_cfg.port, port, sizeof(svc->serial_cfg.port));

    /* 打开串口 */
    if (!gh_transport_open_serial(&svc->transport, &svc->serial_cfg)) {
        svc->device_state = GH_DEV_STATE_ERROR;
        if (svc->on_state) svc->on_state(svc->device_state, svc->on_state_ctx);
        return false;
    }

    /* 启动接收线程 */
    gh_transport_start_rx_thread(&svc->transport);
    return true;
}

void gh_service_disconnect(gh_service_t* svc) {
    if (!svc) return;
    gh_transport_stop_rx_thread(&svc->transport);
    gh_transport_close(&svc->transport);
    gh_parser_reset(&svc->parser);
    svc->device_state = GH_DEV_STATE_DISCONNECTED;
}

bool gh_service_is_connected(const gh_service_t* svc) {
    return (svc && gh_transport_is_open(&svc->transport));
}

bool gh_service_start_hbd(gh_service_t* svc,
                           uint8_t ctrl_bit, uint8_t mode, uint32_t func_mask) {
    if (!svc || !gh_service_is_connected(svc)) return false;
    bool ret = gh_cmd_start_hbd(&svc->parser, ctrl_bit, mode, func_mask);
    if (ret && ctrl_bit == 0) {
        svc->device_state = GH_DEV_STATE_SAMPLING;
    } else if (ret && ctrl_bit == 1) {
        svc->device_state = GH_DEV_STATE_CONNECTED;
    }
    if (svc->on_state) svc->on_state(svc->device_state, svc->on_state_ctx);
    return ret;
}

bool gh_service_config_download(gh_service_t* svc,
                                 const gh_reg_t* regs, uint8_t count) {
    if (!svc || !gh_service_is_connected(svc)) return false;
    return gh_cmd_config_download(&svc->parser, regs, count);
}

bool gh_service_register_rw(gh_service_t* svc, const gh_cmd_param_t* param) {
    if (!svc || !gh_service_is_connected(svc) || !param) return false;
    return gh_cmd_register_rw(&svc->parser, param);
}

void gh_service_set_serial_config(gh_service_t* svc, const gh_serial_config_t* cfg) {
    if (!svc || !cfg) return;
    memcpy(&svc->serial_cfg, cfg, sizeof(gh_serial_config_t));
}

gh_device_state_t gh_service_get_state(const gh_service_t* svc) {
    if (!svc) return GH_DEV_STATE_DISCONNECTED;
    return svc->device_state;
}

bool gh_service_cardiff_control(gh_service_t* svc, uint8_t ctrl_val) {
    if (!svc || !gh_service_is_connected(svc)) return false;
    return gh_cmd_cardiff_control(&svc->parser, ctrl_val);
}

bool gh_service_get_evk_version(gh_service_t* svc, uint8_t type) {
    if (!svc || !gh_service_is_connected(svc)) return false;
    return gh_cmd_get_evk_version(&svc->parser, type);
}

bool gh_service_set_work_mode(gh_service_t* svc, uint8_t mode, uint32_t func_mask) {
    if (!svc || !gh_service_is_connected(svc)) return false;
    return gh_cmd_set_work_mode(&svc->parser, mode, func_mask);
}
