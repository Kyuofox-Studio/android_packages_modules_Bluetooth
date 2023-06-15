/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "a2dp_vendor_lhdcv2_encoder"
#define ATRACE_TAG ATRACE_TAG_AUDIO

#include "a2dp_vendor_lhdcv2_encoder.h"

#ifndef OS_GENERIC
#include <cutils/trace.h>
#endif
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <lhdcBT.h>

#include "a2dp_vendor.h"
#include "a2dp_vendor_lhdcv2.h"
#include "common/time_util.h"
#include "osi/include/allocator.h"
#include "osi/include/log.h"
#include "osi/include/osi.h"

//
// Encoder for LHDC Source Codec
//

//
// The LHDC encoder shared library, and the functions to use
//
static const char* LHDC_ENCODER_LIB_NAME = "liblhdcBT_enc.so";
static void* lhdc_encoder_lib_handle = NULL;

static const char* LHDC_GET_HANDLE_NAME = "lhdcBT_get_handle";
typedef HANDLE_LHDC_BT (*tLHDC_GET_HANDLE)(int version);

static const char* LHDC_FREE_HANDLE_NAME = "lhdcBT_free_handle";
typedef void (*tLHDC_FREE_HANDLE)(HANDLE_LHDC_BT hLhdcParam);

static const char* LHDC_GET_BITRATE_NAME = "lhdcBT_get_bitrate";
typedef int (*tLHDC_GET_BITRATE)(HANDLE_LHDC_BT hLhdcParam);
static const char* LHDC_SET_BITRATE_NAME = "lhdcBT_set_bitrate";
typedef int (*tLHDC_SET_BITRATE)(HANDLE_LHDC_BT hLhdcParam, int index);

//static const char* LHDC_GET_SAMPLING_FREQ_NAME = "lhdcBT_get_sampling_freq";
//typedef int (*tLHDC_GET_SAMPLING_FREQ)(HANDLE_LHDC_BT hLhdcParam);

static const char* LHDC_INIT_HANDLE_ENCODE_NAME = "lhdcBT_init_encoder";
typedef int (*tLHDC_INIT_HANDLE_ENCODE)(HANDLE_LHDC_BT hLhdcParam,int sampling_freq, int bitPerSample, int bitrate_inx, int dualChannels, int need_padding, int mtu_size, int interval);

static const char* LHDC_AUTO_ADJUST_BITRATE_NAME = "lhdcBT_adjust_bitrate";
typedef int (*tLHDC_AUTO_ADJUST_BITRATE)(HANDLE_LHDC_BT hLhdcParam, size_t queueLength);


static const char* LHDC_ENCODE_NAME = "lhdcBT_encode";
typedef int (*tLHDC_ENCODE)(HANDLE_LHDC_BT hLhdcParam, void* p_pcm, unsigned char* p_stream);

static const char* LHDC_SET_LIMIT_BITRATE_ENABLED_NAME = "lhdcBT_set_max_bitrate";
typedef void (*tLHDC_SET_LIMIT_BITRATE_ENABLED)(HANDLE_LHDC_BT hLhdcParam, int max_rate_index);

//static const char* LHDC_GET_ERROR_CODE_NAME = "lhdcBT_get_error_code";
//typedef int (*tLHDC_GET_ERROR_CODE)(HANDLE_LHDC_BT hLhdcParam);

//static const char* LHDC_GET_SUPPORTED_VERSION = "lhdcBT_getSupportedVersion";
//typedef int (*tLHDC_GET_SUPPORTED_VERSION)(HANDLE_LHDC_BT hLhdcParam);

static const char* LHDC_GET_BLOCK_SIZE = "lhdcBT_get_block_Size";
typedef int (*tLHDC_GET_BLOCK_SIZE)(HANDLE_LHDC_BT hLhdcParam);

static tLHDC_GET_HANDLE lhdc_get_handle_func;
static tLHDC_FREE_HANDLE lhdc_free_handle_func;
static tLHDC_GET_BITRATE lhdc_get_bitrate_func;
static tLHDC_SET_BITRATE lhdc_set_bitrate_func;
static tLHDC_INIT_HANDLE_ENCODE lhdc_init_handle_encode_func;
static tLHDC_ENCODE lhdc_encode_func;
static tLHDC_AUTO_ADJUST_BITRATE lhdc_auto_adjust_bitrate_func;
//static tLHDC_GET_ERROR_CODE lhdc_get_error_code_func;
static tLHDC_SET_LIMIT_BITRATE_ENABLED lhdc_set_limit_bitrate;
//static tLHDC_GET_SUPPORTED_VERSION lhdc_get_supported_version;
static tLHDC_GET_BLOCK_SIZE lhdc_get_block_size;

// A2DP LHDC encoder interval in milliseconds
#define A2DP_LHDC_ENCODER_SHORT_INTERVAL_MS 11
#define A2DP_LHDC_ENCODER_INTERVAL_MS 20

// offset
#if (BTA_AV_CO_CP_SCMS_T == TRUE)
#define A2DP_LHDC_OFFSET (AVDT_MEDIA_OFFSET + A2DP_LHDC_MPL_HDR_LEN + 1)
#else
#define A2DP_LHDC_OFFSET (AVDT_MEDIA_OFFSET + A2DP_LHDC_MPL_HDR_LEN)
#endif

//#define A2DP_LHDC_OFFSET (AVDT_MEDIA_OFFSET + 0)

typedef struct {
  uint32_t sample_rate;
  uint8_t channel_mode;
  uint8_t bits_per_sample;
  int quality_mode_index;
  int latency_mode_index;
  int pcm_wlength;
  LHDCBT_SMPL_FMT_T pcm_fmt;
  int8_t channelSplitMode;
  int8_t maxTargetBitrate;
  bool isLLEnabled;
} tA2DP_LHDC_ENCODER_PARAMS;

typedef struct {
  uint32_t counter;
  uint32_t bytes_per_tick; /* pcm bytes read each media task tick */
  uint64_t last_frame_us;
  uint32_t n_frames;
} tA2DP_LHDC_FEEDING_STATE;

typedef struct {
  uint64_t session_start_us;

  size_t media_read_total_expected_packets;
  size_t media_read_total_expected_reads_count;
  size_t media_read_total_expected_read_bytes;

  size_t media_read_total_dropped_packets;
  size_t media_read_total_actual_reads_count;
  size_t media_read_total_actual_read_bytes;
} a2dp_lhdc_encoder_stats_t;

typedef struct {
  a2dp_source_read_callback_t read_callback;
  a2dp_source_enqueue_callback_t enqueue_callback;
  uint16_t TxAaMtuSize;
  size_t TxQueueLength;

  bool use_SCMS_T;
  bool is_peer_edr;          // True if the peer device supports EDR
  bool peer_supports_3mbps;  // True if the peer device supports 3Mbps EDR
  uint16_t peer_mtu;         // MTU of the A2DP peer
  uint32_t timestamp;        // Timestamp for the A2DP frames

  HANDLE_LHDC_BT lhdc_handle;
  bool has_lhdc_handle;  // True if lhdc_handle is valid
  uint8_t version;

  tA2DP_FEEDING_PARAMS feeding_params;
  tA2DP_LHDC_ENCODER_PARAMS lhdc_encoder_params;
  tA2DP_LHDC_FEEDING_STATE lhdc_feeding_state;

  a2dp_lhdc_encoder_stats_t stats;
  uint32_t buf_seq;
} tA2DP_LHDC_ENCODER_CB;

//static bool lhdc_abr_loaded = false;



typedef struct _lhdc_frame_Info {
    uint32_t frame_len;
    uint32_t isSplit;
    uint32_t isLeft;

} lhdc_frame_Info_t;


#define _RECODER_FILE_
#if defined(_RECODER_FILE_)
static FILE  *RecFile = NULL;
#endif

static tA2DP_LHDC_ENCODER_CB a2dp_lhdc_encoder_cb;

static void a2dp_vendor_lhdcv2_encoder_update(uint16_t peer_mtu,
                                            A2dpCodecConfig* a2dp_codec_config,
                                            bool* p_restart_input,
                                            bool* p_restart_output,
                                            bool* p_config_updated);
static void a2dp_lhdcv2_get_num_frame_iteration(uint8_t* num_of_iterations,
                                              uint8_t* num_of_frames,
                                              uint64_t timestamp_us);
static void a2dp_lhdcv2_encode_frames(uint8_t nb_frame);
static bool a2dp_lhdcv2_read_feeding(uint8_t* read_buffer, uint32_t *bytes_read);
static std::string quality_mode_index_to_name(int quality_mode_index);
//static std::string latency_mode_index_to_name(int latency_mode_index);

static void* load_func(const char* func_name) {
  void* func_ptr = dlsym(lhdc_encoder_lib_handle, func_name);
  if (func_ptr == NULL) {
    LOG_ERROR(
              "%s: cannot find function '%s' in the encoder library: %s",
              __func__, func_name, dlerror());
    A2DP_VendorUnloadEncoderLhdcV2();
    return NULL;
  }
  return func_ptr;
}

bool A2DP_VendorLoadEncoderLhdcV2(void) {
  if (lhdc_encoder_lib_handle != NULL) return true;  // Already loaded

  // Initialize the control block
  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));

  // Open the encoder library
  lhdc_encoder_lib_handle = dlopen(LHDC_ENCODER_LIB_NAME, RTLD_NOW);
  if (lhdc_encoder_lib_handle == NULL) {
    LOG_ERROR( "%s: cannot open LHDC encoder library %s: %s", __func__,
              LHDC_ENCODER_LIB_NAME, dlerror());
    return false;
  }


  // Load all functions
  lhdc_get_handle_func = (tLHDC_GET_HANDLE)load_func(LHDC_GET_HANDLE_NAME);
  if (lhdc_get_handle_func == NULL) return false;
  lhdc_free_handle_func = (tLHDC_FREE_HANDLE)load_func(LHDC_FREE_HANDLE_NAME);
  if (lhdc_free_handle_func == NULL) return false;
  lhdc_get_bitrate_func = (tLHDC_GET_BITRATE)load_func(LHDC_GET_BITRATE_NAME);
  if (lhdc_get_bitrate_func == NULL) return false;
  lhdc_set_bitrate_func = (tLHDC_SET_BITRATE)load_func(LHDC_SET_BITRATE_NAME);
  if (lhdc_set_bitrate_func == NULL) return false;
  lhdc_init_handle_encode_func =
      (tLHDC_INIT_HANDLE_ENCODE)load_func(LHDC_INIT_HANDLE_ENCODE_NAME);
  if (lhdc_init_handle_encode_func == NULL) return false;
  lhdc_encode_func = (tLHDC_ENCODE)load_func(LHDC_ENCODE_NAME);
  if (lhdc_encode_func == NULL) return false;
  lhdc_auto_adjust_bitrate_func = (tLHDC_AUTO_ADJUST_BITRATE)load_func(LHDC_AUTO_ADJUST_BITRATE_NAME);
  if (lhdc_auto_adjust_bitrate_func == NULL) return false;
  //lhdc_get_error_code_func = (tLHDC_GET_ERROR_CODE)load_func(LHDC_GET_ERROR_CODE_NAME);
  //if (lhdc_get_error_code_func == NULL) return false;


  lhdc_set_limit_bitrate = (tLHDC_SET_LIMIT_BITRATE_ENABLED)load_func(LHDC_SET_LIMIT_BITRATE_ENABLED_NAME);
  if (lhdc_set_limit_bitrate == NULL) return false;
  //lhdc_get_supported_version = (tLHDC_GET_SUPPORTED_VERSION)load_func(LHDC_GET_SUPPORTED_VERSION);
  //if (lhdc_get_supported_version == NULL) return false;
  lhdc_get_block_size = (tLHDC_GET_BLOCK_SIZE)load_func(LHDC_GET_BLOCK_SIZE);
  if (lhdc_get_block_size == NULL) return false;

  return true;
}

void A2DP_VendorUnloadEncoderLhdcV2(void) {
  // Cleanup any LHDC-related state

    LOG_DEBUG( "%s: a2dp_lhdc_encoder_cb.has_lhdc_handle = %d, lhdc_free_handle_func = %p",
              __func__, a2dp_lhdc_encoder_cb.has_lhdc_handle, lhdc_free_handle_func);
  if (a2dp_lhdc_encoder_cb.has_lhdc_handle && lhdc_free_handle_func != NULL)
    lhdc_free_handle_func(a2dp_lhdc_encoder_cb.lhdc_handle);
  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));

  lhdc_get_handle_func = NULL;
  lhdc_free_handle_func = NULL;
  lhdc_get_bitrate_func = NULL;
  lhdc_set_bitrate_func = NULL;
  lhdc_init_handle_encode_func = NULL;
  lhdc_encode_func = NULL;
  lhdc_auto_adjust_bitrate_func = NULL;
  //lhdc_get_error_code_func = NULL;
  lhdc_set_limit_bitrate = NULL;

  if (lhdc_encoder_lib_handle != NULL) {
    dlclose(lhdc_encoder_lib_handle);
    lhdc_encoder_lib_handle = NULL;
  }
}

void a2dp_vendor_lhdcv2_encoder_init(
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
    A2dpCodecConfig* a2dp_codec_config,
    a2dp_source_read_callback_t read_callback,
    a2dp_source_enqueue_callback_t enqueue_callback) {
  if (a2dp_lhdc_encoder_cb.has_lhdc_handle)
    lhdc_free_handle_func(a2dp_lhdc_encoder_cb.lhdc_handle);

  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));

  a2dp_lhdc_encoder_cb.stats.session_start_us = bluetooth::common::time_get_os_boottime_us();

  a2dp_lhdc_encoder_cb.read_callback = read_callback;
  a2dp_lhdc_encoder_cb.enqueue_callback = enqueue_callback;
  a2dp_lhdc_encoder_cb.is_peer_edr = p_peer_params->is_peer_edr;
  a2dp_lhdc_encoder_cb.peer_supports_3mbps = p_peer_params->peer_supports_3mbps;
  a2dp_lhdc_encoder_cb.peer_mtu = p_peer_params->peer_mtu;
  a2dp_lhdc_encoder_cb.timestamp = 0;


  a2dp_lhdc_encoder_cb.use_SCMS_T = false;  // TODO: should be a parameter
#if (BTA_AV_CO_CP_SCMS_T == TRUE)
  a2dp_lhdc_encoder_cb.use_SCMS_T = true;
#endif

  // NOTE: Ignore the restart_input / restart_output flags - this initization
  // happens when the connection is (re)started.
  bool restart_input = false;
  bool restart_output = false;
  bool config_updated = false;
  a2dp_vendor_lhdcv2_encoder_update(a2dp_lhdc_encoder_cb.peer_mtu,
                                  a2dp_codec_config, &restart_input,
                                  &restart_output, &config_updated);
}

#if 0
bool A2dpCodecConfigLhdcV2::updateEncoderUserConfig(
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params, bool* p_restart_input,
    bool* p_restart_output, bool* p_config_updated) {
  a2dp_lhdc_encoder_cb.is_peer_edr = p_peer_params->is_peer_edr;
  a2dp_lhdc_encoder_cb.peer_supports_3mbps = p_peer_params->peer_supports_3mbps;
  a2dp_lhdc_encoder_cb.peer_mtu = p_peer_params->peer_mtu;
  a2dp_lhdc_encoder_cb.timestamp = 0;

  if (a2dp_lhdc_encoder_cb.peer_mtu == 0) {
    LOG_ERROR(
              "%s: Cannot update the codec encoder for %s: "
              "invalid peer MTU",
              __func__, name().c_str());
    return false;
  }

  if (a2dp_lhdc_encoder_cb.has_lhdc_handle) {
    lhdc_free_handle_func(a2dp_lhdc_encoder_cb.lhdc_handle);
    a2dp_lhdc_encoder_cb.has_lhdc_handle = false;
    LOG_DEBUG("%s: clean lhdc handle", __func__);
  }

  a2dp_vendor_lhdcv2_encoder_update(a2dp_lhdc_encoder_cb.peer_mtu, this,
                                  p_restart_input, p_restart_output,
                                  p_config_updated);
  return true;
}
#endif

// Update the A2DP LHDC encoder.
// |peer_mtu| is the peer MTU.
// |a2dp_codec_config| is the A2DP codec to use for the update.
static void a2dp_vendor_lhdcv2_encoder_update(uint16_t peer_mtu,
                                            A2dpCodecConfig* a2dp_codec_config,
                                            bool* p_restart_input,
                                            bool* p_restart_output,
                                            bool* p_config_updated) {
  tA2DP_LHDC_ENCODER_PARAMS* p_encoder_params =
      &a2dp_lhdc_encoder_cb.lhdc_encoder_params;
  uint8_t codec_info[AVDT_CODEC_SIZE];

  *p_restart_input = false;
  *p_restart_output = false;
  *p_config_updated = false;

  //Example for limit bit rate
  //lhdc_set_limit_bitrate(a2dp_lhdc_encoder_cb.lhdc_handle, 0);


  if (!a2dp_codec_config->copyOutOtaCodecConfig(codec_info)) {
    LOG_ERROR(
              "%s: Cannot update the codec encoder for %s: "
              "invalid codec config",
              __func__, a2dp_codec_config->name().c_str());
    return;
  }
  const uint8_t* p_codec_info = codec_info;
  btav_a2dp_codec_config_t codec_config = a2dp_codec_config->getCodecConfig();

  if (!a2dp_lhdc_encoder_cb.has_lhdc_handle) {
      a2dp_lhdc_encoder_cb.lhdc_handle = lhdc_get_handle_func(A2DP_VendorGetVersionLhdcV2(p_codec_info) <= A2DP_LHDC_VER2 ? 1 : -1);
      if (a2dp_lhdc_encoder_cb.lhdc_handle == NULL) {
        LOG_ERROR( "%s: Cannot get LHDC encoder handle", __func__);
        return;  // TODO: Return an error?
      }
      a2dp_lhdc_encoder_cb.has_lhdc_handle = true;
  }
  a2dp_lhdc_encoder_cb.version = A2DP_VendorGetVersionLhdcV2(p_codec_info);


  // The feeding parameters
  tA2DP_FEEDING_PARAMS* p_feeding_params = &a2dp_lhdc_encoder_cb.feeding_params;
  p_feeding_params->sample_rate =
      A2DP_VendorGetTrackSampleRateLhdcV2(p_codec_info);
  p_feeding_params->bits_per_sample =
      a2dp_codec_config->getAudioBitsPerSample();
  p_feeding_params->channel_count =
      A2DP_VendorGetTrackChannelCountLhdcV2(p_codec_info);
  LOG_DEBUG( "%s:(feeding) sample_rate=%u bits_per_sample=%u channel_count=%u",
            __func__, p_feeding_params->sample_rate,
            p_feeding_params->bits_per_sample, p_feeding_params->channel_count);

  // The codec parameters
  p_encoder_params->sample_rate =
      a2dp_lhdc_encoder_cb.feeding_params.sample_rate;

  uint16_t mtu_size =
      BT_DEFAULT_BUFFER_SIZE - A2DP_LHDC_OFFSET - sizeof(BT_HDR);

  a2dp_lhdc_encoder_cb.TxAaMtuSize = (mtu_size < peer_mtu) ? mtu_size : peer_mtu;

  a2dp_lhdc_encoder_cb.TxAaMtuSize = a2dp_lhdc_encoder_cb.TxAaMtuSize & ((uint16_t)0xFFFE);

  //get separation feature.
  p_encoder_params->channelSplitMode = A2DP_VendorGetChannelSplitModeLhdcV2(p_codec_info);
  // Set the quality mode index
  //int old_quality_mode_index = p_encoder_params->quality_mode_index;
  LOG_DEBUG( "%s:codec_config.codec_specific_1 = %d, codec_config.codec_specific_2 = %d", __func__, (int32_t)codec_config.codec_specific_1, (int32_t)codec_config.codec_specific_2);
  if ((codec_config.codec_specific_1 & A2DP_LHDC_VENDOR_CMD_MASK) == A2DP_LHDC_QUALITY_MAGIC_NUM) {
      int newValue = codec_config.codec_specific_1 & 0xff;

      // adjust non-supported quality modes and wrap to internal library used index
      if (newValue == A2DP_LHDC_QUALITY_ABR) {
        newValue = LHDCBT_QUALITY_AUTO; //9->8
      }

      if (newValue != p_encoder_params->quality_mode_index) {
        p_encoder_params->quality_mode_index = newValue;
        LOG_DEBUG( "%s: setting internal quality mode: %s(%d)", __func__,
            quality_mode_index_to_name(p_encoder_params->quality_mode_index)
            .c_str(), p_encoder_params->quality_mode_index);
      }
  }else {
      p_encoder_params->quality_mode_index = LHDCBT_QUALITY_AUTO;
      LOG_DEBUG( "%s: setting default quality mode to ABR", __func__);
  }

  //To correcting data to middle.
  if (p_encoder_params->channelSplitMode >= A2DP_LHDC_CH_SPLIT_TWS
       && p_encoder_params->quality_mode_index >= LHDCBT_QUALITY_HIGH) {
      /* code */
      LOG_DEBUG( "%s: Channel separation enabled, Max bit rate = LHDCBT_QUALITY_MID", __func__);
      p_encoder_params->quality_mode_index = LHDCBT_QUALITY_MID;
  }

  p_encoder_params->maxTargetBitrate = A2DP_VendorGetMaxDatarateLhdcV2(p_codec_info);

/*
  //p_encoder_params->latency_mode_index = 1;
  if ((codec_config.codec_specific_2 & A2DP_LHDC_VENDOR_CMD_MASK) == A2DP_LHDC_LATENCY_MAGIC_NUM) {
      int newValue = codec_config.codec_specific_2 & 0xff;
      if (newValue != p_encoder_params->latency_mode_index) {
          p_encoder_params->latency_mode_index = newValue;
          LOG_DEBUG( "%s: setting latency value to %s(%d)", __func__,
                    latency_mode_index_to_name(p_encoder_params->latency_mode_index).c_str(),
                    p_encoder_params->latency_mode_index);
      }
  }else {
      p_encoder_params->latency_mode_index = A2DP_LHDC_LATENCY_MID;
  }
*/

  p_encoder_params->isLLEnabled = (codec_config.codec_specific_2 & 1) != 0 ? true :false; //A2DP_VendorGetLowLatencyState(p_codec_info);


  p_encoder_params->pcm_wlength =
      a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample >> 3;
  // Set the Audio format from pcm_wlength
  p_encoder_params->pcm_fmt = LHDCBT_SMPL_FMT_S16;
  if (p_encoder_params->pcm_wlength == 2)
    p_encoder_params->pcm_fmt = LHDCBT_SMPL_FMT_S16;
  else if (p_encoder_params->pcm_wlength == 3)
    p_encoder_params->pcm_fmt = LHDCBT_SMPL_FMT_S24;
 // else if (p_encoder_params->pcm_wlength == 4)
//    p_encoder_params->pcm_fmt = LHDCBT_SMPL_FMT_S32;

  LOG_DEBUG( "%s: MTU=%d, peer_mtu=%d", __func__,
            a2dp_lhdc_encoder_cb.TxAaMtuSize, peer_mtu);
  LOG_DEBUG(
            "%s: sample_rate: %d "
            "quality_mode_index: %d pcm_wlength: %d pcm_fmt: %d",
            __func__, p_encoder_params->sample_rate,
            p_encoder_params->quality_mode_index, p_encoder_params->pcm_wlength,
            p_encoder_params->pcm_fmt);

#if (BTA_AV_CO_CP_SCMS_T == TRUE)
    uint32_t max_mtu_len = ( uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN - 1);
#else
    uint32_t max_mtu_len = ( uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN);
#endif
  // Initialize the encoder.
  // NOTE: MTU in the initialization must include the AVDT media header size.
  int result = lhdc_init_handle_encode_func(
      a2dp_lhdc_encoder_cb.lhdc_handle,
      p_encoder_params->sample_rate,
      p_encoder_params->pcm_fmt,
      p_encoder_params->quality_mode_index,
      p_encoder_params->channelSplitMode > A2DP_LHDC_CH_SPLIT_NONE ? 1 : 0,
      0 /* This parameter alaways is 0 in A2DP */ ,
      max_mtu_len,
      a2dp_vendor_lhdcv2_get_encoder_interval_ms()
  );
    lhdc_set_limit_bitrate(a2dp_lhdc_encoder_cb.lhdc_handle, p_encoder_params->maxTargetBitrate);

    lhdc_set_bitrate_func(a2dp_lhdc_encoder_cb.lhdc_handle, p_encoder_params->quality_mode_index);

#if defined(_RECODER_FILE_)
  if (RecFile == NULL) {
      RecFile = fopen("/sdcard/Download/lhdc.raw","wb");
    LOG_ERROR( "%s: Create recode file = %p", __func__, RecFile);
  }
#endif
  if (result != 0) {
    LOG_ERROR( "%s: error initializing the LHDC encoder: %d", __func__,
              result);
  }
}

void a2dp_vendor_lhdcv2_encoder_cleanup(void) {
  if (a2dp_lhdc_encoder_cb.has_lhdc_handle)
    lhdc_free_handle_func(a2dp_lhdc_encoder_cb.lhdc_handle);
  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));
#if defined(_RECODER_FILE_)
  if (RecFile != NULL) {
      fclose(RecFile);
      RecFile = NULL;
  }
#endif
}

void a2dp_vendor_lhdcv2_feeding_reset(void) {
  /* By default, just clear the entire state */
  memset(&a2dp_lhdc_encoder_cb.lhdc_feeding_state, 0,
         sizeof(a2dp_lhdc_encoder_cb.lhdc_feeding_state));

  int encoder_interval = a2dp_vendor_lhdcv2_get_encoder_interval_ms();
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.bytes_per_tick =
      (a2dp_lhdc_encoder_cb.feeding_params.sample_rate *
       a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8 *
       a2dp_lhdc_encoder_cb.feeding_params.channel_count *
       encoder_interval) /
      1000;
  a2dp_lhdc_encoder_cb.buf_seq = 0;
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us = 0;
  tA2DP_LHDC_ENCODER_PARAMS* p_encoder_params = &a2dp_lhdc_encoder_cb.lhdc_encoder_params;
  LOG_DEBUG( "%s: quality_mode_index (%d)", __func__,
      p_encoder_params->quality_mode_index);
  if (p_encoder_params->quality_mode_index == LHDCBT_QUALITY_AUTO) {
    if(lhdc_set_bitrate_func != NULL && a2dp_lhdc_encoder_cb.has_lhdc_handle)
      LOG_DEBUG("%s: reset ABR!", __func__);
      lhdc_set_bitrate_func(a2dp_lhdc_encoder_cb.lhdc_handle, LHDCBT_QUALITY_RESET_AUTO);
  }
  LOG_DEBUG( "%s: PCM bytes per tick %u, reset timestamp", __func__,
            a2dp_lhdc_encoder_cb.lhdc_feeding_state.bytes_per_tick);
}

void a2dp_vendor_lhdcv2_feeding_flush(void) {
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter = 0;
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.n_frames = 0;
  LOG_DEBUG( "%s", __func__);
}

uint64_t a2dp_vendor_lhdcv2_get_encoder_interval_ms(void) {
  //LOG_DEBUG( "%s: A2DP_LHDC_ENCODER_INTERVAL_MS %u",
              //__func__, a2dp_lhdc_encoder_cb.lhdc_encoder_params.isLLEnabled ? A2DP_LHDC_ENCODER_SHORT_INTERVAL_MS : A2DP_LHDC_ENCODER_INTERVAL_MS);
  if (a2dp_lhdc_encoder_cb.lhdc_encoder_params.isLLEnabled){
      return A2DP_LHDC_ENCODER_SHORT_INTERVAL_MS;
  }else{
      return A2DP_LHDC_ENCODER_INTERVAL_MS;
  }
}

int a2dp_vendor_lhdcv2_get_effective_frame_size() {
  return a2dp_lhdc_encoder_cb.TxAaMtuSize;
}

void a2dp_vendor_lhdcv2_send_frames(uint64_t timestamp_us) {
  uint8_t nb_frame = 0;
  uint8_t nb_iterations = 0;

  a2dp_lhdcv2_get_num_frame_iteration(&nb_iterations, &nb_frame, timestamp_us);
  LOG_DEBUG( "%s: Sending %d frames per iteration, %d iterations",
              __func__, nb_frame, nb_iterations);
  if (nb_frame == 0) return;

  for (uint8_t counter = 0; counter < nb_iterations; counter++) {
    // Transcode frame and enqueue
    if (a2dp_lhdc_encoder_cb.version <= 1) {
        a2dp_lhdcv2_encode_frames(nb_frame);
    }
  }
}

// Obtains the number of frames to send and number of iterations
// to be used. |num_of_iterations| and |num_of_frames| parameters
// are used as output param for returning the respective values.
static void a2dp_lhdcv2_get_num_frame_iteration(uint8_t* num_of_iterations,
                                              uint8_t* num_of_frames,
                                              uint64_t timestamp_us) {
  uint32_t result = 0;
  uint8_t nof = 0;
  uint8_t noi = 1;
  uint32_t pcm_bytes_per_frame = LHDCV2_BT_ENC_BLOCK_SIZE *
      a2dp_lhdc_encoder_cb.feeding_params.channel_count *
      a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;;
  //LOG_DEBUG( "%s: pcm_bytes_per_frame %u", __func__,
//              pcm_bytes_per_frame);

  int encoder_interval = a2dp_vendor_lhdcv2_get_encoder_interval_ms();
  uint32_t us_this_tick = encoder_interval * 1000;
  uint64_t now_us = timestamp_us;
  if (a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us != 0)
    us_this_tick =
        (now_us - a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us);
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us = now_us;

  a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter +=
      a2dp_lhdc_encoder_cb.lhdc_feeding_state.bytes_per_tick * us_this_tick /
      (encoder_interval * 1000);

  result =
      a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter / pcm_bytes_per_frame;
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter -=
      result * pcm_bytes_per_frame;
  nof = result;

  //LOG_DEBUG( "%s: effective num of frames %u, iterations %u",
//              __func__, nof, noi);

  a2dp_lhdc_encoder_cb.lhdc_feeding_state.n_frames += nof;
  if (a2dp_lhdc_encoder_cb.lhdc_feeding_state.n_frames > A2DP_LHDCV2_HDR_NUM_MAX) {
    nof = A2DP_LHDCV2_HDR_NUM_MAX;
  } else {
    nof = a2dp_lhdc_encoder_cb.lhdc_feeding_state.n_frames;
  }
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.n_frames -= nof;
  *num_of_frames = nof;
  *num_of_iterations = noi;
}

static BT_HDR *bt_buf_new( void) {
    BT_HDR *p_buf = ( BT_HDR*)osi_malloc( BT_DEFAULT_BUFFER_SIZE);
    if ( p_buf == NULL) {
        // LeoKu(C): should not happen
        LOG_ERROR(  "%s: bt_buf_new failed!", __func__);
        return  NULL;
    }

    p_buf->offset = A2DP_LHDC_OFFSET;
    p_buf->len = 0;
    p_buf->layer_specific = 0;
    return  p_buf;
}

/**
 * get lhdc frame header
 */
 /*
static int a2dp_get_lhdc_header(uint8_t * in, lhdc_frame_Info_t * h) {
    #define LHDC_HDR_LEN 4
    uint32_t hdr = 0;
    int ret = -1;
    memcpy(&hdr, in , LHDC_HDR_LEN);
    h->frame_len = ( int)( ( hdr >> 8) & 0x1fff);
    h->isSplit = ( ( hdr & 0x00600000) == 0x00600000);
    h->isLeft = ((hdr & 0xf) == 0);

    if ( ( hdr & 0x4c000000) != 0x4c000000){
        //printf( "lhdc hdr err!\n");
    } else {
        ret = 0;
    }
    return ret;
}
*/

#include <vector>
using namespace std;

static void a2dp_lhdcv2_encode_frames(uint8_t nb_frame) {
    BT_HDR * p_buf = NULL;
    //BT_HDR * p_btBufs[128];
    vector<BT_HDR * > btBufs;
    uint8_t nb_frame_org = nb_frame;
    //tA2DP_LHDC_ENCODER_PARAMS* p_encoder_params =
    //    &a2dp_lhdc_encoder_cb.lhdc_encoder_params;
    uint32_t pcm_bytes_per_frame = LHDCV2_BT_ENC_BLOCK_SIZE *
                                 a2dp_lhdc_encoder_cb.feeding_params.channel_count *
                                 a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;


#if (BTA_AV_CO_CP_SCMS_T == TRUE)
    uint32_t max_mtu_len = ( uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN - 1);
#else
    uint32_t max_mtu_len = ( uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN);
#endif
    uint8_t * read_buffer = (uint8_t*)malloc(pcm_bytes_per_frame * 2);
    uint8_t * write_buffer = (uint8_t*)&(read_buffer[pcm_bytes_per_frame]);
    uint8_t latency = 0;//p_encoder_params->latency_mode_index;
    int out_offset = 0;
    int out_len = 0;
    static uint32_t time_prev = bluetooth::common::time_get_os_boottime_ms();
    static uint32_t allSendbytes = 0;
    uint32_t bytes_read = 0;

        while( nb_frame) {

            uint32_t temp_bytes_read = 0;
            if ( !a2dp_lhdcv2_read_feeding(read_buffer, &temp_bytes_read)) {
            LOG_WARN( "%s: underflow %d", __func__, nb_frame);
            a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter +=
                      nb_frame * LHDCV2_BT_ENC_BLOCK_SIZE *
                      a2dp_lhdc_encoder_cb.feeding_params.channel_count *
                      a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;
                break;
            }
            bytes_read += temp_bytes_read;
            out_offset = 0;
            out_len = lhdc_encode_func(a2dp_lhdc_encoder_cb.lhdc_handle, read_buffer, write_buffer);
    #if defined(_RECODER_FILE_)
            if (RecFile != NULL && out_len > 0) {
                fwrite(write_buffer, sizeof(uint8_t), out_len, RecFile);
            }
    #endif
            nb_frame--;

            while (out_len > 0) {
                if (p_buf == NULL) {
                    if (NULL == (p_buf = bt_buf_new())) {
                        LOG_ERROR ( "%s: ERROR", __func__);
                        if (read_buffer) {
                            free(read_buffer);
                            read_buffer = NULL;
                        }
                        for(BT_HDR*  p : btBufs) {
                            free(p);
                        }
                        btBufs.clear();
                        return;
                    }
                }

                uint8_t *p = ( uint8_t *)( p_buf + 1) + p_buf->offset + p_buf->len;
                int space = max_mtu_len - p_buf->len;
                int bytes = ( out_len < space)? out_len : space;
                memcpy( p, &write_buffer[out_offset], bytes);
                out_offset += bytes;
                out_len -= bytes;
                p_buf->len += bytes;
                allSendbytes += bytes;

                if ( p_buf->len >= max_mtu_len ) {
                    btBufs.push_back(p_buf);
                    // allocate new one
                    p_buf = NULL;
                    if (btBufs.size() >= 64) {
                        LOG_ERROR( "%s: Packet buffer usage to big!(%u)", __func__, (uint32_t)btBufs.size());
                        break;
                    }
                }
            }
        }
        uint32_t now_ms = bluetooth::common::time_get_os_boottime_ms();
        if (now_ms - time_prev >= 1000 ) {
            /* code */
            LOG_WARN( "%s: Current data rate about %d kbps", __func__, (allSendbytes * 8) / 1000);
            allSendbytes = 0;
            time_prev = now_ms;
        }

        if ( p_buf) {
            btBufs.push_back(p_buf);
        }

        LOG_DEBUG( "%s:btBufs.size() = %u", __func__, (uint32_t)btBufs.size());
        if ( btBufs.size() == 1) {
            p_buf = btBufs[0];

            p_buf->layer_specific = a2dp_lhdc_encoder_cb.buf_seq++;
            p_buf->layer_specific <<= 8;
            p_buf->layer_specific |= ( latency | ( nb_frame_org << A2DP_LHDC_HDR_NUM_SHIFT));

            *( ( uint32_t*)( p_buf + 1)) = a2dp_lhdc_encoder_cb.timestamp;

            a2dp_lhdc_encoder_cb.enqueue_callback( p_buf, 1, bytes_read);
            LOG_DEBUG ( "%s: Read bytes(%d)", __func__, bytes_read);
            bytes_read = 0;

        } else {

            uint8_t i;

            for( i = 0; i < btBufs.size(); i++) {
                p_buf = btBufs[i];

                p_buf->layer_specific = a2dp_lhdc_encoder_cb.buf_seq++;
                p_buf->layer_specific <<= 8;
                p_buf->layer_specific |= ( A2DP_LHDC_HDR_F_MSK | latency);

                if ( i == 0) {
                    p_buf->layer_specific |= ( A2DP_LHDC_HDR_S_MSK | ( nb_frame_org << A2DP_LHDC_HDR_NUM_SHIFT));
                } else if ( i == ( btBufs.size() - 1)) {
                    p_buf->layer_specific |= A2DP_LHDC_HDR_L_MSK;
                    bytes_read = 0;
                }

                *( ( uint32_t*)( p_buf + 1)) = a2dp_lhdc_encoder_cb.timestamp;

                a2dp_lhdc_encoder_cb.enqueue_callback( p_buf, 1, bytes_read);
                LOG_DEBUG ( "%s: Read bytes(%d)", __func__, bytes_read);
            }
        }

        a2dp_lhdc_encoder_cb.timestamp += ( nb_frame_org * LHDCV2_BT_ENC_BLOCK_SIZE);
        if (read_buffer) {
            free(read_buffer);
            read_buffer = NULL;
        }
        btBufs.clear();
}

static bool a2dp_lhdcv2_read_feeding(uint8_t* read_buffer, uint32_t *bytes_read) {
    uint32_t bytes_per_sample = a2dp_lhdc_encoder_cb.feeding_params.channel_count * a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;
    uint32_t read_size = LHDCV2_BT_ENC_BLOCK_SIZE * bytes_per_sample;


  a2dp_lhdc_encoder_cb.stats.media_read_total_expected_reads_count++;
  a2dp_lhdc_encoder_cb.stats.media_read_total_expected_read_bytes += read_size;

  /* Read Data from UIPC channel */
  uint32_t nb_byte_read =
      a2dp_lhdc_encoder_cb.read_callback(read_buffer, read_size);
  LOG_DEBUG( "%s: want to read size %u, read byte number %u",
                    __func__, read_size, nb_byte_read);

  if ((nb_byte_read % bytes_per_sample) != 0) {
      LOG_ERROR( "%s: PCM data not alignment. The audio sample is shfit %d bytes.", __func__,(nb_byte_read % bytes_per_sample));
  }
  a2dp_lhdc_encoder_cb.stats.media_read_total_actual_read_bytes += nb_byte_read;

  if (nb_byte_read < read_size) {
    if (nb_byte_read == 0) return false;

    /* Fill the unfilled part of the read buffer with silence (0) */
    memset(((uint8_t*)read_buffer) + nb_byte_read, 0, read_size - nb_byte_read);
    nb_byte_read = read_size;
  }
  a2dp_lhdc_encoder_cb.stats.media_read_total_actual_reads_count++;

    *bytes_read = nb_byte_read;

  return true;
}

// internal library: quality mode index table
static std::string quality_mode_index_to_name(int quality_mode_index) {
  switch (quality_mode_index) {
    case LHDCBT_QUALITY_AUTO:
      return "ABR";
    case LHDCBT_QUALITY_HIGH:
      return "HIGH";
    case LHDCBT_QUALITY_MID:
      return "MID";
    case LHDCBT_QUALITY_LOW:
      return "LOW";
    case LHDCBT_QUALITY_LOW4:
      return "LOW_320";
    case LHDCBT_QUALITY_LOW3:
      return "LOW_256";
    case LHDCBT_QUALITY_LOW2:
      return "LOW_192";
    case LHDCBT_QUALITY_LOW1:
      return "LOW_128";
    case LHDCBT_QUALITY_LOW0:
      return "LOW_64";
    default:
      return "Unknown";
  }
}
/*
static std::string latency_mode_index_to_name(int latency_mode_index){
    switch (latency_mode_index) {
      case A2DP_LHDC_LL_ENABLE:
        return "LL is enabled";
      case A2DP_LHDC_LL_DISABLE:
        return "LL is diabled";
      default:
        return "Unknown";
    }
}
*/

void a2dp_vendor_lhdcv2_set_transmit_queue_length(size_t transmit_queue_length) {
  a2dp_lhdc_encoder_cb.TxQueueLength = transmit_queue_length;
  tA2DP_LHDC_ENCODER_PARAMS* p_encoder_params = &a2dp_lhdc_encoder_cb.lhdc_encoder_params;
  LOG_DEBUG( "%s: transmit_queue_length %zu", __func__, transmit_queue_length);
  if (p_encoder_params->quality_mode_index == LHDCBT_QUALITY_AUTO) {
      LOG_DEBUG( "%s: Auto Bitrate Enabled!", __func__);
      if (lhdc_auto_adjust_bitrate_func != NULL) {
          lhdc_auto_adjust_bitrate_func(a2dp_lhdc_encoder_cb.lhdc_handle, transmit_queue_length);
      }
  }
}

#if 0
uint64_t A2dpCodecConfigLhdcV2::encoderIntervalMs() const {
  return a2dp_vendor_lhdcv2_get_encoder_interval_ms();
}
#endif

#if 0
int A2dpCodecConfigLhdcV2::getEffectiveMtu() const {
  return a2dp_lhdc_encoder_cb.TxAaMtuSize;
}
#endif

void A2dpCodecConfigLhdcV2::debug_codec_dump(int fd) {
  a2dp_lhdc_encoder_stats_t* stats = &a2dp_lhdc_encoder_cb.stats;
  tA2DP_LHDC_ENCODER_PARAMS* p_encoder_params =
      &a2dp_lhdc_encoder_cb.lhdc_encoder_params;

  A2dpCodecConfig::debug_codec_dump(fd);

  dprintf(fd,
          "  Packet counts (expected/dropped)                        : %zu / "
          "%zu\n",
          stats->media_read_total_expected_packets,
          stats->media_read_total_dropped_packets);

  dprintf(fd,
          "  PCM read counts (expected/actual)                       : %zu / "
          "%zu\n",
          stats->media_read_total_expected_reads_count,
          stats->media_read_total_actual_reads_count);

  dprintf(fd,
          "  PCM read bytes (expected/actual)                        : %zu / "
          "%zu\n",
          stats->media_read_total_expected_read_bytes,
          stats->media_read_total_actual_read_bytes);

  dprintf(
      fd, "  LHDC quality mode                                       : %s\n",
      quality_mode_index_to_name(p_encoder_params->quality_mode_index).c_str());

  dprintf(fd,
          "  LHDC transmission bitrate (Kbps)                        : %d\n",
          lhdc_get_bitrate_func(a2dp_lhdc_encoder_cb.lhdc_handle));

  dprintf(fd,
          "  LHDC saved transmit queue length                        : %zu\n",
          a2dp_lhdc_encoder_cb.TxQueueLength);
/*
  if (a2dp_lhdc_encoder_cb.has_lhdc_abr_handle) {
    dprintf(fd,
            "  LHDC adaptive bit rate encode quality mode index        : %d\n",
            a2dp_lhdc_encoder_cb.last_lhdc_abr_eqmid);
    dprintf(fd,
            "  LHDC adaptive bit rate adjustments                      : %zu\n",
            a2dp_lhdc_encoder_cb.lhdc_abr_adjustments);
  }
  */
}
