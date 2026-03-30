/* ============================================================
 * 文件: src/protocol/gh_rpc.h
 * 功能: Cardiff RPC 协议帧构建与解析
 *       适用于 Chelsea A 通信模式
 *
 * 帧格式 (host → device):
 *   [AA][11][len][TypeKEY][key_len_if_multi][key...][com_id_if_sall][params...][crc_sum]
 *
 *   TypeKEY byte: [pack_type:2][is_array:1][width:3][secure:1][fin:1]
 *     - is_array=1: multi-char key, followed by key_len byte then key bytes
 *     - is_array=0: single-char key, followed by 1 key byte directly
 *     - secure=1: sall (expects response), followed by com_id byte
 *     - secure=0: send (fire-and-forget)
 *
 *   TypeData byte: [pack_type:2][is_array:1][width:3][end:1][split:1]
 *     - width: log2(bits) → 3=u8, 4=u16, 5=u32
 *     - pack_type: 0=signed, 1=unsigned
 *     - end=1: last parameter in frame
 *
 *   TypeArray byte: same layout as TypeData but is_array=1
 *     Followed by [count: 1 byte][data * count * (2^width/8)]
 *     If split=1, more chunks follow.
 *
 *   len   = number of bytes from TypeKEY through last param byte (excl. CRC)
 *   crc   = sum(frame[3..2+len]) mod 256
 * ============================================================ */

#ifndef GH_RPC_H
#define GH_RPC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Max TX frame buffer size (1024 bytes supports up to ~250 register pairs) */
#define GH_RPC_FRAME_MAX  1024

/* ---- TypeKEY constants ---- */
/* Multi-char key (is_array=1, width=3, pack_type=2, fin=1) */
#define GH_RPC_TYPEKEY_MULTI_SALL  0xDE  /* secure=1: sall */
#define GH_RPC_TYPEKEY_MULTI_SEND  0x9E  /* secure=0: send */
/* Single-char key (is_array=0, width=3, pack_type=2, fin=1) */
#define GH_RPC_TYPEKEY_SINGLE_SALL 0xDA  /* secure=1: sall */
#define GH_RPC_TYPEKEY_SINGLE_SEND 0x9A  /* secure=0: send */

/* ---- TypeData constants ---- */
/* u8 (unsigned, width=3, not-array) */
#define GH_RPC_TYPEDATA_U8_LAST    0x59  /* end=1 */
#define GH_RPC_TYPEDATA_U8_MORE    0x19  /* end=0 */
/* u16 (unsigned, width=4, not-array) */
#define GH_RPC_TYPEDATA_U16_LAST   0x61  /* end=1 */
#define GH_RPC_TYPEDATA_U16_MORE   0x21  /* end=0 */
/* u32 (unsigned, width=5, not-array) */
#define GH_RPC_TYPEDATA_U32_LAST   0x69  /* end=1 */
#define GH_RPC_TYPEDATA_U32_MORE   0x29  /* end=0 */
/* i32/d32 (signed/GH_PRO_TYPE_SIGNED=2, width=5, not-array) */
#define GH_RPC_TYPEDATA_I32_LAST   0x6A  /* end=1  pack_type=2 */
#define GH_RPC_TYPEDATA_I32_MORE   0x2A  /* end=0  pack_type=2 */
/* u8 array (unsigned, width=3, is_array=1) */
#define GH_RPC_TYPEDATA_U8ARR_LAST  0x5D /* end=1, split=0 */
/* u16 array (unsigned, width=4, is_array=1) */
#define GH_RPC_TYPEDATA_U16ARR_LAST  0x65 /* end=1, split=0 */
#define GH_RPC_TYPEDATA_U16ARR_SPLIT 0xE5 /* split=1 (for >255 elements) */

/* ============================================================
 * 帧构建接口
 * ============================================================ */

/**
 * @brief 构建一帧 Cardiff RPC 完整帧（含 AA11 头和 CRC）
 *
 * @param out_buf    输出缓冲区
 * @param out_max    缓冲区容量（建议 >= GH_RPC_FRAME_MAX）
 * @param secure     true=sall（期望设备响应），false=send（单向）
 * @param com_id     通信ID（仅 secure=true 时有效）
 * @param key        功能键字符串（如 "GH3X_GetVersion"）
 * @param params     已打包的参数字节（使用下面的 gh_rpc_pack_* 系列填充）
 * @param params_len 参数字节数
 * @return 写入 out_buf 的总字节数，失败返回 -1
 */
int gh_rpc_build_frame(uint8_t *out_buf, int out_max,
                       bool secure, uint8_t com_id,
                       const char *key,
                       const uint8_t *params, int params_len);

/* ============================================================
 * 参数序列化辅助函数
 * 用法：在调用 gh_rpc_build_frame 前，依次调用这些函数填充 params 缓冲区
 * last_param=true 表示这是最后一个参数（会设置 TypeData 的 end 位）
 * ============================================================ */

/** 打包 1 个 u8 值。返回写入字节数（=2），失败 -1 */
int gh_rpc_pack_u8(uint8_t *buf, int max, uint8_t val, bool last_param);

/** 打包 1 个 u16 值（小端序）。返回写入字节数（=3），失败 -1 */
int gh_rpc_pack_u16(uint8_t *buf, int max, uint16_t val, bool last_param);

/** 打包 1 个 u32 值（小端序）。返回写入字节数（=5），失败 -1 */
int gh_rpc_pack_u32(uint8_t *buf, int max, uint32_t val, bool last_param);

/** 打包 1 个有符号 i32 值（小端序）。返回写入字节数（=5），失败 -1 */
int gh_rpc_pack_i32(uint8_t *buf, int max, int32_t val, bool last_param);

/**
 * @brief 打包 u16 数组（格式：[TypeArray][count][elem0_lo][elem0_hi]...）
 * 支持 count > 255 的自动分包（split）
 * @return 写入字节数，失败 -1
 */
int gh_rpc_pack_u16_array(uint8_t *buf, int max,
                           const uint16_t *data, int count, bool last_param);

/* ============================================================
 * 帧解析接口
 * ============================================================ */

/**
 * @brief 解析收到的 Cardiff RPC 帧，提取 key 和 params 指针
 *
 * @param frame       完整帧（含 AA11 头）
 * @param len         帧总长度（含 CRC 尾字节）
 * @param key_out     输出：以 '\0' 结尾的 key 字符串
 * @param key_max     key_out 缓冲区大小
 * @param payload     输出：指向帧内第一个 param 字节的指针（不做拷贝）
 * @param payload_len 输出：params 区域字节数（不含 CRC）
 * @return true=解析成功（CRC 验证通过）
 */
bool gh_rpc_parse_frame(const uint8_t *frame, int len,
                        char *key_out, int key_max,
                        const uint8_t **payload, int *payload_len);

/**
 * @brief 从 params 中扫描并提取第一个 u16 array 参数的值（处理前置标量）
 * 遍历所有标量 TypeData 直到找到 is_array=1 且 width=4 (u16) 的 TypeArray
 *
 * @param params      params 区域指针
 * @param params_len  params 区域字节数
 * @param vals_out    输出：u16 数组缓冲区
 * @param max_vals    vals_out 容量（元素数）
 * @param num_vals    输出：实际读取的 u16 元素数
 * @return true=找到并成功提取
 */
bool gh_rpc_extract_u16_array(const uint8_t *params, int params_len,
                               uint16_t *vals_out, int max_vals, int *num_vals);

/**
 * @brief 从 u8 array 参数中提取数据指针和长度（不拷贝）
 * 处理 split 分段的情况，返回第一段的数据起点和总字节数（仅跨连续帧不可用）
 *
 * @param params      params 区域指针（gh_rpc_parse_frame 返回的 payload）
 * @param params_len  params 区域长度
 * @param data_out    输出：指向实际数据字节的指针
 * @param data_len    输出：实际数据字节数
 * @return true=成功找到 u8 array 参数
 */
bool gh_rpc_extract_u8_array(const uint8_t *params, int params_len,
                              const uint8_t **data_out, int *data_len);

#ifdef __cplusplus
}
#endif

#endif /* GH_RPC_H */
