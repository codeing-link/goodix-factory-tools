/* ============================================================
 * 文件: src/api/gh_http_server.c
 * 功能: 基于 mongoose 的完整 HTTP + WebSocket 服务器实现
 *
 * 架构说明:
 *   1. 单线程 mongoose 事件循环（主线程）
 *   2. 外部线程（模拟器/串口）通过 gh_api_push_frame() 写入队列
 *   3. 每次 mg_mgr_poll() 后 drain 队列，broadcast 到所有 WS 客户端
 *
 * HTTP 端点:
 *   GET  /                     → 前端静态文件
 *   OPTIONS *                  → CORS 预检
 *   GET  /api/device/status    → 设备状态 JSON
 *   POST /api/device/connect   → 连接串口/BLE
 *   POST /api/device/disconnect→ 断开连接
 *   POST /api/device/start     → 开始/停止采样
 *   POST /api/device/config    → 寄存器配置下发
 *   GET  /api/device/data      → 最新帧（轮询备用）
 *   GET  /api/serial/list      → 列出可用串口
 *   WS   /ws                   → 实时数据 WebSocket
 * ============================================================ */

#include "gh_http_server.h"
#include "mongoose.h"   /* 由 CMake 从 third_party/ 目录引入 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ============================================================
 * 常量
 * ============================================================ */

/* CORS 响应头（允许任意来源，开发环境使用）*/
#define CORS_HEADERS \
    "Access-Control-Allow-Origin: *\r\n"               \
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n" \
    "Access-Control-Allow-Headers: Content-Type\r\n"

/* WebSocket 标记（存在 mg_connection->data[0]）*/
#define WS_MARK     'W'
#define GH_HTTP_MAX_CONFIG_REGS 200

/* ============================================================
 * 内部辅助：简单 JSON 字段提取
 * 支持 {"key":"value"} 和 {"key":123} 两种格式
 * ============================================================ */

/**
 * @brief 从 JSON 字符串中提取字符串字段值
 * @return true=成功
 */
static bool s_json_str(const char *body, size_t blen,
                       const char *key, char *out, size_t out_size) {
    /* 搜索 "key": ，使用标准 strstr 避免 mongoose API 差异 */
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (blen == 0 || !body) return false;
    const char *found = strstr(body, pat);
    if (!found) return false;
    /* 跳过 key 和 ": */
    found += strlen(pat);
    while (*found == ' ' || *found == ':' || *found == '\t') found++;
    if (*found != '"') return false;
    found++; /* 跳过开头的引号 */
    size_t i = 0;
    while (*found && *found != '"' && i < out_size - 1) {
        out[i++] = *found++;
    }
    out[i] = '\0';
    return i > 0;
}

/**
 * @brief 从 JSON 字符串中提取整数字段值
 * @return 找到的整数值，否则返回 dflt
 */
static long s_json_long(const char *body, size_t blen,
                        const char *key, long dflt) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (blen == 0 || !body) return dflt;
    const char *found = strstr(body, pat);
    if (!found) return dflt;
    found += strlen(pat);
    while (*found == ' ' || *found == ':' || *found == '\t') found++;
    long val;
    if (sscanf(found, "%ld", &val) == 1) return val;
    return dflt;
}

/* ============================================================
 * 内部辅助：JSON 响应构造
 * ============================================================ */

/** 成功响应 */
static void s_reply_ok(struct mg_connection *c, const char *data_json) {
    mg_http_reply(c, 200,
        "Content-Type: application/json\r\n" CORS_HEADERS,
        "{\"code\":0,\"msg\":\"ok\",\"data\":%s}",
        data_json ? data_json : "null");
}

/** 错误响应 */
static void s_reply_err(struct mg_connection *c, int code, const char *msg) {
    mg_http_reply(c, 200,
        "Content-Type: application/json\r\n" CORS_HEADERS,
        "{\"code\":%d,\"msg\":\"%s\",\"data\":null}",
        code, msg ? msg : "error");
}

/* ============================================================
 * 内部辅助：WebSocket 广播日志消息
 * ============================================================ */
static void s_ws_broadcast_log(struct mg_mgr *mgr, const gh_log_msg_t *msg) {
    char json[GH_API_LOG_MAX_LEN + 64];
    /* 对 text 中的双引号进行简单转义 */
    char escaped[GH_API_LOG_MAX_LEN * 2];
    size_t j = 0;
    for (size_t i = 0; msg->text[i] && j < sizeof(escaped) - 2; i++) {
        if (msg->text[i] == '"' || msg->text[i] == '\\') {
            escaped[j++] = '\\';
        }
        escaped[j++] = msg->text[i];
    }
    escaped[j] = '\0';

    int len = snprintf(json, sizeof(json),
        "{\"type\":\"log\",\"dir\":\"%s\",\"text\":\"%s\"}",
        msg->dir, escaped);

    for (struct mg_connection *wc = mgr->conns; wc != NULL; wc = wc->next) {
        if (wc->data[0] == WS_MARK) {
            mg_ws_send(wc, json, (size_t)len, WEBSOCKET_OP_TEXT);
        }
    }
}

/* ============================================================
 * 内部辅助：WebSocket 广播
 * ============================================================ */

/**
 * @brief 将数据帧序列化为 JSON 并广播到所有 WS 客户端
 * @param mgr    mongoose 管理器
 * @param frame  数据帧
 */
static void s_ws_broadcast(struct mg_mgr *mgr, const gh_data_frame_t *frame) {
    char json[2048];
    /* 构造完整的多通道原始数据数组 */
    char raw_arr[512] = "[";
    for (int i = 0; i < 16; i++) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%d%s",
                 (int32_t)frame->raw_data[i], i < 15 ? "," : "");
        strncat(raw_arr, tmp, sizeof(raw_arr) - strlen(raw_arr) - 1);
    }
    strncat(raw_arr, "]", sizeof(raw_arr) - strlen(raw_arr) - 1);

    /* 构造算法结果数组 */
    char algo_arr[128] = "[";
    for (int i = 0; i < (int)frame->algo_result_num && i < 8; i++) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%d%s",
                 frame->algo_result[i],
                 i < (int)frame->algo_result_num - 1 ? "," : "");
        strncat(algo_arr, tmp, sizeof(algo_arr) - strlen(algo_arr) - 1);
    }
    if (frame->algo_result_num == 0)
        strncat(algo_arr, "0", sizeof(algo_arr) - strlen(algo_arr) - 1);
    strncat(algo_arr, "]", sizeof(algo_arr) - strlen(algo_arr) - 1);

    int len = snprintf(json, sizeof(json),
        "{\"type\":\"data\","
        "\"func_id\":%u,"
        "\"frame_cnt\":%u,"
        "\"raw\":%s,"
        "\"algo\":%s,"
        "\"hr\":%d,"
        "\"gsensor\":[%d,%d,%d],"
        "\"ts\":%llu}",
        frame->func_id,
        frame->frame_cnt,
        raw_arr,
        algo_arr,
        frame->algo_result_num > 0 ? (int)frame->algo_result[0] : 0,
        (int)frame->gsensor[0],
        (int)frame->gsensor[1],
        (int)frame->gsensor[2],
        (unsigned long long)frame->timestamp_ms);

    /* 遍历所有连接，向标记了 WS_MARK 的连接发送 */
    for (struct mg_connection *wc = mgr->conns; wc != NULL; wc = wc->next) {
        if (wc->data[0] == WS_MARK) {
            mg_ws_send(wc, json, (size_t)len, WEBSOCKET_OP_TEXT);
        }
    }
}

/* ============================================================
 * HTTP 端点处理函数
 * ============================================================ */

/** GET /api/device/status */
static void s_handle_status(struct mg_connection *c,
                             struct mg_http_message *hm,
                             gh_api_t *api) {
    (void)hm;
    const char *state_str = "disconnected";
    if (api->service) {
        switch (gh_service_get_state(api->service)) {
            case GH_DEV_STATE_CONNECTED:  state_str = "connected";  break;
            case GH_DEV_STATE_SAMPLING:   state_str = "sampling";   break;
            case GH_DEV_STATE_ERROR:      state_str = "error";      break;
            default: break;
        }
    }
    char data[256];
    snprintf(data, sizeof(data),
             "{\"state\":\"%s\",\"port\":\"%s\",\"baud_rate\":%d,"
             "\"sim_mode\":%s}",
             state_str,
             api->service ? api->service->serial_cfg.port : "none",
             api->service ? api->service->serial_cfg.baud_rate : 0,
             (api->service == NULL || !gh_service_is_connected(api->service))
                 ? "true" : "false");
    s_reply_ok(c, data);
}

/** POST /api/device/connect
 *  Body: {"port":"/dev/ttyUSB0","baud_rate":115200}
 */
static void s_handle_connect(struct mg_connection *c,
                              struct mg_http_message *hm,
                              gh_api_t *api) {
    if (!api->service) { s_reply_err(c, -1, "No service"); return; }

    char port[64] = {0};
    s_json_str(hm->body.buf, hm->body.len, "port", port, sizeof(port));
    long baud = s_json_long(hm->body.buf, hm->body.len, "baud_rate", 115200);

    if (port[0] == '\0') { s_reply_err(c, -2, "Missing port"); return; }

    /* 更新配置 */
    gh_serial_config_t cfg = {0};
    strncpy(cfg.port, port, sizeof(cfg.port));
    cfg.baud_rate    = (int)baud;
    cfg.data_bits    = 8;
    cfg.parity       = 'N';
    cfg.stop_bits    = 1;
    cfg.flow_control = false;
    gh_service_set_serial_config(api->service, &cfg);

    if (gh_service_connect_serial(api->service, port)) {
        s_reply_ok(c, "{\"state\":\"connected\"}");
    } else {
        s_reply_err(c, -3, "Failed to open serial port");
    }
}

/** POST /api/device/disconnect */
static void s_handle_disconnect(struct mg_connection *c,
                                 struct mg_http_message *hm,
                                 gh_api_t *api) {
    (void)hm;
    if (api->service) gh_service_disconnect(api->service);
    s_reply_ok(c, "{\"state\":\"disconnected\"}");
}

/** POST /api/device/start
 *  Body: {"ctrl":0,"mode":0,"func_mask":64,"config_name":"my_test"}
 *  ctrl=0 → 开始采样, ctrl=1 → 停止采样
 */
static void s_handle_start(struct mg_connection *c,
                            struct mg_http_message *hm,
                            gh_api_t *api) {
    long ctrl        = s_json_long(hm->body.buf, hm->body.len, "ctrl", 0);
    long mode        = s_json_long(hm->body.buf, hm->body.len, "mode", 0);
    long func_mask   = s_json_long(hm->body.buf, hm->body.len, "func_mask", 0x40);
    char config_name[256] = {0};
    s_json_str(hm->body.buf, hm->body.len, "config_name", config_name, sizeof(config_name));

    if (!api->service) {
        s_reply_err(c, -1, "no_service");
        return;
    }
    if (!gh_service_is_connected(api->service)) {
        s_reply_err(c, -2, "not_connected");
        return;
    }

    {
        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf),
                 "[CTRL] start req ctrl=%ld mode=%ld func_mask=0x%lX cfg=%s",
                 ctrl, mode, func_mask, config_name[0] ? config_name : "(none)");
        gh_api_push_log(api, log_buf, "info");
    }

    /* 开始采集时设置 CSV 文件名 */
    if (ctrl == 0 && config_name[0] != '\0') {
        gh_service_set_csv_name(api->service, config_name);
    }

    if (!gh_service_start_hbd(api->service,
                              (uint8_t)ctrl, (uint8_t)mode, (uint32_t)func_mask)) {
        gh_api_push_log(api, "[CTRL] start command send failed", "info");
        s_reply_err(c, -3, "start_cmd_send_failed");
        return;
    }

    gh_api_push_log(api, "[CTRL] start command sent", "info");
    {
        char data[64];
        snprintf(data, sizeof(data),
                 "{\"state\":\"%s\"}", ctrl == 0 ? "sampling" : "connected");
        s_reply_ok(c, data);
    }
}

/** POST /api/device/config
 *  Body: {"regs":[{"addr":258,"data":1},{"addr":768,"data":100}]}
 */
static void s_handle_config(struct mg_connection *c,
                             struct mg_http_message *hm,
                             gh_api_t *api) {
    if (!api->service || !gh_service_is_connected(api->service)) {
        s_reply_err(c, -1, "not_connected");
        return;
    }
    /* 简单解析：遍历 JSON 数组，寻找 "addr":N,"data":N 对 */
    const char *body = hm->body.buf;
    size_t blen = hm->body.len;
    gh_reg_t regs[GH_HTTP_MAX_CONFIG_REGS];
    int reg_count = 0;

    const char *p = body;
    while (p && (p = strstr(p, "\"addr\"")) != NULL && reg_count < GH_HTTP_MAX_CONFIG_REGS) {
        long addr = s_json_long(p, (size_t)(body + blen - p), "addr", -1);
        long data_val = s_json_long(p, (size_t)(body + blen - p), "data", -1);
        if (addr >= 0 && data_val >= 0) {
            regs[reg_count].addr = (uint16_t)addr;
            regs[reg_count].data = (uint16_t)data_val;
            reg_count++;
        }
        p += 6; /* 跳过 "addr" 继续搜索 */
    }

    if (reg_count >= GH_HTTP_MAX_CONFIG_REGS && p && strstr(p, "\"addr\"") != NULL) {
        s_reply_err(c, -5, "too_many_regs");
        return;
    }

    if (reg_count > 0) {
        bool ok = gh_service_config_download(api->service, regs, (uint8_t)reg_count);
        if (!ok) {
            s_reply_err(c, -6, "config_send_failed");
            return;
        }
        char data[64];
        snprintf(data, sizeof(data), "{\"sent\":true,\"count\":%d}", reg_count);
        s_reply_ok(c, data);
    } else {
        s_reply_err(c, -4, "No valid regs found");
    }
}

/** POST /api/device/chip_ctrl
 *  Body: {"ctrl_val":194} // 0xC2 for reset
 */
static void s_handle_chip_ctrl(struct mg_connection *c,
                               struct mg_http_message *hm,
                               gh_api_t *api) {
    if (!api->service || !gh_service_is_connected(api->service)) {
        s_reply_err(c, -1, "not_connected");
        return;
    }
    long ctrl_val = s_json_long(hm->body.buf, hm->body.len, "ctrl_val", 0xC2);
    gh_service_cardiff_control(api->service, (uint8_t)ctrl_val);
    s_reply_ok(c, "{\"sent\":true}");
}

/** POST /api/device/work_mode
 *  Body: {"mode":0,"func_mask":4294967295} // 0xFFFFFFFF
 */
static void s_handle_work_mode(struct mg_connection *c,
                               struct mg_http_message *hm,
                               gh_api_t *api) {
    if (!api->service || !gh_service_is_connected(api->service)) {
        s_reply_err(c, -1, "not_connected");
        return;
    }
    long mode = s_json_long(hm->body.buf, hm->body.len, "mode", 0);
    long func_mask = s_json_long(hm->body.buf, hm->body.len, "func_mask", 0xFFFFFFFF);
    gh_service_set_work_mode(api->service, (uint8_t)mode, (uint32_t)func_mask);
    s_reply_ok(c, "{\"sent\":true}");
}

/** GET /api/device/version */
static void s_handle_get_version(struct mg_connection *c,
                                 struct mg_http_message *hm,
                                 gh_api_t *api) {
    (void)hm;
    if (!api->service || !gh_service_is_connected(api->service)) {
        s_reply_err(c, -1, "not_connected");
        return;
    }
    gh_service_get_evk_version(api->service, 1);
    /* In a real implementation this would asynchronously wait for the string.
       For now, we just issue the command. */
    s_reply_ok(c, "{\"sent\":true,\"msg\":\"Async request issued\"}");
}

/** POST /api/device/read_reg
 *  Body: {"addr":258,"count":1}
 */
static void s_handle_read_reg(struct mg_connection *c,
                              struct mg_http_message *hm,
                              gh_api_t *api) {
    if (!api->service || !gh_service_is_connected(api->service)) {
        s_reply_err(c, -1, "not_connected");
        return;
    }
    long addr = s_json_long(hm->body.buf, hm->body.len, "addr", 0);
    long count = s_json_long(hm->body.buf, hm->body.len, "count", 1);
    
    gh_cmd_param_t param;
    param.cmd = 0x03; // RegisterReadWrite
    param.args.reg_oper.op_mode = 0; // Read
    param.args.reg_oper.reg_count = (uint8_t)count;
    param.args.reg_oper.reg_addr = (uint16_t)addr;
    param.args.reg_oper.regs = NULL;
    
    bool ret = gh_service_register_rw(api->service, &param);
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"sent\":%s}", ret ? "true" : "false");
    s_reply_ok(c, buf);
}

/** GET /api/device/data（轮询备用接口）*/
static void s_handle_get_data(struct mg_connection *c,
                               struct mg_http_message *hm,
                               gh_api_t *api) {
    (void)hm;
    if (!api->has_frame) {
        s_reply_err(c, 1, "no_data_yet");
        return;
    }
    const gh_data_frame_t *f = &api->latest_frame;
    char data[1024];
    snprintf(data, sizeof(data),
             "{\"func_id\":%u,\"frame_cnt\":%u,"
             "\"raw\":[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d],"
             "\"hr\":%d,\"ts\":%llu}",
             f->func_id, f->frame_cnt,
             (int32_t)f->raw_data[0], (int32_t)f->raw_data[1],
             (int32_t)f->raw_data[2], (int32_t)f->raw_data[3],
             (int32_t)f->raw_data[4], (int32_t)f->raw_data[5],
             (int32_t)f->raw_data[6], (int32_t)f->raw_data[7],
             (int32_t)f->raw_data[8], (int32_t)f->raw_data[9],
             (int32_t)f->raw_data[10], (int32_t)f->raw_data[11],
             (int32_t)f->raw_data[12], (int32_t)f->raw_data[13],
             (int32_t)f->raw_data[14], (int32_t)f->raw_data[15],
             f->algo_result_num > 0 ? (int)f->algo_result[0] : 0,
             (unsigned long long)f->timestamp_ms);
    s_reply_ok(c, data);
}

/** GET /api/serial/list（列出可用串口，简单实现）*/
static void s_handle_serial_list(struct mg_connection *c,
                                  struct mg_http_message *hm) {
    (void)hm;
    /* 在实际实现中可以遍历 /dev/tty* 或 Windows COM 口
     * 此处返回常见候选 */
#if defined(_WIN32)
    s_reply_ok(c, "[\"COM1\",\"COM2\",\"COM3\",\"COM4\",\"COM5\",\"COM6\"]");
#else
    s_reply_ok(c,
        "[\"/dev/ttyUSB0\",\"/dev/ttyUSB1\","
        "\"/dev/ttyACM0\",\"/dev/cu.usbserial-0001\","
        "\"/dev/cu.SLAB_USBtoUART\"]");
#endif
}

static void s_handle_protocol(struct mg_connection *c,
                              struct mg_http_message *hm,
                              gh_api_t *api) {
    char prot[64] = {0};
    if (s_json_str(hm->body.buf, hm->body.len, "protocol", prot, sizeof(prot))) {
        if (api->service) {
            if (strcmp(prot, "Chelsea_A") == 0) {
                api->service->use_chelsea_a_parser = true;
            } else {
                api->service->use_chelsea_a_parser = false;
            }
            s_reply_ok(c, "{\"success\":true}");
            return;
        }
    }
    s_reply_err(c, -1, "invalid_protocol");
}

/* ============================================================
 * 内部辅助：URL 精确匹配（及前缀匹配）
 * 替代 mg_http_match_uri（mongoose 7.x 和 8.x 行为不一）
 * ============================================================ */
static bool s_uri_eq(struct mg_http_message *hm, const char *path) {
    size_t plen = strlen(path);
    return hm->uri.len == plen
        && memcmp(hm->uri.buf, path, plen) == 0;
}

/* 前缀匹配（/api/... 子路径）*/
static bool s_uri_starts(struct mg_http_message *hm, const char *prefix) {
    size_t plen = strlen(prefix);
    return hm->uri.len >= plen
        && memcmp(hm->uri.buf, prefix, plen) == 0;
}

/* HTTP 方法比较（大小写不敏感）*/
static bool s_method_eq(struct mg_http_message *hm, const char *method) {
    size_t mlen = strlen(method);
    if (hm->method.len != mlen) return false;
    for (size_t i = 0; i < mlen; i++) {
        /* 小写字母转大写比较 */
        char a = hm->method.buf[i];
        char b = method[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return false;
    }
    return true;
}

/* ============================================================
 * mongoose 主事件处理器
 * ============================================================ */
static void s_mongoose_handler(struct mg_connection *c,
                                int ev, void *ev_data) {
    gh_api_t *api = (gh_api_t *)c->fn_data;

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        /* ---- OPTIONS CORS 预检 ---- */
        if (s_method_eq(hm, "OPTIONS")) {
            mg_http_reply(c, 204, CORS_HEADERS, "");
            return;
        }

        /* ---- WebSocket 升级请求 ---- */
        if (s_uri_eq(hm, "/ws")) {
            mg_ws_upgrade(c, hm, NULL);
            c->data[0] = WS_MARK;   /* 标记此连接为 WebSocket */
            return;
        }

        /* ---- REST API 路由 ---- */
        if (s_uri_eq(hm, "/api/device/status")) {
            s_handle_status(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/connect")) {
            s_handle_connect(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/disconnect")) {
            s_handle_disconnect(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/start")) {
            s_handle_start(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/config")) {
            s_handle_config(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/chip_ctrl")) {
            s_handle_chip_ctrl(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/work_mode")) {
            s_handle_work_mode(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/version")) {
            s_handle_get_version(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/read_reg")) {
            s_handle_read_reg(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/protocol")) {
            s_handle_protocol(c, hm, api);
        } else if (s_uri_eq(hm, "/api/device/data")) {
            s_handle_get_data(c, hm, api);
        } else if (s_uri_eq(hm, "/api/serial/list")) {
            s_handle_serial_list(c, hm);
        } else if (s_uri_starts(hm, "/api/")) {
            /* 未知 API 路径 */
            s_reply_err(c, 404, "Unknown API endpoint");
        } else {
            /* ---- 静态文件服务（前端 HTML/JS/CSS）---- */
            struct mg_http_serve_opts opts;
            memset(&opts, 0, sizeof(opts));
            opts.root_dir   = api->web_root;
            opts.mime_types = "html=text/html,js=application/javascript,"
                              "css=text/css,json=application/json";
            mg_http_serve_dir(c, hm, &opts);
        }
    } else if (ev == MG_EV_WS_OPEN) {
        /* WebSocket 握手完成，标记此连接 */
        c->data[0] = WS_MARK;
        /* 发送欢迎消息，携带当前状态 */
        bool dev_connected = api->service && gh_service_is_connected(api->service);
        char welcome[128];
        snprintf(welcome, sizeof(welcome),
                 "{\"type\":\"hello\",\"state\":\"%s\",\"sim\":%s}",
                 dev_connected ? "connected" : "disconnected",
                 (api->service == NULL || !dev_connected) ? "true" : "false");
        mg_ws_send(c, welcome, strlen(welcome), WEBSOCKET_OP_TEXT);
        printf("[API] WebSocket 客户端已连接\n");
    } else if (ev == MG_EV_WS_MSG) {
        /* 收到客户端 WS 消息（目前忽略，前端通过 REST 控制）*/
    } else if (ev == MG_EV_CLOSE) {
        if (c->data[0] == WS_MARK) {
            c->data[0] = 0;
            printf("[API] WebSocket 客户端已断开\n");
        }
    }
}

/* ============================================================
 * 公开 API 实现
 * ============================================================ */

bool gh_api_init(gh_api_t *api, gh_service_t *service,
                 int http_port, const char *web_root) {
    if (!api) return false;
    memset(api, 0, sizeof(gh_api_t));

    api->service = service;
    snprintf(api->web_root, sizeof(api->web_root), "%s",
             web_root ? web_root : "web/frontend");
    snprintf(api->listen_url, sizeof(api->listen_url),
             "http://0.0.0.0:%d", http_port);

    /* 初始化帧队列 */
    GH_MUTEX_INIT(&api->queue.lock);
    api->queue.head  = 0;
    api->queue.tail  = 0;
    api->queue.count = 0;

    /* 初始化日志队列 */
    GH_MUTEX_INIT(&api->log_queue.lock);
    api->log_queue.head  = 0;
    api->log_queue.tail  = 0;
    api->log_queue.count = 0;

    /* 分配并初始化 mg_mgr */
    struct mg_mgr *mgr = (struct mg_mgr *)malloc(sizeof(struct mg_mgr));
    if (!mgr) return false;
    mg_mgr_init(mgr);
    api->mgr = mgr;

    /* 绑定监听 */
    struct mg_connection *lc = mg_http_listen(mgr, api->listen_url,
                                               s_mongoose_handler, api);
    if (!lc) {
        fprintf(stderr, "[API] Failed to listen on %s\n", api->listen_url);
        mg_mgr_free(mgr);
        free(mgr);
        api->mgr = NULL;
        return false;
    }

    printf("[API] HTTP server listening on %s\n", api->listen_url);
    printf("[API] Serving frontend from: %s\n", api->web_root);
    return true;
}

void gh_api_run(gh_api_t *api) {
    if (!api || !api->mgr) return;
    struct mg_mgr *mgr = (struct mg_mgr *)api->mgr;
    api->running = true;

    while (api->running) {
        /* 1. 轮询一次网络事件（50ms 超时）*/
        mg_mgr_poll(mgr, 50);

        /* 2. Drain 帧队列，广播到所有 WebSocket 客户端 */
        GH_MUTEX_LOCK(&api->queue.lock);
        while (api->queue.count > 0) {
            /* 取出队头帧（拷贝出来再解锁，避免持锁时间过长）*/
            gh_data_frame_t frame = api->queue.frames[api->queue.head];
            api->queue.head = (api->queue.head + 1) % GH_API_QUEUE_SIZE;
            api->queue.count--;
            GH_MUTEX_UNLOCK(&api->queue.lock);

            /* 广播 */
            s_ws_broadcast(mgr, &frame);
            /* 同时更新最新帧（供轮询用）*/
            api->latest_frame = frame;
            api->has_frame    = true;

            GH_MUTEX_LOCK(&api->queue.lock);
        }
        GH_MUTEX_UNLOCK(&api->queue.lock);

        /* 3. Drain 日志队列，广播到所有 WebSocket 客户端（调试窗口）*/
        GH_MUTEX_LOCK(&api->log_queue.lock);
        while (api->log_queue.count > 0) {
            gh_log_msg_t lmsg = api->log_queue.msgs[api->log_queue.head];
            api->log_queue.head = (api->log_queue.head + 1) % GH_API_LOG_QUEUE_SIZE;
            api->log_queue.count--;
            GH_MUTEX_UNLOCK(&api->log_queue.lock);

            s_ws_broadcast_log(mgr, &lmsg);

            GH_MUTEX_LOCK(&api->log_queue.lock);
        }
        GH_MUTEX_UNLOCK(&api->log_queue.lock);
    }
}

void gh_api_stop(gh_api_t *api) {
    if (api) api->running = false;
}

void gh_api_push_frame(gh_api_t *api, const gh_data_frame_t *frame) {
    if (!api || !frame) return;
    GH_MUTEX_LOCK(&api->queue.lock);
    if (api->queue.count < GH_API_QUEUE_SIZE) {
        api->queue.frames[api->queue.tail] = *frame;
        api->queue.tail = (api->queue.tail + 1) % GH_API_QUEUE_SIZE;
        api->queue.count++;
    }
    /* 队列满时丢弃最旧的帧（覆盖环形缓冲头部）*/
    else {
        api->queue.frames[api->queue.tail] = *frame;
        api->queue.tail = (api->queue.tail + 1) % GH_API_QUEUE_SIZE;
        api->queue.head = (api->queue.head + 1) % GH_API_QUEUE_SIZE;
    }
    GH_MUTEX_UNLOCK(&api->queue.lock);
}

void gh_api_push_log(gh_api_t *api, const char *text, const char *dir) {
    if (!api || !text || !dir) return;
    GH_MUTEX_LOCK(&api->log_queue.lock);
    /* 队列满时覆盖最旧的 */
    if (api->log_queue.count >= GH_API_LOG_QUEUE_SIZE) {
        api->log_queue.head = (api->log_queue.head + 1) % GH_API_LOG_QUEUE_SIZE;
        api->log_queue.count--;
    }
    gh_log_msg_t *slot = &api->log_queue.msgs[api->log_queue.tail];
    strncpy(slot->text, text, GH_API_LOG_MAX_LEN - 1);
    slot->text[GH_API_LOG_MAX_LEN - 1] = '\0';
    strncpy(slot->dir, dir, sizeof(slot->dir) - 1);
    slot->dir[sizeof(slot->dir) - 1] = '\0';
    api->log_queue.tail = (api->log_queue.tail + 1) % GH_API_LOG_QUEUE_SIZE;
    api->log_queue.count++;
    GH_MUTEX_UNLOCK(&api->log_queue.lock);
}

void gh_api_destroy(gh_api_t *api) {
    if (!api) return;
    if (api->mgr) {
        mg_mgr_free((struct mg_mgr *)api->mgr);
        free(api->mgr);
        api->mgr = NULL;
    }
    GH_MUTEX_DESTROY(&api->queue.lock);
    GH_MUTEX_DESTROY(&api->log_queue.lock);
}
