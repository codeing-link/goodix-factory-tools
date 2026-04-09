/**
  ****************************************************************************************
 * @file    gh_data_package_decode.c
 * @author  GOODIX GH Driver Team
 * @brief   gh3036 frame data decode
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

/*
 * INCLUDE FILES
 *****************************************************************************************
 */
#include "gh_data_package_decode.h"
#include "gh_algo_adapter_common.h"
#include <string.h>
#include <stdlib.h>

/*
 * DEFINES
 *****************************************************************************************
 */
#define CLEAR_BIT7_MASK       (0x7f)
#define SET_BIT7_MASK         (0x80)
#define SHIFT_BIT_NUM         (7)
#define GH_RAWDATA_MAX          (32)
#define GH_FLAGS_MAX            (32)
#define GH_PHY_VALUE_MAX        (32)
#define GH_TIMESTAMP_MAX         (1)
#define GH_GSENSOR_MAX           (6)  // Assuming max 3 for acc + 3 for gyro
#define GH_ALG_RESULTS_MAX       (32)
#define GH_AGC_INFO_MAX          (32)
#define GH_PROTOCOL_OUT_FRAME_MAX (255)

/*
 * ENUMERATIONS FOR ALGORITHM DATA INDEXES
 *****************************************************************************************
 */
/*
 * GLOBAL VARIABLES
 *****************************************************************************************
 */
// History data for differential decoding
static int32_t g_last_rawdata[GH_RAWDATA_MAX] = { 0 };
static int32_t g_last_phy_value[GH_PHY_VALUE_MAX] = { 0 };
static int32_t g_last_timestamp[GH_TIMESTAMP_MAX] = { 0 };
static int32_t g_last_timestamp_high[GH_TIMESTAMP_MAX] = { 0 };
static int32_t g_last_gs_data[GH_GSENSOR_MAX] = { 0 };
static int32_t g_last_flags[GH_FLAGS_MAX] = { 0 };
static int32_t g_last_algo_data[GH_ALG_RESULTS_MAX] = { 0 };
static int32_t g_last_agc_info[GH_AGC_INFO_MAX] = { 0 };
static int32_t g_last_agc_info_high[GH_AGC_INFO_MAX] = { 0 };
static uint8_t g_last_flag_data_bits = 0;
static uint8_t g_last_agc_size = 0;
static uint8_t g_last_gs_data_size = 0;
static uint8_t g_start_flag = 1;

// Global variables for data_frame_t pointers
static int32_t g_rawdata[GH_RAWDATA_MAX] = { 0 };
static int32_t g_phy_value[GH_PHY_VALUE_MAX] = { 0 };
static int32_t g_gs_data[GH_GSENSOR_MAX] = { 0 };
static int32_t g_flags[GH_FLAGS_MAX] = { 0 };
static int32_t g_algo_data[GH_ALG_RESULTS_MAX] = { 0 };
static int32_t g_agc_info[GH_AGC_INFO_MAX] = { 0 };
static int32_t g_agc_info_high[GH_AGC_INFO_MAX] = { 0 };

/*
 * LOCAL FUNCTION DECLARATION
 *****************************************************************************************
 */
static uint32_t gh_protocol_process_single_frame(gh_func_frame_t* p_func_frame, uint8_t* buffer, uint32_t len);
static void gh_protocol_assign_algo_res(gh_func_frame_t* p_func_frame);
static void gh_protocol_parse_agc_info(gh_func_frame_t* p_func_frame, int32_t* agc_info, int32_t* agc_info_high, int32_t size);

/*
 * FUNCTION DEFINITIONS
 *****************************************************************************************
 */

int32_t zigzag_decode(uint32_t x)
{
    return (int32_t)((x >> 1) ^ (-(int32_t)(x & 1)));
}

void gh_protocol_bytes_read(uint8_t* buffer, int32_t* pos, int32_t* dataOut, int32_t data_cnt)
{
    uint32_t num = 0;
    uint8_t cur_byte = 0;
    int32_t cnt = data_cnt;
    int32_t idx = 0;
    uint32_t shift = 0;

    while (cnt--) 
    {
        num = 0;
        shift = 0;
        
        do 
        {
            cur_byte = buffer[*pos];
            (*pos)++;
            num |= (cur_byte & CLEAR_BIT7_MASK) << shift;
            shift += SHIFT_BIT_NUM;
        } while (cur_byte & SET_BIT7_MASK);
        
        dataOut[idx] = zigzag_decode(num);
        idx++;
    }

    return;
}

int32_t gh_protocol_bytes_to_rawdata(data_frame_t* data, uint8_t* buffer, int32_t buffer_size)
{
    int32_t pos = 0;

    // Read pack_header first
    gh_protocol_bytes_read(buffer, &pos, (int32_t*)&data->pack_header, sizeof(data->pack_header) / sizeof(int32_t));

    // Read rawdata if enabled
    if (data->pack_header.rawdata_en) 
    {
        gh_protocol_bytes_read(buffer, &pos, &data->rawdata_size, 1);
        if (data->rawdata_size < 0 || data->rawdata_size > GH_RAWDATA_MAX) return -1;
        gh_protocol_bytes_read(buffer, &pos, data->p_rawdata, data->rawdata_size);
    }

    // Read phy_value if enabled
    if (data->pack_header.phy_value_en) 
    {
        gh_protocol_bytes_read(buffer, &pos, &data->phy_value_size, 1);
        if (data->phy_value_size < 0 || data->phy_value_size > GH_PHY_VALUE_MAX) return -1;
        gh_protocol_bytes_read(buffer, &pos, data->p_phy_value, data->phy_value_size);
    }

    // Read gs_data if enabled
    if (data->pack_header.gs_data_en) 
    {
        gh_protocol_bytes_read(buffer, &pos, &data->gs_data_size, 1);
        if (data->gs_data_size < 0 || data->gs_data_size > GH_GSENSOR_MAX) return -1;
        gh_protocol_bytes_read(buffer, &pos, data->p_gs_data, data->gs_data_size);
    }

    // Read flags if enabled
    if (data->pack_header.flags_en) 
    {
        gh_protocol_bytes_read(buffer, &pos, &data->flag_data_bits, 1);
        if (data->flag_data_bits < 0 || data->flag_data_bits > GH_FLAGS_MAX) return -1;
        gh_protocol_bytes_read(buffer, &pos, data->p_flags, data->flag_data_bits);
    }

    // Read alg_data if enabled
    if (data->pack_header.alg_data_en) 
    {
        gh_protocol_bytes_read(buffer, &pos, &data->algo_data_bits, 1);
        if (data->algo_data_bits < 0 || data->algo_data_bits > GH_ALG_RESULTS_MAX - 1) return -1;
        gh_protocol_bytes_read(buffer, &pos, data->p_algo_data, data->algo_data_bits);
    }

    // Read agc_info if enabled
    if (data->pack_header.agc_info_en) 
    {
        gh_protocol_bytes_read(buffer, &pos, &data->agc_info_size, 1);
        if (data->agc_info_size < 0 || data->agc_info_size > GH_AGC_INFO_MAX) return -1;
        gh_protocol_bytes_read(buffer, &pos, data->p_agc_info, data->agc_info_size);
        gh_protocol_bytes_read(buffer, &pos, data->p_agc_info_high, data->agc_info_size);
    }

    // Read timestamp if enabled
    if (data->pack_header.timestamp_en) 
    {
        gh_protocol_bytes_read(buffer, &pos, &data->timestamp, 1);
        gh_protocol_bytes_read(buffer, &pos, &data->timestamp_high, 1);
    }

    // Read frame_id
    gh_protocol_bytes_read(buffer, &pos, &data->frame_id, 1);

    // Read function_id if enabled
    if (data->pack_header.func_id_en) 
    {
        gh_protocol_bytes_read(buffer, &pos, &data->function_id, 1);
    }

    // Read slot_cfg if enabled
    if (data->pack_header.slot_cfg_en) 
    {
        gh_protocol_bytes_read(buffer, &pos, &data->slot_cfg, 1);
    }

    if (pos > buffer_size) 
    {
        // Buffer overflow
        return -1;
    }

    return pos;
}

static uint32_t gh_protocol_process_single_frame(gh_func_frame_t* p_func_frame, uint8_t* buffer, uint32_t len)
{
    // Create data frame for decoding
    data_frame_t data_frame = {0};
    
    // Assign global arrays to data_frame pointers
    data_frame.p_rawdata = g_rawdata;
    data_frame.p_phy_value = g_phy_value;
    data_frame.p_gs_data = g_gs_data;
    data_frame.p_flags = g_flags;
    data_frame.p_algo_data = g_algo_data;
    data_frame.p_agc_info = g_agc_info;
    data_frame.p_agc_info_high = g_agc_info_high;
    
    // Decode buffer to data frame
    int32_t decoded_size = gh_protocol_bytes_to_rawdata(&data_frame, buffer, len);
    
    if (decoded_size <= 0) 
    {
        return 0; // Error or no data
    }
    
    // Clear the frame
    // memset(p_func_frame, 0, sizeof(gh_func_frame_t));
    
    // Map data from data_frame to gh_func_frame_t
    p_func_frame->frame_cnt = (uint32_t)data_frame.frame_id;
    p_func_frame->id = (gh_func_fix_idx_e)data_frame.function_id;
    
    // Handle timestamp
    if (data_frame.pack_header.timestamp_en) 
    {
        if (!g_start_flag)
        {
            // For differential encoding, add to last value
            gh_ts_union_t ts;
            ts.lld_comb.data = g_last_timestamp[0];
            ts.lld_comb.data_high = g_last_timestamp_high[0];
            uint64_t last_timestamp = ts.timestaple;
            
            // Add differential value to get actual timestamp
            ts.timestaple = last_timestamp + data_frame.timestamp;
            
            p_func_frame->timestamp = ts.timestaple;
            g_last_timestamp[0] = ts.lld_comb.data;
            g_last_timestamp_high[0] = ts.lld_comb.data_high;
        }
        else
        {
            // First frame, use as is
            gh_ts_union_t ts;
            ts.lld_comb.data = data_frame.timestamp;
            ts.lld_comb.data_high = data_frame.timestamp_high;
            p_func_frame->timestamp = ts.timestaple;
            g_last_timestamp[0] = data_frame.timestamp;
            g_last_timestamp_high[0] = data_frame.timestamp_high;
        }
    }
    
    // Handle rawdata if present
    if (data_frame.pack_header.rawdata_en && data_frame.p_rawdata && data_frame.rawdata_size > 0) 
    {
        p_func_frame->ch_num = (uint8_t)data_frame.rawdata_size;
        // Remove malloc, p_data should be pre-allocated by caller
        // Just process the data
        
        for (int i = 0; i < data_frame.rawdata_size; i++) 
        {
            if (!g_start_flag)
            {
                // For differential encoding, add to last value
                p_func_frame->p_data[i].rawdata = g_last_rawdata[i] + data_frame.p_rawdata[i];
                g_last_rawdata[i] = p_func_frame->p_data[i].rawdata;
            }
            else
            {
                // First frame, use as is
                p_func_frame->p_data[i].rawdata = data_frame.p_rawdata[i];
                g_last_rawdata[i] = data_frame.p_rawdata[i];
            }
        }
    }
    
    // Handle phy_value if present
    if (data_frame.pack_header.phy_value_en && data_frame.p_phy_value && data_frame.phy_value_size > 0) 
    {
        // Remove malloc check, p_data should be pre-allocated by caller
        // Just make sure ch_num is set if not already set by rawdata
        if (p_func_frame->ch_num == 0) {
            p_func_frame->ch_num = (uint8_t)data_frame.phy_value_size;
        }
        
        for (int i = 0; i < data_frame.phy_value_size; i++) 
        {
            if (!g_start_flag)
            {
                // For differential encoding, add to last value
                p_func_frame->p_data[i].ipd_pa = g_last_phy_value[i] + data_frame.p_phy_value[i];
                g_last_phy_value[i] = p_func_frame->p_data[i].ipd_pa;
            }
            else
            {
                // First frame, use as is
                p_func_frame->p_data[i].ipd_pa = data_frame.p_phy_value[i];
                g_last_phy_value[i] = data_frame.p_phy_value[i];
            }
        }
    }
    
    // Handle gs_data if present
    if (data_frame.p_gs_data && data_frame.gs_data_size > 0) {
        if (data_frame.pack_header.gs_data_en) 
        {
            g_last_gs_data_size = data_frame.gs_data_size;
            for (int i = 0; i < g_last_gs_data_size; i++) 
            {
                if (i < 3) {
                    if (!g_start_flag)
                    {
                        // For differential encoding, add to last value
                        p_func_frame->gsensor_data.acc[i] = g_last_gs_data[i] + (int16_t)data_frame.p_gs_data[i];
                        g_last_gs_data[i] = p_func_frame->gsensor_data.acc[i];
                    }
                    else
                    {
                        // First frame, use as is
                        p_func_frame->gsensor_data.acc[i] = (int16_t)data_frame.p_gs_data[i];
                        g_last_gs_data[i] = (int16_t)data_frame.p_gs_data[i];
                    }    
                } else {
                #if GH_GYRO_EN
                    if (!g_start_flag)
                    {
                        // For differential encoding, add to last value
                        p_func_frame->gsensor_data.gyro[i - 3] = g_last_gs_data[i] + (int16_t)data_frame.p_gs_data[i];
                        g_last_gs_data[i] = p_func_frame->gsensor_data.gyro[i - 3];
                    } else {
                        // First frame, use as is
                        p_func_frame->gsensor_data.gyro[i - 3] = (int16_t)data_frame.p_gs_data[i];
                        g_last_gs_data[i] = (int16_t)data_frame.p_gs_data[i];
                    }
                #endif
                }

            }
        }
    }
    
    // Handle flags if present
    if (data_frame.p_flags) {
        if (data_frame.pack_header.flags_en) 
        {
            // Remove malloc check, p_data should be pre-allocated by caller
            // Just process the data
            g_last_flag_data_bits = data_frame.flag_data_bits;
            for (int i = 0; i < g_last_flag_data_bits; i++) 
            {
                if (!g_start_flag)
                {
                    // For differential encoding, add to last value
                    g_last_flags[i] = g_last_flags[i] + data_frame.p_flags[i];
                    memcpy(&p_func_frame->p_data[i].flag, &g_last_flags[i], sizeof(gh_frame_data_flag_t));
                }
                else
                {
                    // First frame, use as is
                    g_last_flags[i] = data_frame.p_flags[i];
                    memcpy(&p_func_frame->p_data[i].flag, &data_frame.p_flags[i], sizeof(gh_frame_data_flag_t));
                }
            }
        } else {
            // No flags in this frame, but we need to process the last flags
            for (int i = 0; i < g_last_flag_data_bits; i++) 
            {
                memcpy(&p_func_frame->p_data[i].flag, &g_last_flags[i], sizeof(gh_frame_data_flag_t));
            }
        }
    }
    
    // Handle alg_data if present
    if (data_frame.p_algo_data) {
        if (data_frame.pack_header.alg_data_en) 
        {   
                g_last_algo_data[0] = data_frame.algo_data_bits;
                // Process algorithm data
                for (int i = 0; i < data_frame.algo_data_bits; i++) 
                {
                    if (!g_start_flag)
                    {
                        // For differential encoding, add to last value
                        g_last_algo_data[i+1] = g_last_algo_data[i+1] + data_frame.p_algo_data[i];
                    }
                    else
                    {
                        // First frame, use as is
                        g_last_algo_data[i+1] = data_frame.p_algo_data[i];
                    }
                }
                memcpy(p_func_frame->p_algo_res, g_last_algo_data, g_last_algo_data[0] + 1);
                // Assign algorithm result based on function id
                // gh_protocol_assign_algo_res(p_func_frame);
        } else {
            memcpy(p_func_frame->p_algo_res, g_last_algo_data, g_last_algo_data[0] + 1);
        }
    }
    
    // Handle agc_info if present
    if (data_frame.p_agc_info) {
        if (data_frame.pack_header.agc_info_en) 
        {
            
                // For differential encoding, update last values
                for (int i = 0; i < data_frame.agc_info_size; i++) 
                {
                    if (!g_start_flag)
                    {
                        // For differential encoding, add to last value
                        g_last_agc_info[i] = g_last_agc_info[i] + data_frame.p_agc_info[i];
                        g_last_agc_info_high[i] = g_last_agc_info_high[i] + data_frame.p_agc_info_high[i];
                    }
                    else
                    {
                        // First frame, use as is
                        g_last_agc_info[i] = data_frame.p_agc_info[i];
                        g_last_agc_info_high[i] = data_frame.p_agc_info_high[i];
                    }
                }
                g_last_agc_size = data_frame.agc_info_size;
                // Parse agc info and assign to func frame
                gh_protocol_parse_agc_info(p_func_frame, g_last_agc_info, g_last_agc_info_high, g_last_agc_size);
        } else {
            gh_protocol_parse_agc_info(p_func_frame, g_last_agc_info, g_last_agc_info_high, g_last_agc_size);
        }
    }
    
    // Reset start flag after first frame
    g_start_flag = 0;
    
    // Note: We don't free the global arrays as they are statically allocated
    return decoded_size;
}

void gh_protocol_process(gh_func_frame_t** p_func_frames, uint8_t* frame_len, uint8_t* buffer, uint32_t len)
{
    if (p_func_frames == NULL || *p_func_frames == NULL || frame_len == NULL || buffer == NULL || len == 0) {
        return;  // 无效输入，避免崩溃
    }

    uint32_t pos = 0;
    *frame_len = 0;

    /* Chelsea A 数据按包解码，包内做差分还原；每个包入口重置历史状态。 */
    g_start_flag = 1;
    memset(g_last_rawdata, 0, sizeof(g_last_rawdata));
    memset(g_last_phy_value, 0, sizeof(g_last_phy_value));
    memset(g_last_timestamp, 0, sizeof(g_last_timestamp));
    memset(g_last_timestamp_high, 0, sizeof(g_last_timestamp_high));
    memset(g_last_gs_data, 0, sizeof(g_last_gs_data));
    memset(g_last_flags, 0, sizeof(g_last_flags));
    memset(g_last_algo_data, 0, sizeof(g_last_algo_data));
    memset(g_last_agc_info, 0, sizeof(g_last_agc_info));
    memset(g_last_agc_info_high, 0, sizeof(g_last_agc_info_high));
    g_last_agc_size = 0;
    g_last_flag_data_bits = 0;
    g_last_gs_data_size = 0;

    // Use the provided frame array directly
    gh_func_frame_t* frame_array = *p_func_frames;
    
    while (pos < len) {
        if (*frame_len >= GH_PROTOCOL_OUT_FRAME_MAX) {
            break;
        }
        // Process single frame
        uint32_t processed_len = gh_protocol_process_single_frame(
            &frame_array[*frame_len], 
            buffer + pos, 
            len - pos
        );
        
        if (processed_len == 0) 
        {
            break; // Error or no more data
        }
        
        pos += processed_len;
        (*frame_len)++; // Increment frame count
    }
}

static void gh_protocol_assign_algo_res(gh_func_frame_t* p_func_frame)
{
    if (!p_func_frame || !p_func_frame->p_data) 
    {
        return;
    }

    switch (p_func_frame->id)
    {
        case GH_FUNC_FIX_IDX_ADT:
        {
            // Assuming p_func_frame->p_algo_res points to pre-allocated gh_algo_adt_result_t memory
            gh_algo_adt_result_t *p_adt_res = (gh_algo_adt_result_t *)p_func_frame->p_algo_res;
            if (p_adt_res) 
            {
                p_adt_res->wear_evt = g_last_algo_data[ADT_WEAR_EVENT_IDX];
                p_adt_res->det_status = g_last_algo_data[ADT_WEAR_DET_STATE_IDX];
                p_adt_res->ctr = g_last_algo_data[ADT_WEAR_CTR_IDX];
            }
        }
        break;

        case GH_FUNC_FIX_IDX_HR:
        {
            // Assuming p_func_frame->p_algo_res points to pre-allocated gh_algo_hr_result_t memory
            gh_algo_hr_result_t *p_hr_res = (gh_algo_hr_result_t *)p_func_frame->p_algo_res;
            if (p_hr_res) 
            {
                p_hr_res->hba_out = g_last_algo_data[HR_HBA_OUT_IDX];
                p_hr_res->valid_score = g_last_algo_data[HR_VALID_SCORE_IDX];
                p_hr_res->hba_snr = g_last_algo_data[HR_SNR_IDX];
                p_hr_res->hba_acc_info = g_last_algo_data[HR_ACC_INFO_IDX];
                p_hr_res->hba_reg_scence = g_last_algo_data[HR_REG_SCENCE_IDX];
                p_hr_res->input_sence = g_last_algo_data[HR_INPUT_SCENCE_IDX];
                p_hr_res->reserved1 = g_last_algo_data[HR_RESERVED1];
                p_hr_res->reserved2 = g_last_algo_data[HR_RESERVED2];
                p_hr_res->reserved3 = g_last_algo_data[HR_RESERVED3];
            }
        }
        break;

        case GH_FUNC_FIX_IDX_SPO2:
        {
            // Assuming p_func_frame->p_algo_res points to pre-allocated gh_algo_spo2_result_t memory
            gh_algo_spo2_result_t *p_spo2_res = (gh_algo_spo2_result_t *)p_func_frame->p_algo_res;
            if (p_spo2_res) 
            {
                p_spo2_res->final_spo2 = g_last_algo_data[SPO2_FINAL_SPO2_IDX];
                p_spo2_res->r_val = g_last_algo_data[SPO2_R_VAL_IDX];
                p_spo2_res->confi_coeff = g_last_algo_data[SPO2_CONFI_COEFF_IDX];
                p_spo2_res->valid_level = g_last_algo_data[SPO2_VALID_LEVEL_IDX];
                p_spo2_res->hb_mean = g_last_algo_data[SPO2_HB_MEAN_IDX];
                p_spo2_res->invalid_flag = g_last_algo_data[SPO2_INVALID_FLAG_IDX];
                p_spo2_res->reserved1 = g_last_algo_data[SPO2_RESERVED1];
                p_spo2_res->reserved2 = g_last_algo_data[SPO2_RESERVED2];
                p_spo2_res->reserved3 = g_last_algo_data[SPO2_RESERVED3];
            }
        }
        break;

        case GH_FUNC_FIX_IDX_HRV:
        {
            // Assuming p_func_frame->p_algo_res points to pre-allocated gh_algo_hrv_result_t memory
            gh_algo_hrv_result_t *p_hrv_res = (gh_algo_hrv_result_t *)p_func_frame->p_algo_res;
            if (p_hrv_res) 
            {
                p_hrv_res->rri[0] = g_last_algo_data[HRV_RRI0_IDX];
                p_hrv_res->rri[1] = g_last_algo_data[HRV_RRI1_IDX];
                p_hrv_res->rri[2] = g_last_algo_data[HRV_RRI2_IDX];
                p_hrv_res->rri[3] = g_last_algo_data[HRV_RRI3_IDX];
                p_hrv_res->confidence = g_last_algo_data[HRV_CONFIDENCE_IDX];
                p_hrv_res->valid_num = g_last_algo_data[HRV_VALID_NUM_IDX];
                p_hrv_res->reserved1 = g_last_algo_data[HRV_RESERVED1];
                p_hrv_res->reserved2 = g_last_algo_data[HRV_RESERVED2];
                p_hrv_res->reserved3 = g_last_algo_data[HRV_RESERVED3];
            }
        }
        break;

        case GH_FUNC_FIX_IDX_GNADT:
        case GH_FUNC_FIX_IDX_IRNADT:
        {
            // Assuming p_func_frame->p_algo_res points to pre-allocated gh_algo_nadt_result_t memory
            gh_algo_nadt_result_t *p_nadt_res = (gh_algo_nadt_result_t *)p_func_frame->p_algo_res;
            if (p_nadt_res) 
            {
                p_nadt_res->wear_off_detect_res = g_last_algo_data[NADT_WEAR_OFF_RES_IDX];
                p_nadt_res->live_body_conf = g_last_algo_data[NADT_LIVE_BODY_CONF_IDX];
                p_nadt_res->reserved1 = g_last_algo_data[NADT_RESERVED1];
                p_nadt_res->reserved2 = g_last_algo_data[NADT_RESERVED2];
                p_nadt_res->reserved3 = g_last_algo_data[NADT_RESERVED3];
            }
        }
        break;

        default:
        break;
    }
}

static void gh_protocol_parse_agc_info(gh_func_frame_t* p_func_frame, int32_t* agc_info, int32_t* agc_info_high, int32_t size)
{
    for (int i = 0; i < size; i++) 
    {
        // 将两个32位数据组合成一个64位的AGC联合体
        gh_agc_union_t agc_union;
        agc_union.data32bit[0] = agc_info[i];
        agc_union.data32bit[1] = agc_info_high[i];
        
        // 与 Qt CSV 行为对齐：AGC_INFO_CHx 取 agc_info 的低 32bit 原始值
        // 直接按位拷贝到 agc_info 结构，避免 bitfield 编译器差异导致重组值偏移。
        if (p_func_frame->p_data) {
            uint64_t agc_raw64 = ((uint64_t)(uint32_t)agc_info_high[i] << 32) | (uint32_t)agc_info[i];
            memset(&p_func_frame->p_data[i].agc_info, 0, sizeof(p_func_frame->p_data[i].agc_info));
            memcpy(&p_func_frame->p_data[i].agc_info, &agc_raw64, sizeof(p_func_frame->p_data[i].agc_info));
            
            // led_drv_fs来自帧数据而不是agc_info
            p_func_frame->led_drv_fs[0] = agc_union.gh_agc_upload.led_drv_fs;
        }
    }
}
