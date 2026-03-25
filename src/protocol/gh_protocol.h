/* ============================================================
 * 文件: src/protocol/gh_protocol.h
 * 功能: GH3x2x 通信协议定义与打包/解包接口
 * 说明: 此文件为纯C实现，无任何Qt/C++依赖
 *       替换原 gh_zip_parser.h + gh_zip_parser_type.h
 * ============================================================ */

#ifndef GH_PROTOCOL_H
#define GH_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 协议常量定义
 * 来源: gh_uprotocol.h（精简复用）
 * ============================================================ */

/* 帧起始标志（串口分帧用） */
#define GH_FRAME_SYNC_BYTE0         (0xAAU)
#define GH_FRAME_SYNC_BYTE1         (0x11U)

/* UProtocol 内层包常量 */
#define GH_UPROTOCOL_FIXED_HEADER   (0x55U)    /* 固定帧头 */
#define GH_UPROTOCOL_VERSION        (0x01U)    /* 协议版本 */
#define GH_UPROTOCOL_HEADER_LEN     (4U)       /* 包头长度: FIXED+VER+CMD+LEN */
#define GH_UPROTOCOL_PACKET_MAX     (255U)     /* 最大包长度（含头和CRC） */

/* 包结构索引 */
#define GH_PKT_IDX_FIXED            (0U)       /* 固定帧头字节位置 */
#define GH_PKT_IDX_VER              (1U)       /* 版本字节位置 */
#define GH_PKT_IDX_CMD              (2U)       /* 命令字节位置 */
#define GH_PKT_IDX_LEN              (3U)       /* 长度字节位置（payload长度）*/
#define GH_PKT_IDX_PAYLOAD_START    (4U)       /* payload 起始位置 */

/* 最大通道数 */
#define GH_CHANNEL_MAX              (32U)

/* 差分压缩相关常量 */
#define RAWDATA_DIFF_BYTE_SIZE      (4U)       /* 每个差分单元 4bit */
#define RAWDATA_DIFF_ODD            (0x01U)    /* 标签变化标志 */
#define RAWDATA_DIFF_EVEN           (0x02U)    /* 偶数相关 */

/* ============================================================
 * 命令码定义（对应原 CmdKey 枚举）
 * 使用 #define 替代 C++ enum class，纯C兼容
 * ============================================================ */

#define GH_CMD_NOP                      (0x00U) /* 无操作 */
#define GH_CMD_OPERATION_ACK            (0x01U) /* 操作响应 */
#define GH_CMD_DEVICE_STATUS_QUERY      (0x02U) /* 设备状态查询 */
#define GH_CMD_REGISTER_READ_WRITE      (0x03U) /* 读写寄存器 */
#define GH_CMD_CONFIG_DATA_DOWNLOAD     (0x04U) /* 配置数据下发 */
#define GH_CMD_PACKAGE_TEST             (0x05U) /* 数据包测试 */
#define GH_CMD_READ_OTP_REGISTER        (0x07U) /* 读OTP寄存器 */
#define GH_CMD_RAWDATA_PACKET           (0x08U) /* 原始数据包（上行）*/
#define GH_CMD_COMPRESSED_EVEN          (0x09U) /* 压缩偶数帧（上行）*/
#define GH_CMD_COMPRESSED_ODD           (0x0AU) /* 压缩奇数帧（上行）*/
#define GH_CMD_NEW_PROTOCOL_RAWDATA     (0x0BU) /* 新协议原始数据包 */
#define GH_CMD_START_HBD                (0x0CU) /* 启动HBD */
#define GH_CMD_SLAVE_WORK_MODE          (0x10U) /* 工作模式设置 */
#define GH_CMD_GSENSOR_SETTING          (0x11U) /* G-sensor设置 */
#define GH_CMD_FIFO_THRESHOLD           (0x12U) /* FIFO阈值设置 */
#define GH_CMD_EVENT_SETTING            (0x13U) /* Cardiff事件设置 */
#define GH_CMD_ECG_PATHOLOGY            (0x14U) /* ECG病理上报 */
#define GH_CMD_FUNC_CHANNEL_MAP         (0x15U) /* 功能通道映射 */
#define GH_CMD_CARDIFF_EVENT_REPORT     (0x16U) /* Cardiff事件上报 */
#define GH_CMD_CARDIFF_CONTROL          (0x17U) /* Cardiff控制 */
#define GH_CMD_GET_EVK_VERSION          (0x19U) /* 获取EVK版本 */
#define GH_CMD_QUERY_CONNECTION         (0x1AU) /* 查询连接状态 */
#define GH_CMD_SET_SAMPLING_RATE        (0x1BU) /* 设置采样率 */
#define GH_CMD_SLOT_EN_SWITCH           (0x1CU) /* SlotEn切换 */
#define GH_CMD_ECG_CONTROL              (0x1DU) /* ECG控制设定 */
#define GH_CMD_DRIVER_CONFIG_DOWNLOAD   (0x1FU) /* 驱动配置下发 */
#define GH_CMD_APP_MODULE               (0x20U) /* 应用模块命令 */
#define GH_CMD_SLAVE_LOG                (0x21U) /* 从机日志 */
#define GH_CMD_DUMP_MODE                (0x23U) /* Dump模式设置 */
#define GH_CMD_SOFT_DIMMING_GAIN        (0x24U) /* 软件调光增益 */
#define GH_CMD_GET_SAMPLING_STATUS      (0x25U) /* 获取采样状态 */
#define GH_CMD_SET_SLAVE_RTC            (0x26U) /* 设置下位机RTC */
#define GH_CMD_HSM_RESULT_UPDATE        (0x29U) /* HSM结果更新 */
#define GH_CMD_RAWDATA_FIFO_UPDATE      (0x2AU) /* 原始数据FIFO更新 */
#define GH_CMD_BP_DATA_TRANSMIT         (0x2BU) /* 血压数据传输 */
#define GH_CMD_FUNCTION_INFO_UPDATE     (0x2CU) /* 功能信息更新 */
#define GH_CMD_GET_MAX_PACKET_LEN       (0xA0U) /* 获取最大包长度 */
#define GH_CMD_VIRTUAL_REGISTER_PKT     (0xA1U) /* 虚拟寄存器包 */
#define GH_CMD_READ_DEBUG_STATUS        (0xA2U) /* 读取调试状态 */

/* ============================================================
 * 数据结构定义
 * ============================================================ */

/**
 * @brief 寄存器操作结构（对应原 STGh3x2xReg）
 */
typedef struct {
    uint16_t addr;          /* 寄存器地址 */
    uint16_t data;          /* 寄存器数据 */
} gh_reg_t;

/**
 * @brief 协议包上行数据帧头（对应原 STGh2x2xPackPakcageHeader）
 * 由 ParseCommand 中从 payload 解析
 */
typedef struct {
    uint8_t need_continue;          /* 是否需要继续接收 */
    uint8_t func_id;                /* 功能ID（PPG=0, HR=1, ECG=5...）*/
    uint8_t tag_array[GH_CHANNEL_MAX]; /* 通道标签数组 */
    uint8_t gs_enable;              /* G-sensor使能 */
    uint8_t algo_res_flag;          /* 算法结果使能 */
    uint8_t agc_enable;             /* AGC数据使能 */
    uint8_t amb_enable;             /* 环境光使能 */
    uint8_t gs_gyro_enable;         /* 陀螺仪使能 */
    uint8_t cap_enable;             /* 电容使能 */
    uint8_t temp_enable;            /* 温度使能 */
    uint8_t zip_enable_flag;        /* 数据压缩使能 */
    uint8_t odd_even_change_flag;   /* 奇偶帧标志 */
    uint8_t fifo_package_mode_flag; /* FIFO打包模式标志 */
    uint8_t fifo_package_mode;      /* FIFO打包模式值 */
    uint8_t rawdata_len;            /* 原始数据长度 */
    uint8_t package_cnt;            /* 分片包计数 */
    uint8_t package_over;           /* 分片包结束标志 */
} gh_pack_header_t;

/**
 * @brief 算法结果结构
 */
typedef struct {
    int32_t  results[8];  /* 算法输出值 */
    uint8_t  result_num;  /* 有效结果数量 */
    uint8_t  update_flag; /* 是否有更新 */
    uint16_t result_bit;  /* 结果有效位掩码 */
} gh_algo_result_t;

/**
 * @brief 电容数据结构
 */
typedef struct {
    uint32_t cap_data[4]; /* 电容数据（4个通道）*/
} gh_cap_data_t;

/**
 * @brief 温度数据结构
 */
typedef struct {
    uint32_t temp_data[4]; /* 温度数据（4个通道）*/
} gh_temp_data_t;

/**
 * @brief 一帧解析结果（对应原 STGh3x2xFrameInfoParser）
 * 解析后的完整帧数据，包含各种传感器数据
 */
typedef struct {
    uint32_t func_id;                           /* 功能ID */
    uint32_t frame_cnt;                         /* 帧计数 */
    uint32_t raw_data[GH_CHANNEL_MAX];          /* 原始传感数据（24bit有效）*/
    uint32_t agc_info[GH_CHANNEL_MAX];          /* AGC信息 */
    uint32_t frame_flags[8];                    /* 帧标志位 */
    int16_t  gsensor_data[6];                   /* G-sensor数据（加速+陀螺 各3轴）*/
    gh_cap_data_t  cap_data;                    /* 电容数据 */
    gh_temp_data_t temp_data;                   /* 温度数据 */
    gh_algo_result_t algo_result;               /* 算法结果 */
    uint8_t  chnl_limit;                        /* 通道数限制 */
    uint8_t  chnl_map[GH_CHANNEL_MAX];          /* 通道映射 */
} gh_frame_t;

/**
 * @brief 差分解压历史数据（对应原 STZipLastDataStruct）
 */
typedef struct {
    uint8_t  last_tag[GH_CHANNEL_MAX][GH_CHANNEL_MAX]; /* 各功能各通道上一帧tag */
    uint32_t last_rawdata[GH_CHANNEL_MAX];              /* 上一帧原始数据 */
    uint32_t last_agcdata[GH_CHANNEL_MAX];              /* 上一帧AGC数据 */
} gh_zip_last_data_t;

/**
 * @brief 命令参数结构（对应原 CmdArg union + Param）
 */
typedef struct {
    uint8_t  cmd;            /* 命令码 GH_CMD_XXX */
    union {
        uint8_t single_u8;   /* 单字节参数 */
        uint16_t single_u16; /* 双字节参数 */
        
        struct {
            uint8_t  ctrl_bit;   /* 控制位 */
            uint8_t  mode;       /* 模式 */
            uint32_t func_mask;  /* 功能掩码 */
        } ctrl_mode_func;        /* 用于 StartHbd / SlaveWorkMode 等 */
        
        struct {
            uint8_t  op_mode;    /* 0=读, 1=写, 2=写MAC */
            uint8_t  reg_count;  /* 寄存器个数 */
            uint16_t reg_addr;   /* 起始地址 */
            const gh_reg_t* regs; /* 寄存器数组指针（写操作）*/
        } reg_oper;              /* 用于 RegisterReadWrite / ConfigDataDownload */

        struct {
            uint8_t  first_u8;
            uint16_t second_u16;
        } double_param;          /* 双参数命令 */
    } args;
} gh_cmd_param_t;

/**
 * @brief 函数信息解析器（对应原 STGh3x2xFunctionInfoParser）
 */
typedef struct {
    uint8_t chnl_limit;              /* 最大通道数 */
    uint8_t chnl_map[GH_CHANNEL_MAX]; /* 通道映射 */
} gh_func_info_t;

/**
 * @brief 协议解析器上下文（对应原 GhZipParser 类成员变量）
 * 替换 C++ 类的成员状态，通过指针传递
 */
typedef struct {
    gh_zip_last_data_t  zip_last_data;         /* 差分解压历史数据 */
    uint8_t             current_func_id;       /* 当前功能ID */
    uint32_t            g_current_func_id;     /* 全局当前功能ID */
    uint32_t            upload_status;         /* 上传状态 */
    uint8_t             odd_even_flag;         /* 奇偶帧状态 */
    uint8_t             package_id;            /* 包ID */
    uint8_t             out_packet_buf[GH_UPROTOCOL_PACKET_MAX + 10]; /* 输出缓冲 */

    /* 功能信息表（静态大小，最多64个功能ID） */
    gh_func_info_t      func_info_map[64];     /* 按 func_id 索引 */
    uint64_t            func_info_valid_mask;  /* 有效功能ID位掩码 */

    /* 回调：数据送出 */
    void (*send_cb)(const uint8_t* data, uint16_t len, void* user_ctx);
    void* send_ctx;

    /* 回调：帧解析完成 */
    void (*on_frame_parsed)(const gh_frame_t* frame, void* user_ctx);
    void* frame_ctx;
} gh_parser_ctx_t;

/* ============================================================
 * 接口函数声明
 * ============================================================ */

/**
 * @brief 初始化协议解析器上下文
 * @param ctx    解析器上下文指针
 * @param send_cb 发送函数回调（用于将打包好的命令发给设备）
 * @param send_ctx 回调用户数据
 * @param on_frame 收到完整帧时回调
 * @param frame_ctx 回调用户数据
 */
void gh_parser_init(gh_parser_ctx_t* ctx,
                    void (*send_cb)(const uint8_t*, uint16_t, void*), void* send_ctx,
                    void (*on_frame)(const gh_frame_t*, void*), void* frame_ctx);

/**
 * @brief 重置解析器状态（清空历史数据）
 * @param ctx 解析器上下文
 */
void gh_parser_reset(gh_parser_ctx_t* ctx);

/**
 * @brief 解析一个完整的协议包（去掉AA11帧头后的内层包）
 * @param ctx    解析器上下文
 * @param packet 原始字节数据指针
 * @param length 数据长度
 * @return true=解析成功, false=失败或不识别
 */
bool gh_parse_packet(gh_parser_ctx_t* ctx, const uint8_t* packet, uint16_t length);

/**
 * @brief 协议打包（下行命令构造）
 * @param cmd        命令码 GH_CMD_XXX
 * @param payload    载荷数据（可为NULL）
 * @param payload_len 载荷长度
 * @param out_buf    输出缓冲区（至少 GH_UPROTOCOL_PACKET_MAX 字节）
 * @param out_len    输出实际长度
 * @return true=成功, false=失败（超长等）
 */
bool gh_packet_format(uint8_t cmd, const uint8_t* payload, uint8_t payload_len,
                      uint8_t* out_buf, uint8_t* out_len);

/**
 * @brief 计算 CRC8（使用内置查找表）
 * @param data   数据指针
 * @param length 数据长度
 * @return CRC8 校验值
 */
uint8_t gh_calc_crc8(const uint8_t* data, uint8_t length);

/**
 * @brief 发送 StartHBD 命令（启动/停止采样）
 * @param ctx       解析器上下文
 * @param ctrl_bit  控制位（0=开启, 1=关闭）
 * @param mode      模式（0=EVK, 1=verify）
 * @param func_mask 功能掩码（如 0x01=ADT, 0x02=HR）
 * @return true=发送成功
 */
bool gh_cmd_start_hbd(gh_parser_ctx_t* ctx,
                      uint8_t ctrl_bit, uint8_t mode, uint32_t func_mask);

/**
 * @brief 发送寄存器读写命令
 * @param ctx       解析器上下文
 * @param param     命令参数
 * @return true=发送成功
 */
bool gh_cmd_register_rw(gh_parser_ctx_t* ctx, const gh_cmd_param_t* param);

/**
 * @brief 发送配置数据下发命令
 * @param ctx       解析器上下文
 * @param regs      寄存器数组
 * @param count     寄存器数量
 * @return true=发送成功
 */
bool gh_cmd_config_download(gh_parser_ctx_t* ctx,
                             const gh_reg_t* regs, uint8_t count);

/**
 * @brief 发送工作模式设置命令
 * @param ctx       解析器上下文
 * @param mode      工作模式
 * @param func_mask 功能掩码
 * @return true=发送成功
 */
bool gh_cmd_set_work_mode(gh_parser_ctx_t* ctx, uint8_t mode, uint32_t func_mask);

/**
 * @brief 发送 Cardiff 控制命令
 * @param ctx       解析器上下文
 * @param ctrl_val  控制值
 * @return true=发送成功
 */
bool gh_cmd_cardiff_control(gh_parser_ctx_t* ctx, uint8_t ctrl_val);

/**
 * @brief 发送 Cardiff 事件上报命令
 * @param ctx       解析器上下文
 * @param event_val 事件值
 * @return true=发送成功
 */
bool gh_cmd_cardiff_event_report(gh_parser_ctx_t* ctx, uint16_t event_val);

/**
 * @brief 发送获取EVK版本命令
 * @param ctx       解析器上下文
 * @param type      版本类型
 * @return true=发送成功
 */
bool gh_cmd_get_evk_version(gh_parser_ctx_t* ctx, uint8_t type);

/**
 * @brief 解析功能信息更新包（0x2C命令，存储通道映射信息）
 * @param ctx     解析器上下文
 * @param payload 载荷数据
 * @param length  载荷长度
 * @return true=解析成功
 */
bool gh_parse_func_info(gh_parser_ctx_t* ctx,
                        const uint8_t* payload, uint8_t length);

/**
 * @brief 解压差分原始数据
 * @param ctx            解析器上下文
 * @param zip_data       压缩数据数组
 * @param chnl_cnt       通道数
 * @param raw_data_out   输出原始数据
 * @param func_id        功能ID（用于取历史数据）
 * @return true=成功
 */
bool gh_decompress_rawdata(gh_parser_ctx_t* ctx,
                            const uint8_t* zip_data, uint8_t chnl_cnt,
                            uint32_t* raw_data_out, uint8_t func_id);

#ifdef __cplusplus
}
#endif

#endif /* GH_PROTOCOL_H */
