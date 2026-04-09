/* ============================================================
 * 文件: src/service/gh_service.c
 * 功能: 业务服务层实现
 * ============================================================ */

#include "gh_service.h"
#include <string.h>
#include <stdio.h>
#include "../protocol/chelsea_a/gh_data_package_decode.h"
#include "../protocol/chelsea_a/gh_data_common.h"
#include "../protocol/gh_rpc.h"
#include <time.h>

#define CHELSEA_FRAME_BATCH_MAX 255
#define GH_BLE_AT_UART_BAUD_RATE 115200

/* ============================================================
 * CSV 写入辅助
 * ============================================================ */

static void s_csv_write_header(FILE *fp) {
    fprintf(fp, "TimeStamp\tFRAME_ID\tACCX\tACCY\tACCZ");
    for (int i = 0; i < 16; i++) fprintf(fp, "\tCH%d", i);
    for (int i = 0; i < 8;  i++) fprintf(fp, "\tFLAG%d", i);
    for (int i = 0; i < 16; i++) fprintf(fp, "\tREF_RESULT%d", i);
    for (int i = 0; i < 8;  i++) fprintf(fp, "\tALGO_RESULT%d", i);
    for (int i = 0; i < 16; i++) fprintf(fp, "\tAGC_INFO_CH%d", i);
    for (int i = 0; i < 4;  i++) fprintf(fp, "\tAMB_CH%d", i);
    for (int i = 0; i < 4;  i++) fprintf(fp, "\tTEMP_CH%d", i);
    fprintf(fp, "\n");
    fflush(fp);
}

static void s_csv_write_frame(FILE *fp, const gh_func_frame_t *fr, uint64_t ts_ms) {
    (void)ts_ms;
    int ch = (int)fr->ch_num;
    fprintf(fp, "%llu\t%u\t%d\t%d\t%d",
            (unsigned long long)fr->timestamp,
            fr->frame_cnt,
            (int)fr->gsensor_data.acc[0],
            (int)fr->gsensor_data.acc[1],
            (int)fr->gsensor_data.acc[2]);

    /* CH 填充口径修正：
     * CH0..CH(ch-1) = rawdata
     * 若 2*ch <= 16，则 CH(ch)..CH(2*ch-1) = ipd_pa
     */
    int ch_vals[16] = {0};
    for (int i = 0; i < ch && i < 16; i++) {
        ch_vals[i] = fr->p_data[i].rawdata;
    }
    if ((2 * ch) <= 16) {
        for (int i = 0; i < ch && (i + ch) < 16; i++) {
            ch_vals[i + ch] = fr->p_data[i].ipd_pa;
        }
    }
    for (int i = 0; i < 16; i++) {
        fprintf(fp, "\t%d", ch_vals[i]);
    }

    /* FLAG0..FLAG7（Qt 仅写 i<ch 的 flag，其余列保持默认0）*/
    for (int i = 0; i < 8; i++) {
        uint8_t f = 0;
        if (i < ch) memcpy(&f, &fr->p_data[i].flag, 1);
        fprintf(fp, "\t%u", (unsigned)f);
    }
    /* REF_RESULT0..15（Web 无外部参考值，保持 0） */
    for (int i = 0; i < 16; i++) {
        fprintf(fp, "\t0");
    }
    /* ALGO_RESULT0..7（对齐 Qt：按 p_algo_res[0] 结果个数写入，其余补0）*/
    int algo_vals[8] = {0};
    if (fr->p_algo_res != NULL) {
        const uint32_t *sp_algo_res = (const uint32_t *)fr->p_algo_res;
        int algo_num = (int)sp_algo_res[0];
        if (algo_num > 8) algo_num = 8;
        for (int i = 0; i < algo_num; i++) {
            algo_vals[i] = (int)sp_algo_res[i + 1];
        }
    }
    for (int i = 0; i < 8; i++) {
        fprintf(fp, "\t%d", algo_vals[i]);
    }
    /* AGC_INFO_CH0..15（Qt: reinterpret_cast<uint32_t*>(&agc_info)[0]）*/
    for (int i = 0; i < 16; i++) {
        uint32_t v = 0;
        if (i < ch) memcpy(&v, &fr->p_data[i].agc_info, sizeof(uint32_t));
        fprintf(fp, "\t%u", (unsigned)v);
    }
    /* AMB_CH0..3, TEMP_CH0..3 (not in Test1 frame) */
    for (int i = 0; i < 4; i++) fprintf(fp, "\t0");
    for (int i = 0; i < 4; i++) fprintf(fp, "\t0");
    fprintf(fp, "\n");
}

/* ============================================================
 * 内部：transport → service 的帧接收回调
 * 当 transport 层组装好一帧后，调用此函数
 * 对应原 handleSerialReadyRead 中 emit reciveData() 之后的处理流程
 * ============================================================ */
static void s_on_transport_frame(const uint8_t* frame, uint16_t len, void* ctx) {
    gh_service_t* svc = (gh_service_t*)ctx;
    if (!svc || !frame || len < 4) return;

    if (svc->use_chelsea_a_parser) {
        /* Chelsea A 解包流程：
         * 1. 解析 Cardiff RPC 帧，提取 key 和 params
         * 2. 若 key == "G"（DealFrameDataProcess），提取 u8* 数组传给 gh_protocol_process
         * 3. 其他 key（如 sall 响应）记录日志
         */
        char rpc_key[64];
        const uint8_t *rpc_params = NULL;
        int rpc_params_len = 0;

        if (!gh_rpc_parse_frame(frame, (int)len,
                                rpc_key, (int)sizeof(rpc_key),
                                &rpc_params, &rpc_params_len)) {
            /* CRC 校验失败或帧格式不对，丢弃 */
            if (svc->on_log) svc->on_log("[RPC] Frame parse/CRC error", svc->on_log_ctx);
            return;
        }

        if (rpc_key[0] == 'G' && rpc_key[1] == '\0') {
            /* 设备发送传感器数据帧 (key="G") */
            static gh_func_frame_t chelsea_frames[CHELSEA_FRAME_BATCH_MAX];
            static uint32_t chelsea_algo_res[CHELSEA_FRAME_BATCH_MAX * 16];
            static gh_frame_data_t chelsea_frame_data[CHELSEA_FRAME_BATCH_MAX * 32];
            static bool chelsea_init = false;
            static uint32_t g_no_u8_array_count = 0;
            static uint32_t g_invalid_frameid_count = 0;
            static uint32_t g_zero_parsed_count = 0;

            if (!chelsea_init) {
                for (int i = 0; i < CHELSEA_FRAME_BATCH_MAX; i++) {
                    memset(&chelsea_frames[i], 0, sizeof(gh_func_frame_t));
                    chelsea_frames[i].p_data = &chelsea_frame_data[i * 32];
                    chelsea_frames[i].p_algo_res = &chelsea_algo_res[i * 16];
                }
                chelsea_init = true;
            }

            const uint8_t *raw_data = NULL;
            int raw_len = 0;
            bool has_u8_arr = gh_rpc_extract_u8_array(rpc_params, rpc_params_len, &raw_data, &raw_len);
            bool used_fallback = false;
            if (!has_u8_arr || !raw_data || raw_len <= 0) {
                /* 部分固件/场景下 key=G 参数可能不是 u8* 数组；降级尝试直接用 params 解包。 */
                raw_data = rpc_params;
                raw_len = rpc_params_len;
                g_no_u8_array_count++;
                used_fallback = true;
            }

            gh_func_frame_t* p_frames = chelsea_frames;
            uint8_t parsed_len = 0;

            gh_protocol_process(&p_frames, &parsed_len, (uint8_t*)raw_data, (uint32_t)raw_len);
            if (parsed_len == 0) {
                g_zero_parsed_count++;
                if (svc->on_log && (g_zero_parsed_count <= 3U || (g_zero_parsed_count % 50U) == 0U)) {
                    char log_buf[192];
                    snprintf(log_buf, sizeof(log_buf),
                             "[RPC] key=G parsed_len=0 (cnt=%u, raw_len=%d, fallback=%d, params_len=%d)",
                             (unsigned)g_zero_parsed_count, raw_len, used_fallback ? 1 : 0, rpc_params_len);
                    svc->on_log(log_buf, svc->on_log_ctx);
                }
            }
            if (used_fallback && parsed_len == 0) {
                /* 限频日志：前3次打印，之后每50次打印一次累计计数 */
                if (svc->on_log && (g_no_u8_array_count <= 3 || (g_no_u8_array_count % 50U) == 0U)) {
                    char log_buf[160];
                    snprintf(log_buf, sizeof(log_buf),
                             "[RPC] key=G no u8* array (cnt=%u, params_len=%d, parsed=0)",
                             (unsigned)g_no_u8_array_count, rpc_params_len);
                    svc->on_log(log_buf, svc->on_log_ctx);
                }
                return;
            }
            if (parsed_len > CHELSEA_FRAME_BATCH_MAX) {
                if (svc->on_log) {
                    char log_buf[128];
                    snprintf(log_buf, sizeof(log_buf),
                             "[RPC] key=G: parsed_len overflow, clamped to %d",
                             CHELSEA_FRAME_BATCH_MAX);
                    svc->on_log(log_buf, svc->on_log_ctx);
                }
                parsed_len = CHELSEA_FRAME_BATCH_MAX;
            } else if (parsed_len == CHELSEA_FRAME_BATCH_MAX) {
                if (svc->on_log) svc->on_log("[RPC] key=G: batch reached frame cap, consider raising cap", svc->on_log_ctx);
            }

        for (uint8_t i = 0; i < parsed_len; i++) {
            uint32_t frame_cnt = p_frames[i].frame_cnt;
            /* 某些模式下 frame_id 可能出现异常值。不要直接丢帧，按连续序号兜底。 */
            if (frame_cnt > 1000000U) {
                uint32_t fixed = svc->has_last_frame_cnt ? (svc->last_frame_cnt + 1U) : 0U;
                g_invalid_frameid_count++;
                if (svc->on_log && (g_invalid_frameid_count <= 3U || (g_invalid_frameid_count % 100U) == 0U)) {
                    char log_buf[192];
                    snprintf(log_buf, sizeof(log_buf),
                             "[RPC] invalid frame_cnt=%u -> fallback=%u (cnt=%u)",
                             (unsigned)frame_cnt, (unsigned)fixed, (unsigned)g_invalid_frameid_count);
                    svc->on_log(log_buf, svc->on_log_ctx);
                }
                frame_cnt = fixed;
                p_frames[i].frame_cnt = frame_cnt;
            }
            if (svc->has_last_frame_cnt) {
                uint32_t prev = svc->last_frame_cnt;
                uint32_t cur = frame_cnt;
                if (cur > prev) {
                    uint32_t diff = cur - prev;
                    if (diff > 1U) {
                        uint32_t lost = diff - 1U;
                        svc->frame_gap_events++;
                        svc->frame_gap_total += lost;
                        if (svc->on_log) {
                            char log_buf[192];
                            snprintf(log_buf, sizeof(log_buf),
                                     "[FRAME_GAP] prev=%u curr=%u lost=%u events=%u total_lost=%u",
                                     (unsigned)prev, (unsigned)cur, (unsigned)lost,
                                     (unsigned)svc->frame_gap_events, (unsigned)svc->frame_gap_total);
                            svc->on_log(log_buf, svc->on_log_ctx);
                        }
                    }
                }
            }
            svc->last_frame_cnt = frame_cnt;
            svc->has_last_frame_cnt = true;
            gh_data_frame_t data;
            memset(&data, 0, sizeof(data));
            data.func_id = p_frames[i].id;
            data.frame_cnt = frame_cnt;
            
            /* 映射原始数据：
             * ch 路输入时，优先放 raw 到前半段；
             * 当 2*ch <= 16 时，将 ipd_pa 补到后半段，形成 CH0..CH15。 */
            for (int ch = 0; ch < p_frames[i].ch_num && ch < 32; ch++) {
                data.raw_data[ch] = p_frames[i].p_data[ch].rawdata;
            }
            if ((2 * (int)p_frames[i].ch_num) <= 16) {
                for (int ch = 0; ch < p_frames[i].ch_num && (ch + p_frames[i].ch_num) < 32; ch++) {
                    data.raw_data[ch + p_frames[i].ch_num] = (uint32_t)p_frames[i].p_data[ch].ipd_pa;
                }
            }
            
            /* G-sensor 映射 */
            data.gsensor[0] = p_frames[i].gsensor_data.acc[0];
            data.gsensor[1] = p_frames[i].gsensor_data.acc[1];
            data.gsensor[2] = p_frames[i].gsensor_data.acc[2];
            
            /* 算法结果（这里简单取第一个值做 HR 的表示，可根据具体的 id 扩展结构体投射）*/
            if (p_frames[i].p_algo_res) {
                 if (data.func_id == GH_FUNC_FIX_IDX_HR) {
                     gh_algo_hr_result_t *p_hr = (gh_algo_hr_result_t *)p_frames[i].p_algo_res;
                     data.algo_result[0] = p_hr->hba_out;
                     data.algo_result_num = 1;
                 }
            }

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            data.timestamp_ms = (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;

            /* 写入 CSV（与 start/stop 线程并发，需加锁） */
            GH_MUTEX_LOCK(&svc->csv_lock);
            if (svc->csv_fp) {
                s_csv_write_frame(svc->csv_fp, &p_frames[i], data.timestamp_ms);
                svc->csv_rows_written++;
            }
            GH_MUTEX_UNLOCK(&svc->csv_lock);

            if (svc->on_data) {
                svc->on_data(&data, svc->on_data_ctx);
            }
        }
        } else if (strcmp(rpc_key, "GH3X_GetVersion") == 0 && rpc_params != NULL) {
            /* GH3X_GetVersion 响应：解析 u8* 字符串参数，提取版本字符串
             * 参数格式：[TypeData_scalar...][TypeArray_u8* count data...]
             * 跳过所有标量参数，找到第一个数组参数即为版本字符串
             */
            char ver_str[256];
            bool ver_found = false;
            int pi = 0;

            while (pi < rpc_params_len && !ver_found) {
                if (pi >= rpc_params_len) break;
                uint8_t type_b   = rpc_params[pi++];
                uint8_t is_arr   = (type_b >> 2) & 0x01U;
                uint8_t end_b    = (type_b >> 6) & 0x01U;
                int     width_b  = (1 << ((type_b >> 3) & 0x07U)) / 8; /* 每元素字节数 */
                if (width_b < 1) width_b = 1;

                if (is_arr) {
                    /* 数组：[count: 1 byte][data...] */
                    if (pi >= rpc_params_len) break;
                    int count = (int)rpc_params[pi++];
                    int total = count * width_b;
                    if (pi + total > rpc_params_len) break;
                    int copy = (total < (int)sizeof(ver_str) - 1) ? total : (int)sizeof(ver_str) - 1;
                    memcpy(ver_str, &rpc_params[pi], (size_t)copy);
                    ver_str[copy] = '\0';
                    /* 按 null 截断（设备发的字符串有 null 结尾）*/
                    for (int j = 0; j < copy; j++) {
                        if (ver_str[j] == '\0') break;
                    }
                    ver_found = true;
                } else {
                    /* 标量：跳过值字节 */
                    if (pi + width_b > rpc_params_len) break;
                    pi += width_b;
                }
                if (end_b) break;
            }

            if (ver_found && svc->on_log) {
                char log_buf[320];
                snprintf(log_buf, sizeof(log_buf), "[VERSION] %s", ver_str);
                svc->on_log(log_buf, svc->on_log_ctx);
            } else {
                /* 解析失败，至少记录收到了响应 */
                char log_buf[128];
                snprintf(log_buf, sizeof(log_buf), "[RPC] RX GH3X_GetVersion params_len=%d (parse failed)", rpc_params_len);
                if (svc->on_log) svc->on_log(log_buf, svc->on_log_ctx);
            }
        } else if (strcmp(rpc_key, "GH3X_RegsReadCmd") == 0 && rpc_params != NULL) {
            /* 寄存器读响应：提取 u16* 数组，推送给前端 */
            uint16_t vals[64];
            int num_vals = 0;
            if (gh_rpc_extract_u16_array(rpc_params, rpc_params_len, vals, 64, &num_vals)
                && num_vals > 0 && svc->on_log) {
                /* 格式: [REG_VAL] 0xVAL0,0xVAL1,...  前端据此更新寄存器显示 */
                char log_buf[256];
                int off = snprintf(log_buf, sizeof(log_buf), "[REG_VAL]");
                for (int vi = 0; vi < num_vals && off < (int)sizeof(log_buf) - 8; vi++) {
                    off += snprintf(log_buf + off, sizeof(log_buf) - (size_t)off,
                                   " 0x%04X", vals[vi]);
                }
                svc->on_log(log_buf, svc->on_log_ctx);
            } else {
                char log_buf[128];
                snprintf(log_buf, sizeof(log_buf),
                         "[RPC] RX GH3X_RegsReadCmd params_len=%d (no u16 array)", rpc_params_len);
                if (svc->on_log) svc->on_log(log_buf, svc->on_log_ctx);
            }
        } else if ((strcmp(rpc_key, "GH3X_ChipCtrl")       == 0 ||
                    strcmp(rpc_key, "download_config")      == 0 ||
                    strcmp(rpc_key, "GH3X_RegsListWriteCmd")== 0 ||
                    strcmp(rpc_key, "GH3X_SwFunctionCmd")   == 0)
                   && rpc_params != NULL) {
            /* sall 命令的设备响应格式：[U8_MORE: status][U8_MORE: com_id_echo]
             * status=0x02 表示成功（与原始抓包一致）
             */
            uint8_t status = 0, com_echo = 0;
            if (rpc_params_len >= 4) {
                /* 跳过 TypeData 字节，读值 */
                status   = rpc_params[1];
                com_echo = rpc_params[3];
            }
            if (svc->on_log) {
                char log_buf[128];
                snprintf(log_buf, sizeof(log_buf),
                         "[RPC] ACK key=\"%s\" status=0x%02X com_id=0x%02X",
                         rpc_key, status, com_echo);
                svc->on_log(log_buf, svc->on_log_ctx);
            }
        } else {
            /* 其他 key：记录日志 */
            char log_buf[128];
            snprintf(log_buf, sizeof(log_buf), "[RPC] RX key=\"%s\" params_len=%d", rpc_key, rpc_params_len);
            if (svc->on_log) svc->on_log(log_buf, svc->on_log_ctx);
        }
    } else {
        /* 原 Cardiff A 流程 */
        const uint8_t* inner_pkt = frame + 2;
        uint16_t       inner_len = len - 2;
        gh_parse_packet(&svc->parser, inner_pkt, inner_len);
    }
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
 * 内部：Chelsea A / Cardiff RPC 帧发送辅助
 * ============================================================ */

/**
 * @brief 发送 Cardiff RPC send 帧（无需设备响应）
 */
static bool s_rpc_send(gh_service_t* svc,
                       const char *key,
                       const uint8_t *params, int params_len) {
    uint8_t frame[GH_RPC_FRAME_MAX];
    int n = gh_rpc_build_frame(frame, (int)sizeof(frame),
                               false, 0,
                               key, params, params_len);
    if (n <= 0) return false;
    gh_transport_send(&svc->transport, frame, (uint16_t)n);
    return true;
}

/**
 * @brief 发送 Cardiff RPC sall 帧（期望设备响应，自动递增 com_id）
 */
static bool s_rpc_sall(gh_service_t* svc,
                       const char *key,
                       const uint8_t *params, int params_len) {
    uint8_t frame[GH_RPC_FRAME_MAX];
    uint8_t com_id = svc->rpc_com_id++;
    int n = gh_rpc_build_frame(frame, (int)sizeof(frame),
                               true, com_id,
                               key, params, params_len);
    if (n <= 0) return false;
    gh_transport_send(&svc->transport, frame, (uint16_t)n);
    return true;
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
    svc->use_chelsea_a_parser = true; /* Chelsea A 为默认协议 */
    /* 对齐老上位机 CardiffRPC 的 secure com_id 窗口，首个常见请求从 0x08 开始。 */
    svc->rpc_com_id   = 0x08;
    GH_MUTEX_INIT(&svc->csv_lock);
    svc->csv_rows_written = 0;
    svc->has_last_frame_cnt = false;
    svc->last_frame_cnt = 0;
    svc->frame_gap_events = 0;
    svc->frame_gap_total = 0;

    /* 初始化默认串口配置 */
    strncpy(svc->serial_cfg.port, "/dev/ttyUSB0", sizeof(svc->serial_cfg.port));
    svc->serial_cfg.baud_rate  = 115200;
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

bool gh_service_connect_ble_at(gh_service_t* svc, const char* at_port, const char* slave_name) {
    return gh_service_connect_ble_at_with_mac(svc, at_port, slave_name, NULL);
}

bool gh_service_connect_ble_at_with_mac(gh_service_t* svc, const char* at_port, const char* slave_name, const char* mac) {
    if (!svc || !at_port || !slave_name) return false;
    const int baud = GH_BLE_AT_UART_BAUD_RATE;
    char mac_buf[32];

    strncpy(svc->serial_cfg.port, at_port, sizeof(svc->serial_cfg.port));
    svc->serial_cfg.baud_rate = baud;
    svc->serial_cfg.data_bits = 8;
    svc->serial_cfg.parity = 'N';
    svc->serial_cfg.stop_bits = 1;
    svc->serial_cfg.flow_control = false;

    if (svc->on_log) {
        char log_buf[160];
        snprintf(log_buf, sizeof(log_buf), "[Service] BLE-AT connect try baud=%d", baud);
        svc->on_log(log_buf, svc->on_log_ctx);
    }
    if (gh_transport_open_ble_at_with_mac(&svc->transport, &svc->serial_cfg, slave_name, mac, mac_buf, sizeof(mac_buf))) {
        gh_transport_start_rx_thread(&svc->transport);
        if (svc->on_log) {
            char log_buf[192];
            snprintf(log_buf, sizeof(log_buf), "[Service] BLE-AT connected: name=%s mac=%s baud=%d", slave_name, mac_buf, baud);
            svc->on_log(log_buf, svc->on_log_ctx);
        }
        return true;
    }

    svc->device_state = GH_DEV_STATE_ERROR;
    if (svc->on_state) svc->on_state(svc->device_state, svc->on_state_ctx);
    return false;
}

bool gh_service_connect_ble_at_fast(gh_service_t* svc, const char* at_port, const char* slave_name, const char* mac) {
    if (!svc || !at_port || !slave_name || !mac || mac[0] == '\0') return false;
    const int baud = GH_BLE_AT_UART_BAUD_RATE;
    char mac_buf[32];

    strncpy(svc->serial_cfg.port, at_port, sizeof(svc->serial_cfg.port));
    svc->serial_cfg.baud_rate = baud;
    svc->serial_cfg.data_bits = 8;
    svc->serial_cfg.parity = 'N';
    svc->serial_cfg.stop_bits = 1;
    svc->serial_cfg.flow_control = false;

    if (svc->on_log) {
        char log_buf[160];
        snprintf(log_buf, sizeof(log_buf), "[Service] BLE-AT fast connect try baud=%d", baud);
        svc->on_log(log_buf, svc->on_log_ctx);
    }

    if (gh_transport_open_ble_at_fast(&svc->transport, &svc->serial_cfg, slave_name, mac, mac_buf, sizeof(mac_buf))) {
        gh_transport_start_rx_thread(&svc->transport);
        if (svc->on_log) {
            char log_buf[192];
            snprintf(log_buf, sizeof(log_buf), "[Service] BLE-AT fast connected: name=%s mac=%s baud=%d", slave_name, mac_buf, baud);
            svc->on_log(log_buf, svc->on_log_ctx);
        }
        return true;
    }

    svc->device_state = GH_DEV_STATE_ERROR;
    if (svc->on_state) svc->on_state(svc->device_state, svc->on_state_ctx);
    return false;
}

bool gh_service_scan_ble_at(gh_service_t* svc, const char* at_port, const char* slave_name, char* out_mac, size_t out_mac_sz) {
    if (!svc || !at_port || !slave_name || !out_mac || out_mac_sz < 2) return false;
    const int baud = GH_BLE_AT_UART_BAUD_RATE;

    gh_serial_config_t cfg = {0};
    strncpy(cfg.port, at_port, sizeof(cfg.port));
    cfg.baud_rate = baud;
    cfg.data_bits = 8;
    cfg.parity = 'N';
    cfg.stop_bits = 1;
    cfg.flow_control = false;

    if (svc->on_log) {
        char log_buf[160];
        snprintf(log_buf, sizeof(log_buf), "[Service] BLE-AT scan try baud=%d", baud);
        svc->on_log(log_buf, svc->on_log_ctx);
    }

    /* 扫描使用临时 transport，避免影响主连接状态机 */
    gh_transport_t tmp;
    gh_transport_init(&tmp, NULL, NULL, NULL, NULL, (gh_log_cb)svc->on_log, svc->on_log_ctx);
    return gh_transport_ble_scan(&tmp, &cfg, slave_name, out_mac, out_mac_sz);
}

void gh_service_disconnect(gh_service_t* svc) {
    if (!svc) return;
    /* 断开时关闭 CSV（若采集中断）*/
    GH_MUTEX_LOCK(&svc->csv_lock);
    if (svc->csv_fp) {
        fclose(svc->csv_fp);
        svc->csv_fp = NULL;
    }
    svc->csv_rows_written = 0;
    GH_MUTEX_UNLOCK(&svc->csv_lock);
    gh_transport_stop_rx_thread(&svc->transport);
    gh_transport_close(&svc->transport);
    gh_parser_reset(&svc->parser);
    svc->device_state = GH_DEV_STATE_DISCONNECTED;
}

bool gh_service_is_connected(const gh_service_t* svc) {
    return (svc && gh_transport_is_open(&svc->transport));
}

const char* gh_service_get_ble_mac(const gh_service_t* svc) {
    if (!svc || !svc->transport.ble_at_mode || svc->transport.ble_mac[0] == '\0') return "";
    return svc->transport.ble_mac;
}

bool gh_service_start_hbd(gh_service_t* svc,
                           uint8_t ctrl_bit, uint8_t mode, uint32_t func_mask) {
    if (!svc || !gh_service_is_connected(svc)) return false;
    bool ret;
    if (svc->use_chelsea_a_parser) {
        /* Chelsea A: GH3X_SwFunctionCmd(func_mask: u32, ctrl: u8)
         * 对齐原始上位机逻辑：一次操作仅发送一帧，ctrl 由上层直接决定。
         */
        uint8_t params[16];
        int plen, n;
        (void)mode;
        plen = 0;
        n = gh_rpc_pack_u32(params + plen, (int)sizeof(params) - plen, func_mask, false);
        if (n < 0) return false;
        plen += n;
        n = gh_rpc_pack_u8(params + plen, (int)sizeof(params) - plen, ctrl_bit, true);
        if (n < 0) return false;
        plen += n;
        ret = s_rpc_sall(svc, "GH3X_SwFunctionCmd", params, plen);
    } else {
        ret = gh_cmd_start_hbd(&svc->parser, ctrl_bit, mode, func_mask);
    }
    if (ret && ctrl_bit == 0) {
        svc->device_state = GH_DEV_STATE_SAMPLING;
        svc->has_last_frame_cnt = false;
        svc->last_frame_cnt = 0;
        svc->frame_gap_events = 0;
        svc->frame_gap_total = 0;
        /* 开启采集：打开 CSV 文件并写入表头 */
        if (svc->csv_filename[0] != '\0') {
            bool open_ok = false;
            GH_MUTEX_LOCK(&svc->csv_lock);
            if (svc->csv_fp) { fclose(svc->csv_fp); svc->csv_fp = NULL; }
            svc->csv_fp = fopen(svc->csv_filename, "w");
            if (svc->csv_fp) {
                s_csv_write_header(svc->csv_fp);
                svc->csv_rows_written = 0;
                open_ok = true;
            }
            GH_MUTEX_UNLOCK(&svc->csv_lock);
            if (open_ok && svc->on_log) {
                char log_buf[320];
                snprintf(log_buf, sizeof(log_buf), "[CSV] Saving to %s", svc->csv_filename);
                svc->on_log(log_buf, svc->on_log_ctx);
            }
        }
    } else if (ret && ctrl_bit == 1) {
        svc->device_state = GH_DEV_STATE_CONNECTED;
        /* 停止采集：关闭 CSV */
        bool was_open = false;
        GH_MUTEX_LOCK(&svc->csv_lock);
        if (svc->csv_fp) {
            fclose(svc->csv_fp);
            svc->csv_fp = NULL;
            was_open = true;
        }
        GH_MUTEX_UNLOCK(&svc->csv_lock);
        if (was_open && svc->on_log) {
            char log_buf[320];
            snprintf(log_buf, sizeof(log_buf), "[CSV] Saved to %s", svc->csv_filename);
            svc->on_log(log_buf, svc->on_log_ctx);
        }
    }
    if (svc->on_state) svc->on_state(svc->device_state, svc->on_state_ctx);
    return ret;
}

bool gh_service_config_download(gh_service_t* svc,
                                 const gh_reg_t* regs, uint8_t count) {
    if (!svc || !gh_service_is_connected(svc) || !regs || count == 0) return false;
    if (svc->use_chelsea_a_parser) {
        /* 对齐原 Qt 上位机 CardiffRPCCall::sendConfig 行为：
         *   send("GH3X_ChipCtrl", 0xC2)
         *   send("download_config", 0)
         *   send("GH3X_RegsListWriteCmd", <u16*>) 分包，每包最多100个u16
         *   send("download_config", 1)
         * 注意：这里使用 send（非 sall），避免设备在某些固件上对配置流程限于普通发送通道。
         */
        uint8_t params[4];
        int n;

        /* Step 0: GH3X_ChipCtrl(0xC2) */
        n = gh_rpc_pack_u8(params, (int)sizeof(params), 0xC2U, true);
        if (n < 0 || !s_rpc_send(svc, "GH3X_ChipCtrl", params, n)) return false;

        /* Step 1: download_config(0) */
        n = gh_rpc_pack_u8(params, (int)sizeof(params), 0U, true);
        if (n < 0 || !s_rpc_send(svc, "download_config", params, n)) return false;

        /* Step 2: GH3X_RegsListWriteCmd(pairs) 分包 */
        {
            const int max_regs = 200;
            const int pair_count = ((int)count > max_regs) ? max_regs : (int)count;
            const int total_u16 = pair_count * 2;      /* addr,val 交替 */
            const int chunk_u16 = 100;                 /* 与原版一致：每包最多100个u16 */
            uint16_t u16_pairs[400];
            for (int i = 0; i < pair_count; i++) {
                u16_pairs[i * 2]     = regs[i].addr;
                u16_pairs[i * 2 + 1] = regs[i].data;
            }

            for (int off = 0; off < total_u16; off += chunk_u16) {
                int cur = total_u16 - off;
                if (cur > chunk_u16) cur = chunk_u16;
                uint8_t arr_params[GH_RPC_FRAME_MAX];
                int arr_len = gh_rpc_pack_u16_array(arr_params, (int)sizeof(arr_params),
                                                    &u16_pairs[off], cur, true);
                if (arr_len < 0 || !s_rpc_send(svc, "GH3X_RegsListWriteCmd", arr_params, arr_len)) {
                    return false;
                }
            }
        }

        /* Step 3: download_config(1) */
        n = gh_rpc_pack_u8(params, (int)sizeof(params), 1U, true);
        return (n >= 0) ? s_rpc_send(svc, "download_config", params, n) : false;
    }
    return gh_cmd_config_download(&svc->parser, regs, count);
}

bool gh_service_register_rw(gh_service_t* svc, const gh_cmd_param_t* param) {
    if (!svc || !gh_service_is_connected(svc) || !param) return false;
    if (svc->use_chelsea_a_parser) {
        uint8_t params[GH_RPC_FRAME_MAX];
        int plen = 0;
        int n;
        uint8_t op = param->args.reg_oper.op_mode;
        uint16_t addr = param->args.reg_oper.reg_addr;
        uint8_t  reg_count = param->args.reg_oper.reg_count;

        if (op == 0U) {
            /* 读操作: GH3X_RegsReadCmd(addr: u16, count: d32) */
            n = gh_rpc_pack_u16(params + plen, (int)sizeof(params) - plen, addr, false);
            if (n < 0) return false;
            plen += n;
            n = gh_rpc_pack_i32(params + plen, (int)sizeof(params) - plen, (int32_t)reg_count, true);
            if (n < 0) return false;
            plen += n;
            return s_rpc_sall(svc, "GH3X_RegsReadCmd", params, plen);
        } else if (op == 1U && param->args.reg_oper.regs) {
            /* 写操作: GH3X_RegsListWriteCmd(pairs: u16*) */
            const gh_reg_t *regs = param->args.reg_oper.regs;
            uint16_t u16_pairs[64 * 2];
            int cnt = (reg_count > 64) ? 64 : reg_count;
            for (int i = 0; i < cnt; i++) {
                u16_pairs[i * 2]     = regs[i].addr;
                u16_pairs[i * 2 + 1] = regs[i].data;
            }
            int arr_len = gh_rpc_pack_u16_array(params, (int)sizeof(params),
                                                 u16_pairs, cnt * 2, true);
            if (arr_len < 0) return false;
            return s_rpc_sall(svc, "GH3X_RegsListWriteCmd", params, arr_len);
        }
        return false;
    }
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
    if (svc->use_chelsea_a_parser) {
        /* Chelsea A: GH3X_ChipCtrl(ctrl: u8) — sall（原始协议抓包确认为 sall）*/
        uint8_t params[2];
        int n = gh_rpc_pack_u8(params, (int)sizeof(params), ctrl_val, true);
        if (n < 0) return false;
        return s_rpc_sall(svc, "GH3X_ChipCtrl", params, n);
    }
    return gh_cmd_cardiff_control(&svc->parser, ctrl_val);
}

bool gh_service_get_evk_version(gh_service_t* svc, uint8_t type) {
    if (!svc || !gh_service_is_connected(svc)) return false;
    if (svc->use_chelsea_a_parser) {
        /* Chelsea A: GH3X_GetVersion(type: u8) — sall，期望响应 */
        uint8_t params[2];
        int n = gh_rpc_pack_u8(params, (int)sizeof(params), type, true);
        if (n < 0) return false;
        return s_rpc_sall(svc, "GH3X_GetVersion", params, n);
    }
    return gh_cmd_get_evk_version(&svc->parser, type);
}

bool gh_service_set_work_mode(gh_service_t* svc, uint8_t mode, uint32_t func_mask) {
    if (!svc || !gh_service_is_connected(svc)) return false;
    if (svc->use_chelsea_a_parser) {
        /* Chelsea A: GH3X_SwFunctionCmd(func_mask: u32, mode: u8) */
        uint8_t params[GH_RPC_FRAME_MAX];
        int plen = 0;
        int n;
        n = gh_rpc_pack_u32(params + plen, (int)sizeof(params) - plen, func_mask, false);
        if (n < 0) return false;
        plen += n;
        n = gh_rpc_pack_u8(params + plen, (int)sizeof(params) - plen, mode, true);
        if (n < 0) return false;
        plen += n;
        return s_rpc_sall(svc, "GH3X_SwFunctionCmd", params, plen);
    }
    return gh_cmd_set_work_mode(&svc->parser, mode, func_mask);
}

void gh_service_set_csv_name(gh_service_t *svc, const char *config_name) {
    if (!svc) return;
    if (!config_name || config_name[0] == '\0') {
        svc->csv_filename[0] = '\0';
        return;
    }
    /* 去掉扩展名（如 "test.config" → "test"），然后加上 ".csv" */
    /* 先找最后一个斜杠（basename）*/
    const char *base = config_name;
    for (const char *p = config_name; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    /* 找最后一个点（扩展名）*/
    const char *dot = NULL;
    for (const char *p = base; *p; p++) {
        if (*p == '.') dot = p;
    }
    size_t stem_len = dot ? (size_t)(dot - base) : strlen(base);
    if (stem_len >= sizeof(svc->csv_filename) - 5) stem_len = sizeof(svc->csv_filename) - 5;
    memcpy(svc->csv_filename, base, stem_len);
    memcpy(svc->csv_filename + stem_len, ".csv", 5);
}

uint32_t gh_service_get_csv_rows_written(gh_service_t *svc) {
    if (!svc) return 0;
    uint32_t rows = 0;
    GH_MUTEX_LOCK(&svc->csv_lock);
    rows = svc->csv_rows_written;
    GH_MUTEX_UNLOCK(&svc->csv_lock);
    return rows;
}
