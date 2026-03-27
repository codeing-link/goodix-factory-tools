#ifndef __GH_HAL_CHIP_H__
#define __GH_HAL_CHIP_H__

#include <stdint.h>
#include "gh_hal_config.h"
#include "gh_pragma_pack.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define GH3036_PPG_RX_NUM                       (2)
#define GH3036_LED_DRV_NUM                      (2)

#define GH_PPG_RX_NUM                           (GH3036_PPG_RX_NUM)
#define GH_LED_DRV_NUM                          (GH3036_LED_DRV_NUM)

#define GH3036_SLOT_INDEX_REG_NUM               (3)
#define GH3036_SLOT_INDEX_DEFAULT_VAL           (0xFF)
#define GH3036_CHIP_READY_VAL                   (0xAA55)
#define GH3036_CHIP_READY_CONFIRM_CNT           (5)

#define FASTEST_SAMPLE_RET_DEFAULT              (319)


#define PPG_TIA_GAIN_DEFAULT                    (3)
#define LED_DRV_FS_DEFAULT                      (40)
#define LED_DRV_FS_BASE                         (20)
#define LED_DRV_FS_MULTI                        (2)

#define GH3036_FIFO_WIDTH                       (4)
#define GH3036_FIFO_MAX_LEN                     (255)

#define RG_EFUSE_START                          (1)
#define RG_EFUSE_STOP                           (0)
#define RG_EFUSE_LED0_ER_ADDR                   (15)
#define RG_EFUSE_LED1_ER_ADDR                   (16)
#define RG_EFUSE_DC_CANCEL_ER1_ADDR             (17)
#define RG_EFUSE_DC_CANCEL_ER2_ADDR             (18)
#define RG_EFUSE_BG_CANCEL_ER1_ADDR             (19)
#define RG_EFUSE_BG_CANCEL_ER2_ADDR             (20)
#define RG_EFUSE_RX0_OFFSET_ER_ADDR             (21)
#define RG_EFUSE_RX1_OFFSET_ER_ADDR             (22)
#define RG_EFUSE_RX0_GAIN_ER_ADDR               (23)
#define RG_EFUSE_RX1_GAIN_ER_ADDR               (24)

#define GH3036_FIFOCTRL_MODE0                   (0x00)
#define GH3036_FIFOCTRL_MODE1                   (0x01)
#define GH3036_FIFOCTRL_MODE2                   (0x02)
#define GH3036_FIFOCTRL_MODE3                   (0x03)

#define GH3036_BG_ID0                           (0x00)
#define GH3036_BG_ID1                           (0x01)
#define GH3036_BG_ID2                           (0x02)

#define GH3036_BG_LEVEL0                        (0x00)
#define GH3036_BG_LEVEL1                        (0x01)
#define GH3036_BG_LEVEL2                        (0x02)

/**
 * @brief led drv idx enum
 */
typedef enum
{
    GH_LED_DRV0_IDX = 0,             /**< LED DRV0 IDX */
    GH_LED_DRV1_IDX = 1,             /**< LED DRV1 IDX */
    GH_LED_DRV2_IDX = 2,             /**< LED DRV2 IDX */
    GH_LED_DRV3_IDX = 3,             /**< LED DRV3 IDX */
} gh_led_drv_idx_e;


#if GH_HAL_AGC_DRE_EN
/**
 * @brief agc dc cancel idx enum
 */
typedef enum
{
    GH_AGC_DC0_IDX = 0,              /**< AGC DC CANCEL IDX */
    GH_AGC_DC1_IDX = 1,              /**< AGC DC CANCEL IDX */
} gh_agc_dc_cancel_idx_e;
#endif

/**
 * @brief tia gain idx enum
 */
typedef enum
{
    GH_TIA_GIAN0_IDX = 0,             /**< Tia gain 0 IDX */
    GH_TIA_GIAN1_IDX = 1,             /**< Tia gain 1 IDX */
    GH_TIA_GIAN2_IDX = 2,             /**< Tia gain 2 IDX */
    GH_TIA_GIAN3_IDX = 3,             /**< Tia gain 3 IDX */
} gh_tia_gain_idx_e;

/**
  * @brief GH3036 slot en structure.
  */
GH_PACK_BEGIN
typedef struct
{
    uint16_t slot0_en : 1;
    uint16_t slot1_en : 1;
    uint16_t slot2_en : 1;
    uint16_t slot3_en : 1;
    uint16_t slot4_en : 1;
    uint16_t slot5_en : 1;
    uint16_t slot6_en : 1;
    uint16_t slot7_en : 1;
    uint16_t slot8_en : 1;
} GH_PACKED slot_en_t;
GH_PACK_END

/**
  * @brief GH3036 slot en union
  */
GH_PACK_BEGIN
typedef union
{
    slot_en_t st_slot_en;
    uint16_t  slot_en;
} GH_PACKED gh3036_slot_en_t;
GH_PACK_END

/**
  * @brief GH3036 isr event enum.
  */
typedef enum
{
    GH_HAL_ISR_RESET = 0,
    GH_HAL_ISR_FIFO_UP = 1,
    GH_HAL_ISR_FIFO_DOWN = 2,
    GH_HAL_ISR_FIFO_WATER = 3,
    GH_HAL_ISR_TIMER = 4,
    GH_HAL_ISR_USER = 5,
    GH_HAL_ISR_FRAME_DONE = 6,
    GH_HAL_ISR_SAMPLE_ERROR = 7,
    GH_HAL_ISR_CAP_CANCEL = 8,
    GH_HAL_ISR_LDO_OC = 9,
    GH_HAL_ISR_SYNC_SAMPLE_ERROR = 10,
    GH_HAL_ISR_MAX
} gh_hal_isr_event_e;

typedef enum
{
    GH_HAL_INT_TIME_10_US = 0,
    GH_HAL_INT_TIME_20_US = 1,
    GH_HAL_INT_TIME_30_US = 2,
    GH_HAL_INT_TIME_39_US = 3,
    GH_HAL_INT_TIME_60_US = 4,
    GH_HAL_INT_TIME_79_US = 5,
    GH_HAL_INT_TIME_118_US = 6,
    GH_HAL_INT_TIME_158_US = 7,
    GH_HAL_INT_TIME_316_US = 8,
} gh_hal_int_time_e;

/**
  * @brief GH3036 chip init structure.
  */
GH_PACK_BEGIN
typedef struct
{
    uint16_t chip_reset                     : 1;
    uint16_t fifo_up_overflow               : 1;
    uint16_t fifo_down_overflow             : 1;
    uint16_t fifo_waterline                 : 1;
    uint16_t timer_interrupt                : 1;
    uint16_t user_interrupt                 : 1;
    uint16_t frame_done                     : 1;
    uint16_t sample_rate_error              : 1;
    uint16_t cap_cancel_done                : 1;
    uint16_t ldo_oc                         : 1;
    uint16_t frame_sync_out_sample_rate_err : 1;
    uint16_t reserved                       : 5;
} GH_PACKED gh_hal_isr_status_t;
GH_PACK_END

/**
  * @brief PPG rx param structure.
  */
GH_PACK_BEGIN
typedef struct
{
    uint8_t gain_code : 4;
    uint8_t dc_cancel_range : 2;
    uint8_t bg_cancel_range : 2;
    uint8_t dc_cancel_code;

} GH_PACKED gh3036_ppg_rx_param_t;
GH_PACK_END

/**
  * @brief GH3036 ppg cfg param structure.
  */
GH_PACK_BEGIN
typedef struct
{
    uint8_t bg_level : 3;
    uint8_t dre_en : 1;
    uint8_t fifo_ctrl : 2;
    uint8_t rx_en : 2;
    uint8_t dc_cancel_en : 1;
    uint8_t dre_scale : 3;
    uint8_t dre_fifo_output_mode : 1;
    uint8_t reserved : 3;
    uint8_t led_drv_code[GH_LED_DRV_NUM];
#if GH_PARAM_BACKUP_EN
    uint8_t led_drv_code_backup[GH_LED_DRV_NUM];
#endif
    uint16_t multiplier;
    gh3036_ppg_rx_param_t rx_param[GH_PPG_RX_NUM];
#if GH_PARAM_BACKUP_EN
    gh3036_ppg_rx_param_t rx_param_backup[GH_PPG_RX_NUM];
#endif
#if GH_PARAM_SYNC_UPDATE_EN
    gh3036_ppg_rx_param_t rx_param_pre[GH_PPG_RX_NUM];
#endif

} GH_PACKED gh3036_ppg_cfg_param_t;
GH_PACK_END

/**
  * @brief GH3036 global cfg param structure.
  */
GH_PACK_BEGIN
typedef struct
{
    uint8_t   led_drv_fs[GH_LED_DRV_NUM];
    uint16_t  cap_cfg_multiplier;
    uint16_t  fastest_sample_rate;

} GH_PACKED gh3036_global_cfg_param_t;
GH_PACK_END

#if GH_HAL_STD_CALI_EN
/**
  * @brief dc canccel cali param structure.
  */
GH_PACK_BEGIN
typedef struct
{
    int8_t dc_cancel_a;
    int8_t dc_cancel_b;
} GH_PACKED gh_std_cali_dc_param_t;
GH_PACK_END

/**
  * @brief bg canccel cali param structure.
  */
GH_PACK_BEGIN
typedef struct
{
    int8_t bg_cancel_a;
    int8_t bg_cancel_b;
} GH_PACKED gh_std_cali_bg_param_t;
GH_PACK_END

/**
  * @brief led cali param structure.
  */
GH_PACK_BEGIN
typedef struct
{
    int8_t led0_er;
    int8_t led1_er;
} GH_PACKED gh_std_cali_led_param_t;
GH_PACK_END

/**
  * @brief rx offset param structure.
  */
GH_PACK_BEGIN
typedef struct
{
    int8_t rx0_offset;
    int8_t rx1_offset;
} GH_PACKED gh_std_cali_rx_param_t;
GH_PACK_END

/**
  * @brief gain param structure.
  */
GH_PACK_BEGIN
typedef struct
{
    int8_t gain_rx0_offset;
    int8_t gain_rx1_offset;
} GH_PACKED gh_std_cali_gain_param_t;
GH_PACK_END

/**
  * @brief cali param structure.
  */
GH_PACK_BEGIN
typedef struct
{
#if GH_HAL_STD_CALI_DRV_EN
    gh_std_cali_led_param_t  led_param;
#endif

#if GH_HAL_STD_CALI_DC_CANCEL_EN
    gh_std_cali_dc_param_t   dc_cancel_param;
#endif

#if GH_HAL_STD_CALI_BG_CANCEL_EN
    gh_std_cali_bg_param_t   bg_cancel_param;
#endif

#if GH_HAL_STD_CALI_RX_OFFSET_EN
    gh_std_cali_rx_param_t   rx_param;
#endif

#if GH_HAL_STD_CALI_GAIN_EN
    gh_std_cali_gain_param_t gain_param;
#endif
} GH_PACKED gh_hal_std_cali_param_t;
GH_PACK_END
#endif


#ifdef __cplusplus
}
#endif

#endif /* __GH_HAL_CHIP_H__*/
