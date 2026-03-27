#ifndef __GH_HAL_SERVICE_H__
#define __GH_HAL_SERVICE_H__

#include <stdint.h>
#include "gh_hal_config.h"
#include "gh_hal_chip.h"
#include "gh_pragma_pack.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SLOT_CFG_MAX_NUM         (9)
#define GH_DRV_CODE_FS           (255)

#define GH_GAIN_GEAR             (7) /**< number of gain gear. */
#define GH_RET_HAL_ERR_GET(x)    ((x) & (0xff))

/**
  * @brief errot code
  */
typedef enum
{
    GH_SERVICE_OK = 0,
    GH_SERVICE_INTF_INIT_ERROR = 1,
    GH_SERVICE_SETTINGS_INIT_ERROR = 2,
    GH_SERVICE_CONTROL_INIT_ERROR = 3,
    GH_SERVICE_FIFO_PARSER_INIT_ERROR = 4,
    GH_SERVICE_STD_INIT_ERROR = 5,
    GH_SERVICE_AGC_INIT_ERROR = 6,
} gh_hal_service_ret_e;

/**
 * @brief module id list.
 */
typedef enum
{
    GH_HAL_STD_ID        = 0,
    GH_HAL_AGC_ID        = 1,
    GH_HAL_SETTINGS_ID   = 2,

} gh_hal_id_list_e;

/**
  * @brief hal sample ctrl operation
  */
typedef enum
{
    GH_HAL_SAMPLE_TOP_STOP = 0,
    GH_HAL_SAMPLE_TOP_START = 1,
    GH_HAL_SAMPLE_CHN_EN = 2,
} gh_hal_sample_op_e;

/**
  * @brief hal sample ctrl operation
  */
typedef enum
{
    GH_HAL_CONFIG_RESET = 0,
    GH_HAL_CONFIG_SET = 1,
} gh_hal_config_op_e;

/**
  * @brief reg addr and val
  */
typedef struct
{
    uint16_t addr;
    uint16_t val;
} gh_chelsea_reg_t;

/**
  * @brief slot config
  */
typedef enum
{
    GH_PPG_CFG0      = 0,
    GH_PPG_CFG1      = 1,
    GH_PPG_CFG2      = 2,
    GH_PPG_CFG3      = 3,
    GH_PPG_CFG4      = 4,
    GH_PPG_CFG5      = 5,
    GH_PPG_CFG6      = 6,
    GH_PPG_CFG7      = 7,
    GH_CAP_CFG       = 13,
    GH_CFG_CONTINUE  = 15,
} gh3036_slot_cfg_id_e;

/**
  * @brief data type
  */
typedef enum
{
    GH_PPG_DATA           = 0, // rawdata, use gh_hal_ppg_channel_t
    GH_PPG_MIX_DATA       = 1, // mixdata, use gh_hal_ppg_mix_channel_t
    GH_PPG_BG_DATA        = 2, // bgdata, use gh_hal_ppg_bg_channel_t
    GH_PPG_BG_CANCEL      = 3,
    GH_PPG_LED_DRV        = 4,
    GH_ECG_DATA           = 5,
    GH_BIA_DATA           = 6,
    GH_GSR_DATA           = 7,
    GH_PRESSURE_DATA      = 8,
    GH_TEMPERATURE_DATA   = 9,
    GH_CAP_DATA           = 10, // gh_hal_cap_channel_t
    GH_PPG_PARAM_DATA     = 11, // gh_hal_ppg_channel_t
    GH_PPG_DRE_DATA       = 12, // gh_hal_ppg_channel_t
    GH_PPG_DRE_DC_INFO    = 13, // gh_hal_ppg_channel_t
} gh_data_type_e;

/**
  * @brief ppg channel
  */
GH_PACK_BEGIN
typedef struct
{
    gh_data_type_e data_type   : 5;
    uint8_t        rx_id       : 3;
    uint8_t        slot_cfg_id : 6;
    uint8_t        reserved    : 2;
} GH_PACKED gh_hal_ppg_channel_t;
GH_PACK_END

/**
  * @brief mix data channel
  */
GH_PACK_BEGIN
typedef struct
{
    gh_data_type_e data_type   : 5;
    uint8_t        rx_id       : 3;
    uint8_t        slot_cfg_id : 6;
    uint8_t        mix_id      : 2;
} GH_PACKED gh_hal_ppg_mix_channel_t;
GH_PACK_END

/**
  * @brief bg data channel
  */
GH_PACK_BEGIN
typedef struct
{
    gh_data_type_e data_type   : 5;
    uint8_t        rx_id       : 3;
    uint8_t        slot_cfg_id : 6;
    uint8_t        bg_id       : 2;
} GH_PACKED gh_hal_ppg_bg_channel_t;
GH_PACK_END

/**
  * @brief cap data channel
  */
GH_PACK_BEGIN
typedef struct
{
    gh_data_type_e data_type   : 5;
    uint8_t        reserved0   : 3;
    uint8_t        slot_cfg_id : 6;
    uint8_t        reserved1   : 2;
} GH_PACKED gh_hal_cap_channel_t;
GH_PACK_END

/**
  * @brief data channel
  */
GH_PACK_BEGIN
typedef union
{
    gh_data_type_e           data_type : 5;
    gh_hal_ppg_channel_t     channel_ppg;
    gh_hal_ppg_mix_channel_t channel_ppg_mix;
    gh_hal_ppg_bg_channel_t  channel_ppg_bg;
    gh_hal_ppg_channel_t     channel_ppg_param;
    gh_hal_ppg_channel_t     channel_ppg_dre;
    gh_hal_ppg_channel_t     channel_ppg_dre_dc_info;
    gh_hal_cap_channel_t     channel_cap;
    uint16_t                 channel;
} GH_PACKED gh_hal_data_channel_t;
GH_PACK_END

/**
  * @brief ppg data
  */
GH_PACK_BEGIN
typedef struct
{
    gh_hal_data_channel_t data_channel;                     //data channel
    uint64_t              timestamp_begin;                  //the begin time of FIFO data unit: ms
    uint64_t              timestamp_end;                    //the end time of FIFO data unit: ms
    uint8_t               fifo_end;                         //0: not end, 1: end
    uint16_t              data_cnt;                         //data count
    int32_t               rawdata : 24;                     //raw data
    uint8_t               gain_code : 4;                    //unit: code
    uint8_t               led_adj_flag : 1;                 //0: no adjust, 1: adjust
    uint8_t               sa_flag : 1;                      //0: no sa, 1: sa
    uint8_t               param_change_flag : 1;            //0: no change, 1: change
    uint8_t               dre_update : 1;                   //0: no update, 1: update
    int32_t               ipd_pa;                           //unit: pA
    uint8_t               led_drv[GH_LED_DRV_NUM];          //unit: code
    uint8_t               led_drv_fs[GH_LED_DRV_NUM];       //uint: mA
    uint32_t              reservd : 4;                      //reserved
    uint32_t              dc_cancel_range : 2;              //unit: code
    uint32_t              bg_cancel_range : 2;              //unit: code
    uint32_t              dc_cancel_code : 8;               //unit: code
} GH_PACKED gh_hal_ppg_data_t;
GH_PACK_END

/**
  * @brief ppg bg data
  */
GH_PACK_BEGIN
typedef struct
{
    gh_hal_data_channel_t data_channel;                     //data channel
    uint64_t              timestamp_begin;                  //the begin time of FIFO data unit: ms
    uint64_t              timestamp_end;                    //the end time of FIFO data unit: ms
    uint8_t               fifo_end;                         //0: not end, 1: end
    uint16_t              data_cnt;                         //data count
    int32_t               rawdata : 24;                     //raw data
    uint8_t               gain_code : 4;                    //unit: code
    int32_t               ipd_pa;                           //unit: pA
} GH_PACKED gh_hal_ppg_bg_data_t;
GH_PACK_END

/**
  * @brief ppg param struct
  */
GH_PACK_BEGIN
typedef struct
{
    uint8_t               dc_cancel_range : 2;              //unit: code
    uint8_t               bg_cancel_range : 2;              //unit: code
    uint8_t               gain_code       : 3;              //unit: code
    uint8_t               skip_ok_flag    : 1;              //0: no skip, 1: skip
    uint8_t               dc_cancel_code;                   //unit: code
    uint8_t               bg_cancel_code;                   //unit: code
} GH_PACKED gh_hal_param_rawdata_t;
GH_PACK_END

/**
  * @brief ppg param union
  */
GH_PACK_BEGIN
typedef union
{
    gh_hal_param_rawdata_t param;                           //param
    uint32_t               rawdata : 24;                    //raw data
} GH_PACKED gh_hal_param_rawdata_u;
GH_PACK_END

/**
  * @brief ppg param data
  */
GH_PACK_BEGIN
typedef struct
{
    gh_hal_data_channel_t  data_channel;                    //data channel
    uint64_t               timestamp_begin;                 //the begin time of FIFO data unit: ms
    uint64_t               timestamp_end;                   //the end time of FIFO data unit: ms
    uint8_t                fifo_end;                        //0: not end, 1: end
    uint16_t               data_cnt;                        //data count
    gh_hal_param_rawdata_u param_rawdata;                   //param raw data
} GH_PACKED gh_hal_ppg_param_data_t;
GH_PACK_END

/**
  * @brief cap data
  */
GH_PACK_BEGIN
typedef struct
{
    gh_hal_data_channel_t data_channel;                     //data channel
    uint64_t              timestamp_begin;                  //the begin time of FIFO data unit: ms
    uint64_t              timestamp_end;                    //the end time of FIFO data unit: ms
    uint8_t               fifo_end;                         //0: not end, 1: end
    uint16_t              data_cnt;                         //data count
    int32_t               rawdata : 24;                     //raw data
} GH_PACKED gh_hal_cap_data_t;
GH_PACK_END

/**
  * @brief dre data
  */
GH_PACK_BEGIN
typedef struct
{
    gh_hal_data_channel_t data_channel;                     //data channel
    uint64_t              timestamp_begin;                  //the begin time of FIFO data unit: ms
    uint64_t              timestamp_end;                    //the end time of FIFO data unit: ms
    uint8_t               fifo_end;                         //0: not end, 1: end
    uint16_t              data_cnt;                         //data count
    int32_t               rawdata : 24;                     //raw data
    int8_t                dre_update;                       //0: no update, 1: update
} GH_PACKED gh_hal_ppg_dre_data_t;
GH_PACK_END

/**
  * @brief common data
  */
GH_PACK_BEGIN
typedef struct
{
    gh_hal_data_channel_t data_channel;                     //data channel
    uint64_t              timestamp_begin;                  //the begin time of FIFO data unit: ms
    uint64_t              timestamp_end;                    //the end time of FIFO data unit: ms
    uint8_t               fifo_end;                         //0: not end, 1: end
    uint16_t              data_cnt;                         //data count
    int32_t               rawdata : 24;                     //raw data
} GH_PACKED gh_hal_common_data_t;
GH_PACK_END

/**
  * @brief gh data
  */
typedef union
{
    gh_hal_data_channel_t    data_channel;
    gh_hal_common_data_t     common_data;
    gh_hal_ppg_data_t        ppg_data;
#if GH_SUPPORT_FIFO_CTRL_DEBUG1
    gh_hal_ppg_data_t        ppg_mixdata;
    gh_hal_ppg_bg_data_t     ppg_bg_data;
#endif
#if GH_SUPPORT_FIFO_CTRL_DEBUG0
    gh_hal_ppg_param_data_t  ppg_param_data;
#endif
#if GH_SUPPORT_FIFO_CTRL_DEBUG1
    gh_hal_ppg_dre_data_t    ppg_dre_data;
#endif
    gh_hal_ppg_data_t        ppg_dre_dc_info;

    gh_hal_cap_data_t        cap_data;
} gh_data_t;

/**
  * @brief hal sample ctrl struct
  */
typedef struct
{
    gh_hal_sample_op_e op;
    gh_hal_data_channel_t ch_map;
} gh_hal_sample_ctrl_t;

/**
  * @brief hal config ctrl struct
  */
typedef struct
{
    gh_hal_config_op_e op;
    gh_chelsea_reg_t *p_reg;
    uint16_t number;
} gh_hal_config_ctrl_t;


#ifdef __cplusplus
}
#endif

#endif /* __GH_HAL_SERVICE_H__*/
