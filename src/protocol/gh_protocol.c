/* ============================================================
 * 文件: src/protocol/gh_protocol.c
 * 功能: GH3x2x 协议实现（打包/解包/差分解压/命令发送）
 * 说明: 纯C实现，无Qt/C++/STL依赖
 *       移植自 gh_zip_parser.cpp + gh_zip_command_api.cpp +
 *              gh_zip_command_send.cpp
 * ============================================================ */

#include "gh_protocol.h"
#include <string.h>   /* memset, memcpy */
#include <stdio.h>    /* printf（调试用，可移除）*/

/* ============================================================
 * CRC8 查找表（来自原 g_uchCrc8TabArr）
 * ============================================================ */
static const uint8_t s_crc8_tab[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

/* ============================================================
 * 宏：从字节数组构造多字节整型（大端读取）
 * ============================================================ */
#define GH_MAKE_DWORD(b0, b1, b2, b3) \
    (((uint32_t)(b3) << 24) | ((uint32_t)(b2) << 16) | \
     ((uint32_t)(b1) << 8)  | ((uint32_t)(b0)))

#define GH_MAKE_WORD(b0, b1) \
    (((uint16_t)(b1) << 8) | (uint16_t)(b0))

/* 上行数据包中各字段索引（来自 gh_uprotocol.h 宏定义） */
#define UPROTOCOL_FUNCTION_ID_INDEX     (0U)
#define UPROTOCOL_RAWDATA_TYPE_INDEX    (1U)
#define UPROTOCOL_RAWDATA_CHNL_NUM      (2U)
#define UPROTOCOL_PACKAGE_TYPE_INDEX    (6U)
#define UPROTOCOL_RAWDATA_LEN_INDEX     (7U)

/* 位域偏移（来自原宏 GH3X2X_GET_LEFT_SHIFT_VAL） */
#define UPROTOCOL_GS_ENABLE_FIELD       (0U)
#define UPROTOCOL_ALGO_ENABLE_FIELD     (1U)
#define UPROTOCOL_AGC_ENABLE_FIELD      (2U)
#define UPROTOCOL_AMBIANCE_ENABLE_FIELD (3U)
#define UPROTOCOL_GS_GYRO_ENABLE_FIELD  (4U)
#define UPROTOCOL_CAP_ENABLE_FIELD      (5U)
#define UPROTOCOL_TEMP_ENABLE_FIELD     (6U)
#define UPROTOCOL_ZIP_ENABLE_FIELD      (0U)
#define UPROTOCOL_ODDEVEN_FLAG_FIELD    (1U)
#define UPROTOCOL_FUNCTION_MODE_FIELD   (2U)
#define UPROTOCOL_SPLIC_PACK_CNT_FIELD  (3U)
#define UPROTOCOL_SPLIC_PACK_OVER_FIELD (5U)

/* ============================================================
 * 内部辅助：从字节中提取位域
 * ============================================================ */
static inline uint8_t s_get_bit(uint8_t byte, uint8_t field_offset) {
    return (byte >> field_offset) & 0x01U;
}

/* ============================================================
 * 公开API实现
 * ============================================================ */

/**
 * @brief 初始化协议解析器上下文
 */
void gh_parser_init(gh_parser_ctx_t* ctx,
                    void (*send_cb)(const uint8_t*, uint16_t, void*), void* send_ctx,
                    void (*on_frame)(const gh_frame_t*, void*), void* frame_ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(gh_parser_ctx_t));
    ctx->send_cb   = send_cb;
    ctx->send_ctx  = send_ctx;
    ctx->on_frame_parsed = on_frame;
    ctx->frame_ctx = frame_ctx;
    gh_parser_reset(ctx);
}

/**
 * @brief 重置解析器状态
 */
void gh_parser_reset(gh_parser_ctx_t* ctx) {
    if (!ctx) return;
    memset(&ctx->zip_last_data, 0, sizeof(gh_zip_last_data_t));
    ctx->current_func_id    = 0xFF;
    ctx->odd_even_flag      = 1;
    ctx->package_id         = 0;
    ctx->g_current_func_id  = 0;
    ctx->upload_status      = 0;
    ctx->func_info_valid_mask = 0ULL;
}

/**
 * @brief 计算CRC8（使用查找表，原 CalculateCrc8）
 */
uint8_t gh_calc_crc8(const uint8_t* data, uint8_t length) {
    uint8_t crc = 0xFF;
    uint8_t i;
    for (i = 0; i < length; i++) {
        crc = s_crc8_tab[crc ^ data[i]];
    }
    return crc;
}

/**
 * @brief 协议打包（内层UProtocol格式，原 UprotocolPacketFormat）
 *
 * 包格式: [0x55][0x01][cmd][len][payload...][CRC8]
 */
bool gh_packet_format(uint8_t cmd, const uint8_t* payload, uint8_t payload_len,
                      uint8_t* out_buf, uint8_t* out_len) {
    if (!out_buf || !out_len) return false;

    /* 检查总长度是否超限（头4字节 + payload + 1字节CRC） */
    uint16_t total = GH_UPROTOCOL_HEADER_LEN + payload_len + 1U;
    if (total > GH_UPROTOCOL_PACKET_MAX) return false;

    /* 填充包头 */
    out_buf[GH_PKT_IDX_FIXED]   = GH_UPROTOCOL_FIXED_HEADER; /* 0x55 */
    out_buf[GH_PKT_IDX_VER]     = GH_UPROTOCOL_VERSION;       /* 0x01 */
    out_buf[GH_PKT_IDX_CMD]     = cmd;
    out_buf[GH_PKT_IDX_LEN]     = payload_len;

    /* 拷贝 payload */
    *out_len = GH_UPROTOCOL_HEADER_LEN;
    if (payload_len > 0U && payload != NULL) {
        memcpy(&out_buf[*out_len], payload, payload_len);
        *out_len += payload_len;
    }

    /* 追加 CRC8 */
    out_buf[*out_len] = gh_calc_crc8(out_buf, *out_len);
    *out_len += 1U;
    return true;
}

/**
 * @brief 解析上行包头（原 GhUnPackPakcageHeader）
 */
static void s_unpack_header(gh_pack_header_t* hdr, const uint8_t* buf) {
    uint32_t complete_mask;
    uint8_t  i;

    hdr->need_continue = 0;
    hdr->func_id = buf[UPROTOCOL_FUNCTION_ID_INDEX];

    /* 解析 rawdata_type 字节中的各使能位 */
    hdr->gs_enable     = s_get_bit(buf[UPROTOCOL_RAWDATA_TYPE_INDEX], UPROTOCOL_GS_ENABLE_FIELD);
    hdr->algo_res_flag = s_get_bit(buf[UPROTOCOL_RAWDATA_TYPE_INDEX], UPROTOCOL_ALGO_ENABLE_FIELD);
    hdr->agc_enable    = s_get_bit(buf[UPROTOCOL_RAWDATA_TYPE_INDEX], UPROTOCOL_AGC_ENABLE_FIELD);
    hdr->amb_enable    = s_get_bit(buf[UPROTOCOL_RAWDATA_TYPE_INDEX], UPROTOCOL_AMBIANCE_ENABLE_FIELD);
    hdr->gs_gyro_enable = s_get_bit(buf[UPROTOCOL_RAWDATA_TYPE_INDEX], UPROTOCOL_GS_GYRO_ENABLE_FIELD);
    hdr->cap_enable    = s_get_bit(buf[UPROTOCOL_RAWDATA_TYPE_INDEX], UPROTOCOL_CAP_ENABLE_FIELD);
    hdr->temp_enable   = s_get_bit(buf[UPROTOCOL_RAWDATA_TYPE_INDEX], UPROTOCOL_TEMP_ENABLE_FIELD);

    /* 通道掩码（4字节大端）→ 生成 tag_array */
    complete_mask = GH_MAKE_DWORD(buf[UPROTOCOL_RAWDATA_CHNL_NUM + 3],
                                  buf[UPROTOCOL_RAWDATA_CHNL_NUM + 2],
                                  buf[UPROTOCOL_RAWDATA_CHNL_NUM + 1],
                                  buf[UPROTOCOL_RAWDATA_CHNL_NUM + 0]);
    for (i = 0; i < GH_CHANNEL_MAX; i++) {
        hdr->tag_array[i] = (complete_mask & (1U << i)) ? 0x00U : 0xFFU;
    }

    /* 解析 package_type 字节 */
    hdr->zip_enable_flag    = s_get_bit(buf[UPROTOCOL_PACKAGE_TYPE_INDEX], UPROTOCOL_ZIP_ENABLE_FIELD);
    hdr->odd_even_change_flag = s_get_bit(buf[UPROTOCOL_PACKAGE_TYPE_INDEX], UPROTOCOL_ODDEVEN_FLAG_FIELD);
    hdr->fifo_package_mode_flag = s_get_bit(buf[UPROTOCOL_PACKAGE_TYPE_INDEX], UPROTOCOL_FUNCTION_MODE_FIELD);
    hdr->package_cnt        = (buf[UPROTOCOL_PACKAGE_TYPE_INDEX] >> UPROTOCOL_SPLIC_PACK_CNT_FIELD) & 0x03U;
    hdr->package_over       = (buf[UPROTOCOL_PACKAGE_TYPE_INDEX] >> UPROTOCOL_SPLIC_PACK_OVER_FIELD) & 0x01U;
    hdr->rawdata_len        = buf[UPROTOCOL_RAWDATA_LEN_INDEX];
}

/**
 * @brief 差分解压原始数据（原 ParseRawDataZip）
 *
 * 压缩格式说明:
 *   byte[0]: 压缩数据总长度
 *   byte[1]: 标签标志（RAWDATA_DIFF_ODD=有变化，0=无变化）
 *   随后: 各通道差分数据（每个差分用4bit编码）
 *
 * 每个差分单元（4bit）:
 *   高3bit → 有效4bit分组数(chNum)
 *   低1bit → 方向（0=正差, 1=负差）
 */
bool gh_decompress_rawdata(gh_parser_ctx_t* ctx,
                            const uint8_t* zip_data, uint8_t chnl_cnt,
                            uint32_t* raw_data_out, uint8_t func_id) {
    uint8_t  tag_temp[GH_CHANNEL_MAX];
    uint8_t  diff_data_len;
    uint8_t  curr_byte; /* 当前字节索引 */
    uint8_t  curr_4bit; /* 当前4bit半字节（0=高, 1=低）*/
    uint8_t  ch;

    if (!zip_data || !raw_data_out || chnl_cnt == 0) return false;
    if (func_id >= GH_CHANNEL_MAX) return false;

    diff_data_len = zip_data[0];
    if (diff_data_len == 0) return false;

    curr_byte = 1;
    curr_4bit = 0;

    /* 步骤1：解析标签（tag） */
    if (zip_data[curr_byte] == RAWDATA_DIFF_ODD) {
        /* 标签有变化，直接读取新标签 */
        curr_byte++;
        memcpy(tag_temp, &zip_data[curr_byte], chnl_cnt);
        curr_byte += chnl_cnt;
    } else {
        /* 标签无变化，复用历史标签 */
        memcpy(tag_temp, ctx->zip_last_data.last_tag[func_id], chnl_cnt);
        curr_byte++;
    }

    /* 步骤2：解析各通道差分数据 */
    for (ch = 0; ch < chnl_cnt; ch++) {
        uint8_t  data_type;
        uint8_t  ch_num;    /* 有效4bit组数（最高有效组索引）*/
        uint8_t  diff_sta;  /* 差值方向 */
        uint32_t diff_val = 0;
        int8_t   i;

        if (curr_byte > diff_data_len) {
            /* 数据不足，当前值等于历史值 */
            raw_data_out[ch] = ctx->zip_last_data.last_rawdata[ch];
            continue;
        }

        /* 取4bit差分类型字段 */
        if (curr_4bit == 0) {
            data_type = (zip_data[curr_byte] >> RAWDATA_DIFF_BYTE_SIZE) & 0x0FU;
            curr_4bit = 1;
        } else {
            data_type = zip_data[curr_byte] & 0x0FU;
            curr_4bit = 0;
            curr_byte++;
        }

        ch_num   = data_type / RAWDATA_DIFF_EVEN;  /* 有效分组数 */
        diff_sta = data_type % RAWDATA_DIFF_EVEN;  /* 方向 */

        /* 从高位到低位拼接差值 */
        for (i = (int8_t)ch_num; i >= 0; i--) {
            uint8_t diff_unit;
            if (curr_byte > diff_data_len) {
                diff_val = 0;
                break;
            }
            if (curr_4bit == 0) {
                diff_unit = (zip_data[curr_byte] >> RAWDATA_DIFF_BYTE_SIZE) & 0x0FU;
                curr_4bit = 1;
            } else {
                diff_unit = zip_data[curr_byte] & 0x0FU;
                curr_4bit = 0;
                curr_byte++;
            }
            diff_val |= (uint32_t)diff_unit << ((uint8_t)i * RAWDATA_DIFF_BYTE_SIZE);
        }

        /* 计算当前值 */
        if (diff_sta == 0) {
            raw_data_out[ch] = ctx->zip_last_data.last_rawdata[ch] + diff_val;
        } else {
            raw_data_out[ch] = ctx->zip_last_data.last_rawdata[ch] - diff_val;
        }
    }

    return true;
}

/**
 * @brief 解析功能信息更新包（0x2C命令），存储通道数和映射
 */
bool gh_parse_func_info(gh_parser_ctx_t* ctx,
                        const uint8_t* payload, uint8_t length) {
    uint8_t func_id;
    uint8_t chnl_num;

    if (!ctx || !payload || length < 2) return false;

    func_id  = payload[0];
    if (func_id >= 64U) return false;

    /* 从 FunctionInfo 结构中读取通道数（偏移可能因结构体而异，此处简化处理） */
    chnl_num = payload[1 + /* offsetof uchChnlNum */ 0];  /* 具体偏移取决于原结构体 */

    if (chnl_num > GH_CHANNEL_MAX) chnl_num = GH_CHANNEL_MAX;

    ctx->func_info_map[func_id].chnl_limit = chnl_num;
    /* 标记该功能ID有效 */
    ctx->func_info_valid_mask |= (1ULL << func_id);

    /* 拷贝通道映射（紧跟在FunctionInfo结构体之后）*/
    if (length > 2U) {
        uint8_t copy_len = (length - 2U < chnl_num) ? (length - 2U) : chnl_num;
        memcpy(ctx->func_info_map[func_id].chnl_map, &payload[2], copy_len);
    }
    return true;
}

/**
 * @brief 解析一个完整的协议包（原 ParseCommand）
 *
 * 调用前提：packet 是去除外层 0xAA11 后的完整 UProtocol 包
 * 格式: [0x55][0x01][cmd][len][payload...][crc8]
 */
bool gh_parse_packet(gh_parser_ctx_t* ctx, const uint8_t* packet, uint16_t length) {
    uint8_t cmd;
    uint8_t payload_len;
    uint8_t expected_crc;
    uint8_t actual_crc;

    if (!ctx || !packet || length < GH_UPROTOCOL_HEADER_LEN + 1U) return false;

    /* 检查固定头 */
    if (packet[GH_PKT_IDX_FIXED] != GH_UPROTOCOL_FIXED_HEADER) return false;

    cmd         = packet[GH_PKT_IDX_CMD];
    payload_len = packet[GH_PKT_IDX_LEN];

    /* 验证长度合法性 */
    if ((uint16_t)(GH_UPROTOCOL_HEADER_LEN + payload_len + 1U) > length) return false;

    /* 验证 CRC8 */
    expected_crc = packet[GH_UPROTOCOL_HEADER_LEN + payload_len];
    actual_crc   = gh_calc_crc8(packet, GH_UPROTOCOL_HEADER_LEN + payload_len);
    if (expected_crc != actual_crc) return false;

    /* 指向 payload */
    const uint8_t* payload = &packet[GH_PKT_IDX_PAYLOAD_START];

    /* 根据 cmd 分发处理 */
    switch (cmd) {
        case GH_CMD_RAWDATA_PACKET:
        case GH_CMD_COMPRESSED_EVEN:
        case GH_CMD_COMPRESSED_ODD:
        case GH_CMD_NEW_PROTOCOL_RAWDATA: {
            /* 上行数据包：解析帧头 + 数据体 */
            gh_pack_header_t hdr;
            s_unpack_header(&hdr, payload);
            /* TODO: 调用多帧解析，结果通过 on_frame_parsed 回调通知上层 */
            /* gh_parse_multi_frame(ctx, payload, payload_len, &hdr); */
            break;
        }
        case GH_CMD_FUNCTION_INFO_UPDATE:
            /* 功能信息更新：记录通道映射 */
            gh_parse_func_info(ctx, payload, payload_len);
            break;
        case GH_CMD_OPERATION_ACK:
            /* 操作应答：可以通知 service 层命令执行结果 */
            break;
        default:
            /* 其余命令，忽略或透传给上层 */
            break;
    }

    return true;
}

/* ============================================================
 * 命令构造函数（下行）
 * ============================================================ */

/**
 * @brief 内部：打包 + 调用 send_cb
 */
static bool s_send_cmd(gh_parser_ctx_t* ctx,
                        uint8_t cmd, const uint8_t* payload, uint8_t payload_len) {
    uint8_t out_len = 0;
    if (!gh_packet_format(cmd, payload, payload_len,
                           ctx->out_packet_buf, &out_len)) {
        return false;
    }
    if (ctx->send_cb) {
        ctx->send_cb(ctx->out_packet_buf, out_len, ctx->send_ctx);
    }
    return true;
}

/**
 * @brief 发送 StartHBD 命令（原 SendStartHbd）
 *
 * Payload 格式（7字节）:
 *   [0] ctrl_bit  [1] mode  [2] 0x00
 *   [3-6] func_mask（小端）
 */
bool gh_cmd_start_hbd(gh_parser_ctx_t* ctx,
                      uint8_t ctrl_bit, uint8_t mode, uint32_t func_mask) {
    uint8_t payload[7];
    payload[0] = ctrl_bit;
    payload[1] = mode;
    payload[2] = 0x00U;
    payload[3] = (uint8_t)(func_mask        & 0xFFU);
    payload[4] = (uint8_t)((func_mask >> 8) & 0xFFU);
    payload[5] = (uint8_t)((func_mask >> 16)& 0xFFU);
    payload[6] = (uint8_t)((func_mask >> 24)& 0xFFU);
    return s_send_cmd(ctx, GH_CMD_START_HBD, payload, sizeof(payload));
}

/**
 * @brief 发送工作模式设置（原 SendSlaveWorkModeSetting）
 *
 * Payload 格式（5字节）:
 *   [0] mode  [1-4] func_mask（小端）
 */
bool gh_cmd_set_work_mode(gh_parser_ctx_t* ctx, uint8_t mode, uint32_t func_mask) {
    uint8_t payload[5];
    payload[0] = mode;
    payload[1] = (uint8_t)(func_mask        & 0xFFU);
    payload[2] = (uint8_t)((func_mask >> 8) & 0xFFU);
    payload[3] = (uint8_t)((func_mask >> 16)& 0xFFU);
    payload[4] = (uint8_t)((func_mask >> 24)& 0xFFU);
    return s_send_cmd(ctx, GH_CMD_SLAVE_WORK_MODE, payload, sizeof(payload));
}

/**
 * @brief 发送寄存器读写（原 SendRegisterReadWrite）
 *
 * Payload 格式:
 *   [0] op_mode  [1] reg_count
 *   [写模式] [2] addr_hi [3] addr_lo [4..] data_hi data_lo ...
 *   [读模式] [2] addr_hi [3] addr_lo
 */
bool gh_cmd_register_rw(gh_parser_ctx_t* ctx, const gh_cmd_param_t* param) {
    uint8_t  payload[64] = {0};
    uint8_t  len = 0;
    uint8_t  op_mode   = param->args.reg_oper.op_mode;
    uint8_t  reg_count = param->args.reg_oper.reg_count;
    uint16_t reg_addr  = param->args.reg_oper.reg_addr;
    const gh_reg_t* regs = param->args.reg_oper.regs;

    payload[len++] = op_mode;
    payload[len++] = reg_count;

    if (op_mode == 0U || op_mode == 1U) {
        /* 读/写：添加起始地址 */
        payload[len++] = (uint8_t)((reg_addr >> 8) & 0xFFU); /* 高字节 */
        payload[len++] = (uint8_t)(reg_addr & 0xFFU);        /* 低字节 */

        if (op_mode == 1U && regs != NULL) {
            /* 写操作追加数据 */
            uint8_t i;
            for (i = 0; i < reg_count && len + 2 <= sizeof(payload); i++) {
                payload[len++] = (uint8_t)((regs[i].data >> 8) & 0xFFU);
                payload[len++] = (uint8_t)(regs[i].data & 0xFFU);
            }
        }
    } else if (op_mode == 2U) {
        /* 写MAC：6字节MAC地址 */
        uint8_t mac_len = 6U;
        payload[len++] = mac_len;
        if (regs != NULL) {
            memcpy(&payload[len], regs, mac_len);
            len += mac_len;
        }
    }

    return s_send_cmd(ctx, GH_CMD_REGISTER_READ_WRITE, payload, len);
}

/**
 * @brief 发送配置数据下发（原 SendConfigDataDownload）
 *
 * Payload 格式: [addr_hi][addr_lo][data_hi][data_lo] × N
 */
bool gh_cmd_config_download(gh_parser_ctx_t* ctx,
                             const gh_reg_t* regs, uint8_t count) {
    uint8_t  payload[128] = {0};
    uint8_t  len = 0;
    uint8_t  i;

    if (!regs || count == 0U) return false;

    for (i = 0; i < count && len + 4 <= sizeof(payload); i++) {
        payload[len++] = (uint8_t)((regs[i].addr >> 8) & 0xFFU);
        payload[len++] = (uint8_t)(regs[i].addr & 0xFFU);
        payload[len++] = (uint8_t)((regs[i].data >> 8) & 0xFFU);
        payload[len++] = (uint8_t)(regs[i].data & 0xFFU);
    }

    return s_send_cmd(ctx, GH_CMD_CONFIG_DATA_DOWNLOAD, payload, len);
}

/**
 * @brief 发送 Cardiff 控制命令（原 SendCardiffControl）
 * Payload: [0] ctrl_val
 */
bool gh_cmd_cardiff_control(gh_parser_ctx_t* ctx, uint8_t ctrl_val) {
    return s_send_cmd(ctx, GH_CMD_CARDIFF_CONTROL, &ctrl_val, 1U);
}

/**
 * @brief 发送 Cardiff 事件上报命令（原 SendCardiffEventReport）
 * Payload: [0] low_byte  [1] high_byte（小端）
 */
bool gh_cmd_cardiff_event_report(gh_parser_ctx_t* ctx, uint16_t event_val) {
    uint8_t payload[2];
    payload[0] = (uint8_t)(event_val & 0xFFU);
    payload[1] = (uint8_t)((event_val >> 8) & 0xFFU);
    return s_send_cmd(ctx, GH_CMD_CARDIFF_EVENT_REPORT, payload, sizeof(payload));
}

/**
 * @brief 发送获取EVK版本命令（原 SendGetEvkVersion）
 * Payload: [0] type
 */
bool gh_cmd_get_evk_version(gh_parser_ctx_t* ctx, uint8_t type) {
    return s_send_cmd(ctx, GH_CMD_GET_EVK_VERSION, &type, 1U);
}
