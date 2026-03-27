/**
  ****************************************************************************************
  * @file    gh_data_fusion_collector.h
  * @author  GHealth Driver Team
  * @brief   ghealth data collector
  ****************************************************************************************
  * @attention
  #####Copyright (c) 2024 GOODIX
   All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of GOODIX nor the names of its contributors may be used
    to endorse or promote products derived from this software without
    specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS AND CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

  ****************************************************************************************
  */

#ifndef __GH_DATA_COMMON_H__
#define __GH_DATA_COMMON_H__

#include <stdint.h>
#include "gh_hal_service.h"
#include "gh_global_config.h"
#include "gh_pragma_pack.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define GH_FUNC_NAME_LEN                   (25)

/**
 * @brief GH agc info struct
 */
GH_PACK_BEGIN
typedef struct
{
    uint32_t gain_code : 4;
    uint32_t bg_cancel_range : 2;
    uint32_t dc_cancel_range : 2;
    uint32_t dc_cancel_code : 8;
    uint32_t led_drv0 : 8;
    uint32_t led_drv1 : 8;
    uint32_t bg_cancel_code : 8;
    uint32_t tia_gain : 3;
    uint32_t resveresd : 5;
} GH_PACKED gh_agc_info_t;
GH_PACK_END

/**
 * @brief GH data flag, need clear
 */
GH_PACK_BEGIN
typedef struct
{
    uint8_t led_adj_flag : 1;
    uint8_t sa_flag : 1;
    uint8_t param_change_flag : 1;
    uint8_t dre_update : 1;
    uint8_t skip_ok_flag : 1;
    uint8_t resveresd : 3;
} GH_PACKED gh_frame_data_flag_t;
GH_PACK_END

/**
 * @brief GH frame data struct
 */
GH_PACK_BEGIN
typedef struct
{
    int32_t ipd_pa;
    int32_t rawdata;
    gh_frame_data_flag_t flag;
    gh_agc_info_t agc_info;
} GH_PACKED gh_frame_data_t;
GH_PACK_END

/**
 * @brief GH function fix id
 */
typedef enum
{
    GH_FUNC_FIX_IDX_ADT = 0,
    GH_FUNC_FIX_IDX_HR = 1,
    GH_FUNC_FIX_IDX_SPO2 = 2,
    GH_FUNC_FIX_IDX_HRV = 3,
    GH_FUNC_FIX_IDX_GNADT = 4,
    GH_FUNC_FIX_IDX_IRNADT = 5,
    GH_FUNC_FIX_IDX_ALGO_MAX = 6,

    GH_FUNC_FIX_IDX_TEST1 = 6,
    GH_FUNC_FIX_IDX_TEST2 = 7,
    GH_FUNC_FIX_IDX_PPG_CFG0 = 8,
    GH_FUNC_FIX_IDX_PPG_CFG1 = 9,
    GH_FUNC_FIX_IDX_PPG_CFG2 = 10,
    GH_FUNC_FIX_IDX_PPG_CFG3 = 11,
    GH_FUNC_FIX_IDX_PPG_CFG4 = 12,
    GH_FUNC_FIX_IDX_PPG_CFG5 = 13,
    GH_FUNC_FIX_IDX_PPG_CFG6 = 14,
    GH_FUNC_FIX_IDX_PPG_CFG7 = 15,
    GH_FUNC_FIX_IDX_CAP_CFG = 16,
    GH_FUNC_FIX_IDX_MAX
} gh_func_fix_idx_e;

/**
 * @brief GH function fix id
 */
typedef enum
{
    GH_FUNC_FIX_ADT = 1 << GH_FUNC_FIX_IDX_ADT,
    GH_FUNC_FIX_HR = 1 << GH_FUNC_FIX_IDX_HR,
    GH_FUNC_FIX_SPO2 = 1 << GH_FUNC_FIX_IDX_SPO2,
    GH_FUNC_FIX_HRV = 1 << GH_FUNC_FIX_IDX_HRV,
    GH_FUNC_FIX_GNADT = 1 << GH_FUNC_FIX_IDX_GNADT,
    GH_FUNC_FIX_IRNADT = 1 << GH_FUNC_FIX_IDX_IRNADT,
    GH_FUNC_FIX_TEST1 = 1 << GH_FUNC_FIX_IDX_TEST1,
    GH_FUNC_FIX_TEST2 = 1 << GH_FUNC_FIX_IDX_TEST2,
    GH_FUNC_FIX_PPG_CFG0 = 1 << GH_FUNC_FIX_IDX_PPG_CFG0,
    GH_FUNC_FIX_PPG_CFG1 = 1 << GH_FUNC_FIX_IDX_PPG_CFG1,
    GH_FUNC_FIX_PPG_CFG2 = 1 << GH_FUNC_FIX_IDX_PPG_CFG2,
    GH_FUNC_FIX_PPG_CFG3 = 1 << GH_FUNC_FIX_IDX_PPG_CFG3,
    GH_FUNC_FIX_PPG_CFG4 = 1 << GH_FUNC_FIX_IDX_PPG_CFG4,
    GH_FUNC_FIX_PPG_CFG5 = 1 << GH_FUNC_FIX_IDX_PPG_CFG5,
    GH_FUNC_FIX_PPG_CFG6 = 1 << GH_FUNC_FIX_IDX_PPG_CFG6,
    GH_FUNC_FIX_PPG_CFG7 = 1 << GH_FUNC_FIX_IDX_PPG_CFG7,
    GH_FUNC_FIX_CAP_CFG = 1 << GH_FUNC_FIX_IDX_CAP_CFG,
} gh_func_fix_id_e;

/**
 * @brief GH gsensor data
 */
typedef struct
{
    int16_t acc[GH_ACC_AXIS_NUM];
#if GH_GYRO_EN
    int16_t gyro[GH_GYRO_AXIS_NUM];
#endif
} gh_gsensor_data_t;

/**
 * @brief GH gsensor data struct
 */
typedef struct
{
    uint64_t timestamp;
    gh_gsensor_data_t data;
} gh_gsensor_ts_and_data_t;

/**
 * @brief GH frame struct
 */
typedef struct
{
    uint32_t frame_cnt;
    uint64_t timestamp;
#if GH_GSENSOR_DEBUG_EN
    uint64_t gs_left_ts;
    uint64_t gs_right_ts;
#endif
    gh_gsensor_data_t gsensor_data;
    gh_func_fix_idx_e id;
    gh_hal_data_channel_t *p_ch_map;
    gh_frame_data_t *p_data;
    uint8_t ch_num;
    uint8_t ch_max;
    uint8_t gsensor_en;
    uint8_t fifo_end_flag;
    uint8_t led_drv_fs[GH_LED_DRV_NUM];
    void *p_algo_res;
} gh_func_frame_t;

/**
 * @brief GH frame publish function
 */
typedef uint32_t (*gh_frame_publish_t)(void *p_parent_node, gh_func_frame_t *frame);

/**
 * @brief GH data fusion collector control enum
 */
typedef enum
{
    GH_DATA_COLLECTOR_CTRL_DISABLE = 0,
    GH_DATA_COLLECTOR_CTRL_ENABLE,
} gh_data_collector_ctrl_e;

/**
 * @brief GH data fusion collector state enum
 */
typedef enum
{
    GH_DATA_COLLECTOR_STATE_DISABLE = 0,
    GH_DATA_COLLECTOR_STATE_WATT_NEW_FIFO_MSG,   //it is state for collector state from close became open
    GH_DATA_COLLECTOR_STATE_WAIT_GH_DATA,
    GH_DATA_COLLECTOR_STATE_WAIT_GS_DATA,
} gh_data_collector_state_e;

/**
 * @brief GH data fusion collector struct
 */
typedef struct gh_data_collector_struct gh_data_collector_t;
struct gh_data_collector_struct
{
    void *p_parent_node;
    gh_data_collector_state_e state;

    //channel0 timestamp of this fifo read period, if there is no channel0 in this fifo read period,
    //this timestamp is last channel0 tiamestamp of last fifo reed period
    uint64_t ghealth_first_chnl0_timestamp;
    uint32_t timestamp_step; //timestamp step of every frame
    uint16_t ghealth_buf_read_index;
    uint16_t gsensor_left_point_valid_flag;
    gh_gsensor_ts_and_data_t gsensor_left_point_data;
    uint32_t chnl_collect_flag;
    gh_func_frame_t *frame;
    gh_frame_publish_t frame_publish;

    //search start gsensor buffer offset for every ghealth frame
    uint16_t gsensor_search_offset;
    uint8_t chnl0_sample_cnt;
};

// /**
//  * @brief extern function type text
//  */
// extern const int8_t g_function_name[GH_FUNC_FIX_IDX_MAX][GH_FUNC_NAME_LEN];

// /**
//  * @brief extern data type text
//  */
// extern const int8_t g_data_type_name[GH_FUNC_FIX_IDX_MAX][GH_FUNC_NAME_LEN];

#ifdef __cplusplus
}
#endif

#endif // __GH_DATA_COMMON_H__

