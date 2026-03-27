/**
 ****************************************************************************************
 *
 * @file    gh_app_config.h
 * @author  GOODIX GH Driver Team
 * @brief   Header file containing project global config.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GH_GLOBAL_CONFIG_H__
#define __GH_GLOBAL_CONFIG_H__

#include <stdint.h>
#include "gh_hal_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// Chip define
#define GH_CHIP_3018                                    (0)
#define GH_CHIP_3X2X                                    (1)
#define GH_CHIP_33XX                                    (2)
#define GH_CHIP_3036                                    (3)
#define GH_CHIP_TYPE                                    GH_CHIP_3036

/// Functions enable
#ifndef GH_FUNC_ADT_EN
#define GH_FUNC_ADT_EN                                  (1)
#endif

#ifndef GH_FUNC_HR_EN
#define GH_FUNC_HR_EN                                   (1)
#endif

#ifndef GH_FUNC_SPO2_EN
#define GH_FUNC_SPO2_EN                                 (1)
#endif

#ifndef GH_FUNC_HRV_EN
#define GH_FUNC_HRV_EN                                  (1)
#endif

#ifndef GH_FUNC_GNADT_EN
#define GH_FUNC_GNADT_EN                                (1)
#endif

#ifndef GH_FUNC_IRNADT_EN
#define GH_FUNC_IRNADT_EN                                (1)
#endif

#ifndef GH_FUNC_TEST1_EN
#define GH_FUNC_TEST1_EN                                (1)
#endif

#ifndef GH_FUNC_TEST2_EN
#define GH_FUNC_TEST2_EN                                (1)
#endif

#ifndef GH_FUNC_SLOT_EN
#define GH_FUNC_SLOT_EN                                 (1)
#endif

/// Algorithm functions Configuration
#ifndef GH_ALGO_ADAPTER_EN
#define GH_ALGO_ADAPTER_EN                              (1)
#endif

#ifndef GH_ALGO_EN
#define GH_ALGO_EN                                      ((1) & GH_ALGO_ADAPTER_EN)
#endif

#if (1 == GH_ALGO_EN)
#define GH_ALGO_ADT_EN                                  ((1) & GH_FUNC_ADT_EN)
#define GH_ALGO_HR_EN                                   ((1) & GH_FUNC_HR_EN)
#define GH_ALGO_SPO2_EN                                 ((1) & GH_FUNC_SPO2_EN)
#define GH_ALGO_HRV_EN                                  ((1) & GH_FUNC_HRV_EN)
#define GH_ALGO_GNADT_EN                                ((1) & GH_FUNC_GNADT_EN)
#define GH_ALGO_IRNADT_EN                               ((1) & GH_FUNC_IRNADT_EN)
#else
#define GH_ALGO_ADT_EN                                  (0)
#define GH_ALGO_HR_EN                                   (0)
#define GH_ALGO_SPO2_EN                                 (0)
#define GH_ALGO_HRV_EN                                  (0)
#define GH_ALGO_GNADT_EN                                (0)
#define GH_ALGO_IRNADT_EN                               (0)
#endif

/// GOODIX Algorithm Configuration
#if (1 == GH_ALGO_ADT_EN)
#define GH_USE_GOODIX_ADT_ALGO                          (1)
#else
#define GH_USE_GOODIX_ADT_ALGO                          (0)
#endif
#if (1 == GH_ALGO_HR_EN)
#define GH_USE_GOODIX_HR_ALGO                           (1)
#else
#define GH_USE_GOODIX_HR_ALGO                           (0)
#endif
#if (1 == GH_ALGO_HRV_EN)
#define GH_USE_GOODIX_HRV_ALGO                          (1)
#else
#define GH_USE_GOODIX_HRV_ALGO                          (0)
#endif
#if (1 == GH_ALGO_SPO2_EN)
#define GH_USE_GOODIX_SPO2_ALGO                         (1)
#else
#define GH_USE_GOODIX_SPO2_ALGO                         (0)
#endif
#if (1 == GH_ALGO_GNADT_EN) || (1 == GH_ALGO_IRNADT_EN)
#define GH_USE_GOODIX_NADT_ALGO                         (1)
#else
#define GH_USE_GOODIX_NADT_ALGO                         (0)
#endif
#define GH_USE_GOODIX_ECG_ALGO                          (0)
#define GH_USE_GOODIX_HSM_ALGO                          (0)
#define GH_USE_GOODIX_BT_ALGO                           (0)
#define GH_USE_GOODIX_RESP_ALGO                         (0)
#define GH_USE_GOODIX_AF_ALGO                           (0)

/* algoritm channel nunber config*/
#define GH_HR_ALGO_SUPPORT_CHNL_MAX                     (4)  /* range 1~4 */
#define GH_SPO2_ALGO_SUPPORT_CHNL_MAX                   (4)  /* range 1-4 */

#define GH_FUNC_OFFSET_ADT                              (0)
#define GH_FUNC_OFFSET_HR                               (1)
#define GH_FUNC_OFFSET_HRV                              (2)
#define GH_FUNC_OFFSET_SPO2                             (3)
#define GH_FUNC_OFFSET_NADT                             (4)
#define GH_FUNC_OFFSET_ECG                              (5)
#define GH_FUNC_OFFSET_HSM                              (6)
#define GH_FUNC_OFFSET_BT                               (7)
#define GH_FUNC_OFFSET_RESP                             (8)
#define GH_FUNC_OFFSET_AF                               (9)
#define GH_FUNC_OFFSET_MAX                              (10)

#define GH_NO_FUNCTION                                  (0)
#define GH_FUNCTION_ADT                                 ((0x1) << (GH_FUNC_OFFSET_ADT))
#define GH_FUNCTION_HR                                  ((0x1) << (GH_FUNC_OFFSET_HR))
#define GH_FUNCTION_HRV                                 ((0x1) << (GH_FUNC_OFFSET_HRV))
#define GH_FUNCTION_SPO2                                ((0x1) << (GH_FUNC_OFFSET_SPO2))
#define GH_FUNCTION_NADT                                ((0x1) << (GH_FUNC_OFFSET_NADT))
#define GH_FUNCTION_ECG                                 ((0x1) << (GH_FUNC_OFFSET_ECG))
#define GH_FUNCTION_HSM                                 ((0x1) << (GH_FUNC_OFFSET_HSM))
#define GH_FUNCTION_BT                                  ((0x1) << (GH_FUNC_OFFSET_BT))
#define GH_FUNCTION_RESP                                ((0x1) << (GH_FUNC_OFFSET_RESP))
#define GH_FUNCTION_AF                                  ((0x1) << (GH_FUNC_OFFSET_AF))

#define GH_ALGO_FUNCTION_GROUP_0                        (GH_FUNCTION_HR | GH_FUNCTION_SPO2 | GH_FUNCTION_HRV \
                                                        | GH_FUNCTION_NADT)
#define GH_ALGO_FUNCTION_GROUP_1                        (GH_FUNCTION_SPO2)
#define GH_ALGO_FUNCTION_GROUP_2                        (GH_FUNCTION_HRV)
#define GH_ALGO_FUNCTION_GROUP_3                        (GH_NO_FUNCTION)
#define GH_ALGO_FUNCTION_GROUP_4                        (GH_NO_FUNCTION)

#define GOODIX_ALGO_BASIC                               (1)
#define GOODIX_ALGO_MEDIUM                              (2)
#define GOODIX_ALGO_PREMIUM                             (3)
#define GOODIX_ALGO_EXCLUSIVE                           (4)

#define GOODIX_ECG_500FS_QRS_ENABLE                     (1)
#define GOODIX_ECG_500FS_QRS_DISABLE                    (2)
#define GOODIX_ECG_250FS_QRS_ENABLE                     (3)
#define GOODIX_ECG_250FS_QRS_DISABLE                    (4)

#ifndef GOODIX_HR_ALGO_VERISON
#define GOODIX_HR_ALGO_VERISON                          (GOODIX_ALGO_EXCLUSIVE)
#endif
#ifndef GOODIX_SPO2_ALGO_VERISON
#define GOODIX_SPO2_ALGO_VERISON                        (GOODIX_ALGO_EXCLUSIVE)
#endif
#ifndef GOODIX_ECG_ALGO_CONFIG
#define GOODIX_ECG_ALGO_CONFIG                          (GOODIX_ECG_ALGO_250FS_QRS_ENABLE)
#endif

/* 0: global buffer for goodix algorithm mem pool
 * 1: user dynamic buffer for goodix algorithm mem pool */
#ifndef GH_USE_DYNAMIC_ALGO_MEM
#define GH_USE_DYNAMIC_ALGO_MEM                         (0)
#endif

/// END of GOODIX Algorithm config

/// SDK data type define
#define GH_CHIP_PPG_DATA0                               (1)
#if (1 == GH_SUPPORT_FIFO_CTRL_DEBUG1)
#define GH_CHIP_PPG_DATA1                               (1)
#else
#define GH_CHIP_PPG_DATA1                               (0)
#endif
#if (1 == GH_SUPPORT_FIFO_CTRL_DEBUG0)
#define GH_CHIP_PPG_DEBUG0                              (1)
#else
#define GH_CHIP_PPG_DEBUG0                              (0)
#endif
#if (1 == GH_SUPPORT_FIFO_CTRL_DEBUG1)
#define GH_CHIP_PPG_DEBUG1_BGDATA0                      (1)
#define GH_CHIP_PPG_DEBUG1_BGDATA1                      (1)
#define GH_CHIP_PPG_DEBUG1_BGDATA2                      (1)
#define GH_CHIP_PPG_DEBUG2_DRE                          (1)
#else
#define GH_CHIP_PPG_DEBUG1_BGDATA0                      (0)
#define GH_CHIP_PPG_DEBUG1_BGDATA1                      (0)
#define GH_CHIP_PPG_DEBUG1_BGDATA2                      (0)
#define GH_CHIP_PPG_DEBUG2_DRE                          (0)
#endif
#define GH_CHIP_CAP_RAWDATA                             (1)
#if (1 == GH_SUPPORT_FIFO_CTRL_DRE_DC_INFO)
#define GH_CHIP_PPG_DC_INFO                             (1)
#else
#define GH_CHIP_PPG_DC_INFO                             (0)
#endif
#define GH_CHIP_PPG_CONFIG0                             (0)
#define GH_CHIP_PPG_CONFIG1                             (1)
#define GH_CHIP_PPG_CONFIG2                             (2)
#define GH_CHIP_PPG_CONFIG3                             (3)
#define GH_CHIP_PPG_CONFIG4                             (4)
#define GH_CHIP_PPG_CONFIG5                             (5)
#define GH_CHIP_PPG_CONFIG6                             (6)
#define GH_CHIP_PPG_CONFIG7                             (7)
#define GH_CHIP_PPG_CONFIG_NUM                          (8)
#define GH_CHIP_CAP_CONFIG0                             (0)
#define GH_CHIP_CAP_CONFIG_NUM                          (1)
#define GH_CHIP_CONFIG_NUM                              (GH_CHIP_PPG_CONFIG_NUM + GH_CHIP_CAP_CONFIG_NUM)
#define GH_CHIP_PPG_CONFIG_RX_MIN                       (1)
#define GH_CHIP_PPG_CONFIG_RX_NUM                       (2)
#define GH_CHIP_CAP_CONFIG_RX_NUM                       (1)
#define GH_CHIP_RX_NUM                                  (GH_CHIP_PPG_CONFIG_NUM * GH_CHIP_PPG_CONFIG_RX_NUM \
                                                        + GH_CHIP_CAP_CONFIG_NUM * GH_CHIP_CAP_CONFIG_RX_NUM)
#define GH_CHIP_PPG_DATA_NUM                            (GH_CHIP_PPG_DATA0 + GH_CHIP_PPG_DATA1 \
                                                        + GH_CHIP_PPG_DEBUG0 + GH_CHIP_PPG_DEBUG1_BGDATA0 \
                                                        + GH_CHIP_PPG_DEBUG1_BGDATA1 + GH_CHIP_PPG_DEBUG1_BGDATA2 \
                                                        + GH_CHIP_PPG_DEBUG2_DRE + GH_CHIP_PPG_DC_INFO)
#define GH_CHIP_CAP_DATA_NUM                            (GH_CHIP_CAP_RAWDATA)
#define GH_CHIP_DATA_NUM                                ((GH_CHIP_PPG_DATA_NUM) + (GH_CHIP_CAP_DATA_NUM))

#define GH_ADT_CONFIG_MAX                               (GH_CHIP_PPG_CONFIG_NUM) // (GH_CHIP_PPG_CONFIG1)
#define GH_HR_CONFIG_MAX                                (GH_CHIP_PPG_CONFIG_NUM) // (GH_CHIP_PPG_CONFIG1)
#define GH_SPO2_CONFIG_MAX                              (GH_CHIP_PPG_CONFIG_NUM) // (GH_CHIP_PPG_CONFIG2)
#define GH_HRV_CONFIG_MAX                               (GH_CHIP_PPG_CONFIG_NUM) // (GH_CHIP_PPG_CONFIG1)
#define GH_NADT_CONFIG_MAX                              (GH_CHIP_PPG_CONFIG_NUM) // (GH_CHIP_PPG_CONFIG3)
#define GH_TEST_CONFIG_MAX                              (GH_CHIP_PPG_CONFIG_NUM) // (GH_CHIP_PPG_CONFIG0)

#define GH_ADT_CONFIG_RX_MAX                            (GH_CHIP_PPG_CONFIG_RX_NUM) // (GH_CHIP_PPG_CONFIG_RX_MIN)
#define GH_HR_CONFIG_RX_MAX                             (GH_CHIP_PPG_CONFIG_RX_NUM) // (GH_CHIP_PPG_CONFIG_RX_MIN)
#define GH_SPO2_CONFIG_RX_MAX                           (GH_CHIP_PPG_CONFIG_RX_NUM) // (GH_CHIP_PPG_CONFIG_RX_MIN)
#define GH_HRV_CONFIG_RX_MAX                            (GH_CHIP_PPG_CONFIG_RX_NUM) // (GH_CHIP_PPG_CONFIG_RX_MIN)
#define GH_NADT_CONFIG_RX_MAX                           (GH_CHIP_PPG_CONFIG_RX_NUM) // (GH_CHIP_PPG_CONFIG_RX_MIN)
#define GH_TEST_CONFIG_RX_MAX                           (GH_CHIP_PPG_CONFIG_RX_NUM) // (GH_CHIP_PPG_CONFIG_RX_NUM)

#define GH_ADT_DATA_TYPE                                (GH_CHIP_DATA_NUM)
#define GH_HR_DATA_TYPE                                 (GH_CHIP_DATA_NUM)
#define GH_SPO2_DATA_TYPE                               (GH_CHIP_DATA_NUM)
#define GH_HRV_DATA_TYPE                                (GH_CHIP_DATA_NUM)
#define GH_NADT_DATA_TYPE                               (GH_CHIP_DATA_NUM)
#define GH_TEST_DATA_TYPE                               (GH_CHIP_DATA_NUM)

#define GH_PPG_SLOT_CONFIG_MAX                          (GH_CHIP_PPG_CONFIG1)
#define GH_PPG_SLOT_CONFIG_RX_MAX                       (GH_CHIP_PPG_CONFIG_RX_NUM)
#define GH_PPG_SLOT_DATA_TYPE                           (GH_CHIP_PPG_DATA_NUM)

#define GH_CAP_SLOT_CONFIG_MAX                          (GH_CHIP_PPG_CONFIG1)
#define GH_CAP_SLOT_CONFIG_RX_MAX                       (GH_CHIP_CAP_CONFIG_RX_NUM)
#define GH_CAP_SLOT_DATA_TYPE                           (GH_CHIP_CAP_DATA_NUM)

/// fusion mode
#define GH_FUSION_MODE_NONE                             (0)
#define GH_FUSION_MODE_SYNC                             (1)
#define GH_FUSION_MODE_ASYNC                            (2)

#ifndef GH_FUSION_MODE_SEL
#define GH_FUSION_MODE_SEL                              (GH_FUSION_MODE_ASYNC)
#endif

/// fusion config
#if (GH_FUSION_MODE_SEL == GH_FUSION_MODE_ASYNC)
#define GH_GSENSOR_FUSION_EN                            (1) // ghealth data will fusion with gsensor(1) or not(0)
#define GH_RAWDATA_CAP_PRO_EN                           (1) // cap pack will be processed(1) or not(0)
#endif

/// multi-sensor strategy enable
#ifndef GH_MTSS_EN
#define GH_MTSS_EN                                      (1)
#endif

// MTSS interval max time between gs move event and ghealth wear on event allowed
#if (GH_MTSS_EN)
#define GH_MOVE_GH_WEAR_ON_ITVAL_TIME                   3000
#endif

/// Accelerometer Configuration
#define GH_GYRO_EN                                      (1)
#define GH_ACCX_IDX                                     (0)
#define GH_ACCY_IDX                                     (1)
#define GH_ACCZ_IDX                                     (2)
#define GH_ACC_AXIS_NUM                                 (3)
#define GH_GYRX_IDX                                     (0)
#define GH_GYRY_IDX                                     (1)
#define GH_GYRZ_IDX                                     (2)
#define GH_GYRO_AXIS_NUM                                (3)

/// g-sensor move detect enable
#ifndef GH_GS_MOVE_DET_EN
#define GH_GS_MOVE_DET_EN                               (1)
#endif

/// g-sensor move detect parameters
#if (GH_GS_MOVE_DET_EN)
#define GH_MOVE_DET_GS_SAMPLE_PERIOD_MS                 (40)
#define GH_GS_TS_INTERVAL_TOO_BIG_MS                    (1000)
#define GH_GS_DIFF_THRD                                 ((uint32_t)65 * 65) // diff_x^2 + diff_y^2 + diff_z^2
#define GH_GS_MOVE_CNT_THRD                             (2)
#define GH_GS_NOT_MOVE_CNT_THRD                         (40)
#define GH_GS_MOVE_TIME_OUT_THRD                        (5000) // unit: millisecond
#endif

/// fusion ghleath data max config,
/// refer to the waterline
#ifndef GH_FIFO_READ_MAX_NUM
#define GH_FIFO_READ_MAX_NUM                            (255) // 1 ~ 255
#endif

/// async fusion gsensor data max config,
/// refer to the period of ghleath data reading
#ifndef GH_ASYNC_GSENSOR_QUEUE_DEPTH
#define GH_ASYNC_GSENSOR_QUEUE_DEPTH                    ((uint32_t)(1.5 * 100))
#endif

// sync fusion gsensor data max config
/// refer to the period of ghleath data reading
#ifndef GH_SYNC_GSENSOR_DATA_MAX
#define GH_SYNC_GSENSOR_DATA_MAX                        ((uint32_t)(1.5 * 100))
#endif

/// define log enable
#ifndef GH_LOG_DEBUG_ENABLE
#define GH_LOG_DEBUG_ENABLE                             (1)
#endif

/// gsensor debug cfg
#ifndef GH_GSENSOR_DEBUG_EN
#define GH_GSENSOR_DEBUG_EN                             (0)
#endif

/// Goodix protocol packaging enable
#ifndef GH_PROTOCOL_EN
#define GH_PROTOCOL_EN                                  (1)
#endif

/// APP layer MIPS statistics
#ifndef GH_APP_MIPS_STA_EN
#define GH_APP_MIPS_STA_EN                              (0)
#endif

#if (GH_APP_MIPS_STA_EN)
#define GH_APP_MAIN_FREQUENCY_MHZ                       (64)
#define GH_APP_TIMESTAMP_UNIT_US                        (1000) // 1ms
#endif

/// define error code op
#define GH_RET_MODULE_ID_GET(x)                         ((x >> 24) & 0xff)
#define GH_RET_INTERNAL_ERR_GET(x)                      ((x >> 8) & 0xffff)
#define GH_RET_INERFACE_ERR_GET(x)                      (x & 0xff)

/// redefine NULL
#define GH_NULL_PTR                                     ((void *) 0)

/// redefine static inlint
#define GH_STATIC_INLINE                                static __inline

/// redefine inlint
#ifndef GH_INLINE
#define GH_INLINE                                       __inline
#endif

/// makeup word from bytes
#define GH_MAKEUP_WORD(high_byte, low_byte)             (((((uint16_t)(high_byte)) << 8)& 0xFF00) |\
                                                          (((uint16_t)(low_byte))& 0x00FF))

/// makeup dword from bytes
#define GH_MAKEUP_DWORD(byte3, byte2, byte1, byte0)     (((((uint32_t)(byte3)) << 24) & 0xFF000000U)| \
                                                          ((((uint32_t)(byte2)) << 16) & 0x00FF0000U) | \
                                                          ((((uint32_t)(byte1)) << 8) & 0x0000FF00U) | \
                                                           (((uint32_t)(byte0)) & 0x000000FFU))

/// macro of val clear bits
#define GH_VAL_CLEAR_BIT(x, b)                          ((x) &= (~(b)))

/// macro of val set bits
#define GH_VAL_SET_BIT(x, b)                            ((x) |= (b))

/// macro of val get bits
#define GH_VAL_GET_BIT(x, b)                            ((x) &= (b))

/*********************************** DO NOT MODIFY FOLLOWING CODE *******************************/
#if (\
            GH_USE_GOODIX_HR_ALGO      ||\
            GH_USE_GOODIX_HRV_ALGO     ||\
            GH_USE_GOODIX_SPO2_ALGO    ||\
            GH_USE_GOODIX_NADT_ALGO\
    )
#define GH_GOODIX_ALGO_ENABLE_MODE                       (1)
#else
#define GH_GOODIX_ALGO_ENABLE_MODE                       (0)
#endif

/*********************************** END ********************************************************/

#ifdef __cplusplus
}
#endif

#endif /* __GH_GLOBAL_CONFIG_H__ */

