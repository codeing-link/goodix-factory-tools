/* ============================================================
 * 文件: src/protocol/gh_rpc.c
 * 功能: Cardiff RPC 协议帧构建与解析实现
 * ============================================================ */

#include "gh_rpc.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 帧构建实现
 * ============================================================ */

int gh_rpc_build_frame(uint8_t *out_buf, int out_max,
                       bool secure, uint8_t com_id,
                       const char *key,
                       const uint8_t *params, int params_len) {
    if (!out_buf || !key || out_max < 4) return -1;
    int key_len = (int)strlen(key);
    if (key_len == 0 || key_len > 128) return -1;

    int is_multi = (key_len > 1) ? 1 : 0;

    /* 计算 payload 长度：TypeKEY(1) + [key_len_byte(1) if multi] + key_bytes + [com_id(1) if secure] + params */
    int payload_len = 1 + (is_multi ? 1 : 0) + key_len + (secure ? 1 : 0) + params_len;

    /* 总帧长度：AA(1) + 11(1) + len_byte(1) + payload(len) + CRC(1) */
    int total = 3 + payload_len + 1;
    if (total > out_max) return -1;

    int idx = 0;
    out_buf[idx++] = 0xAAU;
    out_buf[idx++] = 0x11U;
    out_buf[idx++] = (uint8_t)payload_len;

    /* TypeKEY */
    if (is_multi) {
        out_buf[idx++] = secure ? GH_RPC_TYPEKEY_MULTI_SALL : GH_RPC_TYPEKEY_MULTI_SEND;
        out_buf[idx++] = (uint8_t)key_len;
    } else {
        out_buf[idx++] = secure ? GH_RPC_TYPEKEY_SINGLE_SALL : GH_RPC_TYPEKEY_SINGLE_SEND;
    }

    /* key 字节 */
    memcpy(&out_buf[idx], key, (size_t)key_len);
    idx += key_len;

    /* com_id（仅 sall）*/
    if (secure) {
        out_buf[idx++] = com_id;
    }

    /* 参数字节 */
    if (params && params_len > 0) {
        memcpy(&out_buf[idx], params, (size_t)params_len);
        idx += params_len;
    }

    /* CRC = sum(frame[3..idx-1]) mod 256 */
    uint8_t crc = 0;
    for (int i = 3; i < idx; i++) {
        crc = (uint8_t)(crc + out_buf[i]);
    }
    out_buf[idx++] = crc;

    return idx;
}

/* ============================================================
 * 参数序列化辅助
 * ============================================================ */

int gh_rpc_pack_u8(uint8_t *buf, int max, uint8_t val, bool last_param) {
    if (!buf || max < 2) return -1;
    buf[0] = last_param ? GH_RPC_TYPEDATA_U8_LAST : GH_RPC_TYPEDATA_U8_MORE;
    buf[1] = val;
    return 2;
}

int gh_rpc_pack_u16(uint8_t *buf, int max, uint16_t val, bool last_param) {
    if (!buf || max < 3) return -1;
    buf[0] = last_param ? GH_RPC_TYPEDATA_U16_LAST : GH_RPC_TYPEDATA_U16_MORE;
    buf[1] = (uint8_t)(val & 0xFFU);
    buf[2] = (uint8_t)((val >> 8) & 0xFFU);
    return 3;
}

int gh_rpc_pack_u32(uint8_t *buf, int max, uint32_t val, bool last_param) {
    if (!buf || max < 5) return -1;
    buf[0] = last_param ? GH_RPC_TYPEDATA_U32_LAST : GH_RPC_TYPEDATA_U32_MORE;
    buf[1] = (uint8_t)(val & 0xFFU);
    buf[2] = (uint8_t)((val >> 8) & 0xFFU);
    buf[3] = (uint8_t)((val >> 16) & 0xFFU);
    buf[4] = (uint8_t)((val >> 24) & 0xFFU);
    return 5;
}

int gh_rpc_pack_i32(uint8_t *buf, int max, int32_t val, bool last_param) {
    if (!buf || max < 5) return -1;
    buf[0] = last_param ? GH_RPC_TYPEDATA_I32_LAST : GH_RPC_TYPEDATA_I32_MORE;
    uint32_t uval = (uint32_t)val;
    buf[1] = (uint8_t)(uval & 0xFFU);
    buf[2] = (uint8_t)((uval >> 8) & 0xFFU);
    buf[3] = (uint8_t)((uval >> 16) & 0xFFU);
    buf[4] = (uint8_t)((uval >> 24) & 0xFFU);
    return 5;
}

int gh_rpc_pack_u16_array(uint8_t *buf, int max,
                           const uint16_t *data, int count, bool last_param) {
    if (!buf || !data || count <= 0) return -1;

    int idx = 0;
    const uint16_t *ptr = data;
    int remaining = count;

    /* 分段：每段最多 255 个元素（split=1 表示还有后续段）*/
    while (remaining > 255) {
        int needed = 2 + 255 * 2;
        if (idx + needed > max) return -1;
        buf[idx++] = GH_RPC_TYPEDATA_U16ARR_SPLIT; /* split=1, end=0 */
        buf[idx++] = 255;
        for (int i = 0; i < 255; i++) {
            buf[idx++] = (uint8_t)(ptr[i] & 0xFFU);
            buf[idx++] = (uint8_t)((ptr[i] >> 8) & 0xFFU);
        }
        ptr += 255;
        remaining -= 255;
    }

    /* 最后一段（split=0）*/
    int needed = 2 + remaining * 2;
    if (idx + needed > max) return -1;
    buf[idx++] = last_param ? GH_RPC_TYPEDATA_U16ARR_LAST : (GH_RPC_TYPEDATA_U16ARR_LAST & ~0x40U);
    buf[idx++] = (uint8_t)remaining;
    for (int i = 0; i < remaining; i++) {
        buf[idx++] = (uint8_t)(ptr[i] & 0xFFU);
        buf[idx++] = (uint8_t)((ptr[i] >> 8) & 0xFFU);
    }

    return idx;
}

/* ============================================================
 * 帧解析实现
 * ============================================================ */

bool gh_rpc_parse_frame(const uint8_t *frame, int len,
                        char *key_out, int key_max,
                        const uint8_t **payload, int *payload_len) {
    if (!frame || len < 5) return false;
    if (frame[0] != 0xAAU || frame[1] != 0x11U) return false;

    int frame_payload_len = (int)frame[2];
    /* 期望总帧长度 = 4 + frame_payload_len（AA + 11 + len_byte + payload + CRC）*/
    int expected_total = 4 + frame_payload_len;
    if (len < expected_total) return false;

    /* CRC 校验：sum(frame[3..2+frame_payload_len]) mod 256 == frame[3+frame_payload_len] */
    uint8_t crc = 0;
    for (int i = 3; i < 3 + frame_payload_len; i++) {
        crc = (uint8_t)(crc + frame[i]);
    }
    if (crc != frame[3 + frame_payload_len]) return false;

    /* 解析 TypeKEY */
    if (3 >= 3 + frame_payload_len) return false; /* 无 payload */
    uint8_t type_key = frame[3];
    int is_array_key = (int)((type_key >> 2) & 0x01U);
    int secure       = (int)((type_key >> 6) & 0x01U);

    int offset = 4; /* 当前解析位置（已跳过 AA11 + len + TypeKEY）*/
    int payload_end = 3 + frame_payload_len; /* CRC 所在索引（不含 CRC 的最后一个 payload 字节之后）*/

    if (is_array_key) {
        /* 多字符 key：[key_len][key_bytes...] */
        if (offset >= payload_end) return false;
        int key_len = (int)frame[offset++];
        if (offset + key_len > payload_end) return false;
        if (key_out && key_max > 0) {
            int copy = (key_len < key_max - 1) ? key_len : key_max - 1;
            memcpy(key_out, &frame[offset], (size_t)copy);
            key_out[copy] = '\0';
        }
        offset += key_len;
    } else {
        /* 单字符 key：直接跟一个字节 */
        if (offset >= payload_end) return false;
        if (key_out && key_max >= 2) {
            key_out[0] = (char)frame[offset];
            key_out[1] = '\0';
        }
        offset++;
    }

    /* com_id（仅 secure=1 时存在）*/
    if (secure) {
        if (offset >= payload_end) return false;
        offset++; /* 跳过 com_id */
    }

    /* 剩余部分即为 params */
    if (payload)     *payload     = &frame[offset];
    if (payload_len) *payload_len = payload_end - offset;

    return true;
}

bool gh_rpc_extract_u8_array(const uint8_t *params, int params_len,
                              const uint8_t **data_out, int *data_len) {
    if (!params || params_len <= 0 || !data_out || !data_len) return false;

    /* 某些固件在 u8* 前可能有标量参数，因此这里扫描参数列表，找到第一个 u8 数组 */
    static uint8_t s_u8_buf[4096];
    int pi = 0;
    while (pi < params_len) {
        uint8_t type_b  = params[pi++];
        uint8_t is_arr  = (type_b >> 2) & 0x01U;
        uint8_t end_b   = (type_b >> 6) & 0x01U;
        uint8_t split_b = (type_b >> 7) & 0x01U;
        int width_b = (1 << ((type_b >> 3) & 0x07U)) / 8;
        if (width_b < 1) width_b = 1;

        if (!is_arr) {
            if (pi + width_b > params_len) return false;
            pi += width_b;
            if (end_b) break;
            continue;
        }

        int out_len = 0;
        do {
            if (pi >= params_len) return false;
            int count = (int)params[pi++];
            int chunk_bytes = count * width_b;
            if (chunk_bytes < 0 || pi + chunk_bytes > params_len) return false;

            if (width_b == 1 && count > 0) {
                if (out_len + count > (int)sizeof(s_u8_buf)) return false;
                memcpy(&s_u8_buf[out_len], &params[pi], (size_t)count);
                out_len += count;
            }
            pi += chunk_bytes;

            if (!split_b) break;
            if (pi >= params_len) return false;
            type_b  = params[pi++];
            is_arr  = (type_b >> 2) & 0x01U;
            split_b = (type_b >> 7) & 0x01U;
            width_b = (1 << ((type_b >> 3) & 0x07U)) / 8;
            if (width_b < 1) width_b = 1;
            if (!is_arr) return false;
        } while (1);

        if (out_len > 0) {
            *data_out = s_u8_buf;
            *data_len = out_len;
            return true;
        }
        if (end_b) break;
    }

    return false;
}

bool gh_rpc_extract_u16_array(const uint8_t *params, int params_len,
                               uint16_t *vals_out, int max_vals, int *num_vals) {
    if (!params || params_len < 2 || !vals_out || max_vals <= 0 || !num_vals) return false;
    *num_vals = 0;

    int pi = 0;
    while (pi < params_len) {
        uint8_t type_b  = params[pi++];
        uint8_t is_arr  = (type_b >> 2) & 0x01U;
        uint8_t end_b   = (type_b >> 6) & 0x01U;
        uint8_t split_b = (type_b >> 7) & 0x01U;
        int     width   = (type_b >> 3) & 0x07U;  /* log2(bits) */
        int     elem_sz = (1 << width) / 8;        /* bytes per element */
        if (elem_sz < 1) elem_sz = 1;

        if (is_arr) {
            /* 数组头：读 count 字节，然后 count * elem_sz 数据字节 */
            do {
                if (pi >= params_len) goto done;
                int count = (int)params[pi++];
                if (pi + count * elem_sz > params_len) goto done;

                if (width == 4) {
                    /* u16 数组：读最多 max_vals 个元素，剩余跳过 */
                    int read_n = count;
                    if (*num_vals + read_n > max_vals) read_n = max_vals - *num_vals;
                    for (int i = 0; i < read_n; i++) {
                        vals_out[*num_vals] = (uint16_t)params[pi] |
                                             ((uint16_t)params[pi + 1] << 8);
                        (*num_vals)++;
                        pi += 2;
                    }
                    pi += (count - read_n) * 2; /* 跳过超出的元素 */
                } else {
                    /* 其他宽度数组：跳过 */
                    pi += count * elem_sz;
                }

                if (!split_b) break;
                /* split=1: 读下一段的 TypeArray 头 */
                if (pi >= params_len) goto done;
                type_b  = params[pi++];
                split_b = (type_b >> 7) & 0x01U;
            } while (true);

            if (width == 4 && *num_vals > 0) return true; /* 找到了 u16 数组 */
        } else {
            /* 标量：跳过 elem_sz 字节 */
            if (pi + elem_sz > params_len) break;
            pi += elem_sz;
        }
        if (end_b) break;
    }
done:
    return (*num_vals > 0);
}
