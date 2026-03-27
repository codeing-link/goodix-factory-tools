/**
 ****************************************************************************************
 *
 * @file    gh_algo_adapter_common.h
 * @author  GOODIX GH Driver Team
 * @brief   Header file containing algorithm adapter common struct.
 *
 ****************************************************************************************
 * @attention
  #####Copyright (c) 2019 GOODIX
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

/** @addtogroup GH ALGO ADAPTER COMMON
  * @{
  */

/** @defgroup API
  * @brief ALGO API COMMON.
  * @{
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GH_ALGO_ADAPTER_COMMON_H__
#define __GH_ALGO_ADAPTER_COMMON_H__

#include <stdint.h>
// #if (GH_USE_GOODIX_ADT_ALGO)
// #include "gh_wear_detector.h"
// #endif
// #if (GH_USE_GOODIX_HR_ALGO)
// #include "goodix_hba.h"
// #endif
// #if (GH_USE_GOODIX_HRV_ALGO)
// #include "goodix_hrv.h"
// #endif
// #if (GH_USE_GOODIX_NADT_ALGO)
// #include "goodix_nadt.h"
// #endif
// #if (GH_USE_GOODIX_SPO2_ALGO)
// #include "goodix_spo2.h"
// #endif

#ifdef __cplusplus
extern "C"
{
#endif

#define GH_ALGO_CHL_MAP_DEFAULT     0xFF
#define GH_ALGO_ADC_DATA_ONLY       0
#define GH_ALGO_ADC_IPD_DATA        1
#define GH_ALGO_DATA_TYPE           GH_ALGO_ADC_IPD_DATA
#define GH_ALGO_CHIP_3018           0
#define GH_ALGO_CHIP_3X2X           1
#define GH_ALGO_CHIP_33XX           2
#define GH_ALGO_CHIP_3036           3
#define GH_ALGO_CHIP_TYPE           GH_ALGO_CHIP_3036
#define GH_GSENSOR_X_INDEX          0
#define GH_GSENSOR_Y_INDEX          1
#define GH_GSENSOR_Z_INDEX          2
#define GH_ALGO_DATA_BIT_NUM        24
#define GH_ALGO_NADT_GREEN_FLAG     0
#define GH_ALGO_NADT_IR_FLAG        1
#define GH_HRV_RRI_NUM              4
#define GH_MAX(X, Y)                (((X) > (Y)) ? (X) : (Y))
#define GH_ALGO_PPG_CHL1            1
#define GH_ALGO_PPG_CHL2            2
#define GH_ALGO_PPG_CHL3            3
#define GH_ALGO_PPG_CHL4            4
#define GH_ALGO_PPG_CHL_NUM         GH_MAX(GH_HR_ALGO_SUPPORT_CHNL_MAX, GH_SPO2_ALGO_SUPPORT_CHNL_MAX)
#define GH_ALGO_SPO2_OUT_COEF       10000
#define GH_ALGO_DEFAULT_GAIN        (0xFF)
#define GH_ALGO_DEFAULT_CUR         (0xFF)
#define GH_ALGO_BYTE_BITS           (8)
#define GH_ALGO_EN_FLAG_OFFSET      (7)
#define GH_ALGO_EN_FLAG_MAX         ((GH_ALGO_PPG_CHL_NUM * 3 / GH_ALGO_BYTE_BITS) \
                                    + (((GH_ALGO_PPG_CHL_NUM * 3) % GH_ALGO_BYTE_BITS) ? 1 : 0))

#define GH_ALGO_HRV_RRI_NUM0        0
#define GH_ALGO_HRV_RRI_NUM1        1
#define GH_ALGO_HRV_RRI_NUM2        2
#define GH_ALGO_HRV_RRI_NUM3        3

#define GH_ALGO_NADT_RES0_MASK(X)    ((X) & 0x3)

/**
  * @brief GH ALGO nadt result0.
  */
typedef enum
{
    GH_ALGO_NADT_RES0_NORMAL = 0,       // normal
    GH_ALGO_NADT_RES0_WEAR_ON = 1,      // wear on
    GH_ALGO_NADT_RES0_WEAR_OFF = 2,     // wear off
    GH_ALGO_NADT_RES0_NON_LINVING = 3,  // non-living
} gh_algo_nadt_res0_e;

#define GH_ALGO_NADT_RES1_MASK(X)    (((X) >> 2) & 0x01)

/**
  * @brief GH ALGO nadt result1.
  */
typedef enum
{
    GH_ALGO_NADT_RES1_NORMAL = 0,       // normal
    GH_ALGO_NADT_RES1_SUS_OFF = 1,      // suspected of wear off
} gh_algo_nadt_res1_e;

/**
  * @brief GH ALGO hrv parameters.
  */
typedef struct
{
    int32_t *p_hr;
} gh_algo_hrv_t;

/**
  * @brief GH ALGO version function.
  */
typedef int32_t (*gh_goodix_algo_version_t)(uint8_t version[150]);

/**
  * @brief GH ALGO configuration get function.
  */
typedef const void *(*gh_goodix_algo_cfg_get)(void);

/**
  * @brief GH ALGO configuration version get function.
  */
typedef void (*gh_goodix_algo_cfg_ver_get)(int8_t* ver, uint8_t ver_len);

/**
  * @brief GH ALGO init function.
  */
typedef int32_t (*gh_goodix_algo_init)(const void *cfg_instance, uint32_t cfg_size, const int8_t *interface_ver);

/**
  * @brief GH ALGO deinit function.
  */
typedef int32_t (*gh_goodix_algo_deinit)(void);

/**
  * @brief GH ALGO process function.
  */
typedef int32_t (*gh_goodix_algo_calc)(void *raw, void *res);

/**
  * @brief GH Goodix algorithm interface.
  */
typedef struct
{
    gh_goodix_algo_version_t algo_ver;
    gh_goodix_algo_cfg_get algo_cfg_get;
    gh_goodix_algo_cfg_ver_get algo_cfg_ver_get;
    gh_goodix_algo_init algo_init;
    gh_goodix_algo_deinit algo_deinit;
    gh_goodix_algo_calc algo_calc;
} gh_goodix_algo_inf_t;

typedef enum
{
    GH_ALGO_DRV_CUR0 = 0,
    GH_ALGO_DRV_CUR1 = 1,
    GH_ALGO_DRV_CUR_MAX
} gh_algo_drv_cur_e;

/**
 * @brief GH ALGO return code.
 */
typedef enum
{
    GH_ALGO_OK = 0,
    GH_ALGO_NULL = 1,
    GH_ALGO_INIT_ERR = 2,
    GH_ALGO_DEINIT_ERR = 3,
    GH_ALGO_EXE_ERR = 4,
    GH_ALGO_VER_ERR = 5,
    GH_ALGO_INF_ERR = 6
} gh_algo_ret_e;

/**
 * @brief GH ALGO channel enum.
 */
typedef enum
{
    GH_ALGO_GREEN_CH0 = 0,
#if (GH_ALGO_PPG_CHL_NUM >= GH_ALGO_PPG_CHL2)
    GH_ALGO_GREEN_CH1,
#endif
#if (GH_ALGO_PPG_CHL_NUM >= GH_ALGO_PPG_CHL3)
    GH_ALGO_GREEN_CH2,
#endif
#if (GH_ALGO_PPG_CHL_NUM >= GH_ALGO_PPG_CHL4)
    GH_ALGO_GREEN_CH3,
#endif

    GH_ALGO_IR_CH0,
#if (GH_ALGO_PPG_CHL_NUM >= GH_ALGO_PPG_CHL2)
    GH_ALGO_IR_CH1,
#endif
#if (GH_ALGO_PPG_CHL_NUM >= GH_ALGO_PPG_CHL3)
    GH_ALGO_IR_CH2,
#endif
#if (GH_ALGO_PPG_CHL_NUM >= GH_ALGO_PPG_CHL4)
    GH_ALGO_IR_CH3,
#endif

    GH_ALGO_RED_CH0,
#if (GH_ALGO_PPG_CHL_NUM >= GH_ALGO_PPG_CHL2)
    GH_ALGO_RED_CH1,
#endif
#if (GH_ALGO_PPG_CHL_NUM >= GH_ALGO_PPG_CHL3)
    GH_ALGO_RED_CH2,
#endif
#if (GH_ALGO_PPG_CHL_NUM >= GH_ALGO_PPG_CHL4)
    GH_ALGO_RED_CH3,
#endif

    GH_ALGO_CHL_MAX
} gh_adapter_chl_e;

/**
 * @brief GH ALGO parameter.
 */
typedef struct
{
    uint16_t fs;
    uint8_t chl_num;
    uint8_t chl_map[GH_ALGO_CHL_MAX];
    void *p_algo_inst;
    gh_goodix_algo_inf_t *p_inf;
} gh_func_algo_param_t;

/**
 * @brief wear event id
 */
typedef struct
{
    uint32_t wear_evt;                  // refer to gh_adt_wear_e
    uint32_t det_status;                // refer to gh_adt_det_state_e
    uint32_t ctr;
    uint8_t update;
} gh_algo_adt_result_t;

/**
 * @brief wear detector struct
 */
typedef struct
{
    uint8_t scence;
} gh_algo_hr_t;

/**
 * @brief GH ALGO hr result.
 */
typedef struct
{
    int32_t hba_out_flag;               // Output value flag: 1 is valid
    int32_t hba_out;                    // Heart rate value
    int32_t valid_score;                // Confidence score: 0->100, the larger the value, the more reliable it is

                                        // Signal-to-noise ratio: After filtering to remove interference,
    int32_t hba_snr;                    // the main peak energy of the spectrum is at the total energy
    int32_t valid_level;                // Confidence level: 0 -> 1 -> 2, the larger the more reliable

                                        // Movement status: 0-resting, small movement;
                                        //                  1-walking-medium movement;
    int32_t hba_acc_info;               //                  2-running-large movement;
    int32_t hba_reg_scence;             // Motion scenes: refer to hba_scenes_e enumeration type
    int32_t input_sence;
    int32_t reserved1;
    int32_t reserved2;
    int32_t reserved3;
} gh_algo_hr_result_t;

/**
 * @brief GH ALGO hrv result.
 */
typedef struct
{
    int32_t rri[GH_HRV_RRI_NUM];
    int32_t confidence;
    int32_t valid_num;
    uint8_t update;
    int32_t reserved1;
    int32_t reserved2;
    int32_t reserved3;
} gh_algo_hrv_result_t;

/**
 * @brief GH ALGO nadt result.
 */
typedef struct
{
    int32_t wear_off_detect_res;        // refer to gh_algo_nadt_res0_e
    int32_t live_body_conf;             // Wearing detection confidence
    uint8_t update;
    int32_t reserved1;
    int32_t reserved2;
    int32_t reserved3;
} gh_algo_nadt_result_t;

/**
 * @brief GH ALGO spo2 result.
 */
typedef struct
{
    int32_t final_spo2;
    int32_t r_val;
    int32_t confi_coeff;
    int32_t valid_level;
    int32_t hb_mean;
    int32_t invalid_flag;
    uint8_t update;
    int32_t reserved1;
    int32_t reserved2;
    int32_t reserved3;
} gh_algo_spo2_result_t;

typedef struct
{
    uint32_t frame_id;
    uint8_t  total_ch_num;
    uint8_t *enable_flg;
    int32_t *ppg_rawdata;
    uint8_t *ch_agc_gain;
    uint16_t *ch_agc_drv;
    int32_t acc_x;
    int32_t acc_y;
    int32_t acc_z;
    int32_t groy_x;
    int32_t groy_y;
    int32_t groy_z;
    uint8_t sleep_flg;
    uint8_t bit_num;
    uint8_t chip_type;
    uint8_t data_type;
    void *pst_module_unique;
} gh_algo_common_input_t;

#ifdef __cplusplus
}
#endif

#endif /* __GH_ALGO_ADAPTER_COMMON_H__ */

/** @} */

/** @} */

