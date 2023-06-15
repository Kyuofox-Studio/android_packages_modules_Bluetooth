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

#define LOG_TAG "a2dp_vendor_lhdcv5_encoder"
#define ATRACE_TAG ATRACE_TAG_AUDIO

#include "a2dp_vendor_lhdcv5_encoder.h"

#ifndef OS_GENERIC
#include <cutils/trace.h>
#endif
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <lhdcv5BT.h>

#include "a2dp_vendor.h"
#include "a2dp_vendor_lhdcv5.h"
#include "common/time_util.h"
#include "osi/include/allocator.h"
#include "osi/include/log.h"
#include "osi/include/osi.h"

//
// Encoder for LHDC Source Codec
//
#define MCPS_FRAME_ENCODE    // estimate MCPS of encoding frame

#ifdef MCPS_FRAME_ENCODE
#define STATIS_MILLION_UNIT   (1000000)
#define STATIS_KILO_UNIT      (1000)
#define STATIS_CPU_FREQ_MHZ   (1708800 / 1000)  //reference CPU clock(MHz)
#define MCPS_FRAME_ENCODE_DUR (60)         //statistics duration (sec)
uint64_t mcpsStat_time_dur_cnt_v5 = 0;     //statistics duration counter (sec)
uint64_t mcpsStat_all_enc_us_v5 = 0;       //total execution time(us) of current encoded frames
uint64_t mcpsStat_all_enc_frm_v5 = 0;      //total number of current encoded frames
uint64_t mcpsStat_enc_frm_per_sec_v5 = 0;  //number of frame encoded in one sec
uint64_t mcpsStat_all_time_dur_ms_v5 = 0;  //total real time(ms) when enter final statistics calculation
#endif

#define LHDCV5_SRC_EXT_API_ENABLE // LHDC extended API implementation

//
// The LHDC encoder shared library, and the functions to use
//
static const char* LHDC_ENCODER_LIB_NAME = "liblhdcv5BT_enc.so";
static void* lhdc_encoder_lib_handle = nullptr;

static const char* LHDC_GET_HANDLE_NAME = "lhdcv5BT_get_handle";
typedef int32_t (*tLHDC_GET_HANDLE)(uint32_t version, HANDLE_LHDCV5_BT *hLhdcParam);

static const char* LHDC_FREE_HANDLE_NAME = "lhdcv5BT_free_handle";
typedef int32_t (*tLHDC_FREE_HANDLE)(HANDLE_LHDCV5_BT hLhdcParam);

static const char* LHDC_GET_BITRATE_NAME = "lhdcv5BT_get_bitrate";
typedef int32_t (*tLHDC_GET_BITRATE)(HANDLE_LHDCV5_BT hLhdcParam, uint32_t *bitrate);

static const char* LHDC_SET_BITRATE_NAME = "lhdcv5BT_set_bitrate";
typedef int32_t (*tLHDC_SET_BITRATE)(HANDLE_LHDCV5_BT hLhdcParam, uint32_t bitrateInx);

static const char* LHDC_SET_MAX_BITRATE_NAME = "lhdcv5BT_set_max_bitrate";
typedef int32_t (*tLHDC_SET_MAX_BITRATE)(HANDLE_LHDCV5_BT hLhdcParam, uint32_t maxBitrateInx);

static const char* LHDC_SET_MIN_BITRATE_NAME = "lhdcv5BT_set_min_bitrate";
typedef int32_t (*tLHDC_SET_MIN_BITRATE)(HANDLE_LHDCV5_BT hLhdcParam, uint32_t minBitrateInx);

static const char* LHDC_AUTO_ADJUST_BITRATE_NAME = "lhdcv5BT_adjust_bitrate";
typedef int32_t (*tLHDC_AUTO_ADJUST_BITRATE)(HANDLE_LHDCV5_BT hLhdcParam, uint32_t queueLength);

static const char* LHDC_SET_EXT_FUNC = "lhdcv5BT_set_ext_func_state";
typedef int32_t (*tLHDC_SET_EXT_FUNC)(HANDLE_LHDCV5_BT hLhdcParam,
    LHDCV5_EXT_FUNC_T field, bool enabled, void *priv, uint32_t privDataLen);

static const char* LHDC_INIT_ENCODER_NAME = "lhdcv5BT_init_encoder";
typedef int32_t (*tLHDC_INIT_ENCODER)(HANDLE_LHDCV5_BT hLhdcParam,
    uint32_t samplingFreq, uint32_t bitsPerSample, uint32_t bitrateInx,
    uint32_t mtu, uint32_t interval, uint32_t reserved);

static const char* LHDC_GET_BLOCK_SIZE = "lhdcv5BT_get_block_Size";
typedef int32_t (*tLHDC_GET_BLOCK_SIZE)(HANDLE_LHDCV5_BT hLhdcParam, uint32_t *samplesPerFrame);

static const char* LHDC_ENCODE_NAME = "lhdcv5BT_encode";
typedef int32_t (*tLHDC_ENCODE)(HANDLE_LHDCV5_BT hLhdcParam,
    void *pInPcm, uint32_t pcmBytes, uint8_t *pOutBuf, uint32_t outBufBytes,
    uint32_t *pOutByte, uint32_t *pOut_frames);

//
// LHDC extend function API
//
#ifdef LHDCV5_SRC_EXT_API_ENABLE
static const char* LHDCV5_EXT_SET_CONFIG_NAME = "lhdcv5BT_set_user_exconfig";
typedef int (*tLHDCV5_EXT_SET_CONFIG)(HANDLE_LHDCV5_BT hLhdcParam, const char* userBuf, const int bufLen);

static const char* LHDCV5_EXT_GET_CONFIG_NAME = "lhdcv5BT_get_user_exconfig";
typedef int (*tLHDCV5_EXT_GET_CONFIG)(HANDLE_LHDCV5_BT hLhdcParam, const char* userBuf, const int bufLen);

static const char* LHDC_EXT_SET_DATA_NAME = "lhdcv5BT_set_user_exdata";
typedef void (*tLHDCV5_EXT_SET_DATA)(HANDLE_LHDCV5_BT hLhdcParam, const char* userBuf, const int bufLen);

static const char* LHDCV5_EXT_GET_APIVER_NAME = "lhdcv5BT_get_user_exApiver";
typedef int (*tLHDCV5_EXT_GET_APIVER)(HANDLE_LHDCV5_BT hLhdcParam, const char* userBuf, const int bufLen);
#endif

static tLHDC_GET_HANDLE lhdc_get_handle;
static tLHDC_FREE_HANDLE lhdc_free_handle;
static tLHDC_GET_BITRATE lhdc_get_bitrate;
static tLHDC_SET_BITRATE lhdc_set_bitrate;
static tLHDC_SET_MAX_BITRATE lhdc_set_max_bitrate;
static tLHDC_SET_MIN_BITRATE lhdc_set_min_bitrate;
static tLHDC_AUTO_ADJUST_BITRATE lhdc_auto_adjust_bitrate;
static tLHDC_SET_EXT_FUNC lhdc_set_ext_func;
static tLHDC_INIT_ENCODER lhdc_init_encoder;
static tLHDC_GET_BLOCK_SIZE lhdc_get_block_size;
static tLHDC_ENCODE lhdc_encode_func;

//
// LHDC extend function API
//
#ifdef LHDCV5_SRC_EXT_API_ENABLE
static tLHDCV5_EXT_GET_APIVER lhdcv5_ext_get_ApiVer;
static tLHDCV5_EXT_GET_CONFIG lhdcv5_ext_get_config;
static tLHDCV5_EXT_SET_CONFIG lhdcv5_ext_set_config;
static tLHDCV5_EXT_SET_DATA lhdcv5_ext_set_data;
#endif

// A2DP LHDC encoder interval in milliseconds
#define A2DP_LHDC_ENCODER_SHORT_INTERVAL_MS 10
#define A2DP_LHDC_ENCODER_INTERVAL_MS 20

// offset
#if (BTA_AV_CO_CP_SCMS_T == TRUE)
#define A2DP_LHDC_OFFSET (AVDT_MEDIA_OFFSET + A2DP_LHDC_MPL_HDR_LEN + 1)
#else
#define A2DP_LHDC_OFFSET (AVDT_MEDIA_OFFSET + A2DP_LHDC_MPL_HDR_LEN)
#endif

typedef struct {
  tA2DP_SAMPLE_RATE sample_rate;
  uint32_t bits_per_sample;
  uint32_t quality_mode_index;
  uint32_t pcm_fmt;
  uint32_t max_target_bitrate;
  uint32_t min_target_bitrate;
  uint8_t isLLEnabled;
  uint8_t Reserved;
} tA2DP_LHDCV5_ENCODER_PARAMS;

typedef struct {
  uint32_t counter;
  uint32_t bytes_per_tick; /* pcm bytes read each media task tick */
  uint64_t last_frame_us;
} tA2DP_LHDCV5_FEEDING_STATE;

typedef struct {
  uint64_t session_start_us;

  uint32_t media_read_total_expected_packets;
  uint32_t media_read_total_expected_reads_count;
  uint32_t media_read_total_expected_read_bytes;

  uint32_t media_read_total_dropped_packets;
  uint32_t media_read_total_actual_reads_count;
  uint32_t media_read_total_actual_read_bytes;
} a2dp_lhdcv5_encoder_stats_t;

typedef struct {
  a2dp_source_read_callback_t read_callback;
  a2dp_source_enqueue_callback_t enqueue_callback;
  uint32_t TxAaMtuSize;
  uint32_t TxQueueLength;

  bool use_SCMS_T;
  bool is_peer_edr;          // True if the peer device supports EDR
  bool peer_supports_3mbps;  // True if the peer device supports 3Mbps EDR
  uint16_t peer_mtu;         // MTU of the A2DP peer
  uint32_t timestamp;        // Timestamp for the A2DP frames

  HANDLE_LHDCV5_BT lhdc_handle;
  bool has_lhdc_handle;  // True if lhdc_handle is valid

  tA2DP_FEEDING_PARAMS feeding_params;
  tA2DP_LHDCV5_ENCODER_PARAMS lhdc_encoder_params;
  tA2DP_LHDCV5_FEEDING_STATE lhdc_feeding_state;

  a2dp_lhdcv5_encoder_stats_t stats;
  uint32_t buf_seq;
  uint32_t bytes_read;
} tA2DP_LHDCV5_ENCODER_CB;

// debug flag for dump PCM to file
#define _V5ENC_REC_FILE_
#if defined(_V5ENC_REC_FILE_)
#define V5ENC_RAW_NAME "/sdcard/Download/lhdcv5.raw"
#define V5ENC_PCM_NAME     "/sdcard/Download/sourcev5.pcm"
static FILE  *recFile = NULL;
static FILE *pcmFile = NULL;
#endif

static tA2DP_LHDCV5_ENCODER_CB a2dp_lhdc_encoder_cb;

static void a2dp_vendor_lhdcv5_encoder_update(uint16_t peer_mtu,
    A2dpCodecConfig* a2dp_codec_config,
    bool* p_restart_input,
    bool* p_restart_output,
    bool* p_config_updated);

static void a2dp_lhdcv5_get_num_frame_iteration(uint8_t* num_of_iterations,
    uint8_t* num_of_frames,
    uint64_t timestamp_us);

static void a2dp_lhdcV5_encode_frames(uint8_t nb_frame);

static bool a2dp_lhdcv5_read_feeding(uint8_t* read_buffer, uint32_t *bytes_read);

static std::string quality_mode_index_to_name(uint32_t quality_mode_index);


static void* load_func(const char* func_name) {
  if(!func_name) return NULL;

  void* func_ptr = dlsym(lhdc_encoder_lib_handle, func_name);
  if (func_ptr == NULL) {
    LOG_ERROR(
        "%s: cannot find function '%s' in the encoder library: %s",
        __func__, func_name, dlerror());
    if (!A2DP_VendorUnloadEncoderLhdcV5()) {
      LOG_ERROR( "%s: unload encoder error", __func__);
    }
    return nullptr;
  }
  return func_ptr;
}

bool A2DP_VendorLoadEncoderLhdcV5(void) {
  if (lhdc_encoder_lib_handle != nullptr) return true;  // Already loaded

  // Initialize the control block
  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));

  lhdc_encoder_lib_handle =
      A2DP_VendorCodecLoadExternalLib(LHDC_ENCODER_LIB_NAME, "LHDCv5_Encoder");

  lhdc_get_handle = nullptr;
  lhdc_free_handle = nullptr;
  lhdc_get_bitrate = nullptr;
  lhdc_set_bitrate = nullptr;
  lhdc_set_max_bitrate = nullptr;
  lhdc_set_min_bitrate = nullptr;
  lhdc_auto_adjust_bitrate = nullptr;
  lhdc_set_ext_func = nullptr;
  lhdc_init_encoder = nullptr;
  lhdc_get_block_size =nullptr;
  lhdc_encode_func = nullptr;

  //
  // LHDC extend function API
  //
#ifdef LHDCV5_SRC_EXT_API_ENABLE
  lhdcv5_ext_get_ApiVer = nullptr;
  lhdcv5_ext_get_config = nullptr;
  lhdcv5_ext_set_config = nullptr;
  lhdcv5_ext_set_data = nullptr;
#endif

  // Load all APIs
  lhdc_get_handle = (tLHDC_GET_HANDLE)load_func(LHDC_GET_HANDLE_NAME);
  if (lhdc_get_handle == nullptr) return false;
  lhdc_free_handle = (tLHDC_FREE_HANDLE)load_func(LHDC_FREE_HANDLE_NAME);
  if (lhdc_free_handle == nullptr) return false;
  lhdc_get_bitrate = (tLHDC_GET_BITRATE)load_func(LHDC_GET_BITRATE_NAME);
  if (lhdc_get_bitrate == nullptr) return false;
  lhdc_set_bitrate = (tLHDC_SET_BITRATE)load_func(LHDC_SET_BITRATE_NAME);
  if (lhdc_set_bitrate == nullptr) return false;
  lhdc_set_max_bitrate = (tLHDC_SET_MAX_BITRATE)load_func(LHDC_SET_MAX_BITRATE_NAME);
  if (lhdc_set_max_bitrate == nullptr) return false;
  lhdc_set_min_bitrate = (tLHDC_SET_MIN_BITRATE)load_func(LHDC_SET_MIN_BITRATE_NAME);
  if (lhdc_set_min_bitrate == nullptr) return false;
  lhdc_auto_adjust_bitrate = (tLHDC_AUTO_ADJUST_BITRATE)load_func(LHDC_AUTO_ADJUST_BITRATE_NAME);
  if (lhdc_auto_adjust_bitrate == nullptr) return false;
  lhdc_set_ext_func = (tLHDC_SET_EXT_FUNC)load_func(LHDC_SET_EXT_FUNC);
  if (lhdc_set_ext_func == nullptr) return false;
  lhdc_init_encoder = (tLHDC_INIT_ENCODER)load_func(LHDC_INIT_ENCODER_NAME);
  if (lhdc_init_encoder == nullptr) return false;
  lhdc_get_block_size = (tLHDC_GET_BLOCK_SIZE)load_func(LHDC_GET_BLOCK_SIZE);
  if (lhdc_get_block_size == nullptr) return false;
  lhdc_encode_func = (tLHDC_ENCODE)load_func(LHDC_ENCODE_NAME);
  if (lhdc_encode_func == nullptr) return false;

  //
  // LHDC extend function API
  //
#ifdef LHDCV5_SRC_EXT_API_ENABLE
  lhdcv5_ext_get_ApiVer = (tLHDCV5_EXT_GET_APIVER)load_func(LHDCV5_EXT_GET_APIVER_NAME);
  if (lhdcv5_ext_get_ApiVer == nullptr) return false;
  lhdcv5_ext_get_config = (tLHDCV5_EXT_GET_CONFIG)load_func(LHDCV5_EXT_GET_CONFIG_NAME);
  if (lhdcv5_ext_get_config == nullptr) return false;
  lhdcv5_ext_set_config = (tLHDCV5_EXT_SET_CONFIG)load_func(LHDCV5_EXT_SET_CONFIG_NAME);
  if (lhdcv5_ext_set_config == nullptr) return false;
  lhdcv5_ext_set_data = (tLHDCV5_EXT_SET_DATA)load_func(LHDC_EXT_SET_DATA_NAME);
  if (lhdcv5_ext_set_data == nullptr) return false;
#endif

  return true;
}

bool A2DP_VendorUnloadEncoderLhdcV5(void) {
  if (lhdc_encoder_lib_handle == nullptr) return true;  // Already unload

  // Cleanup any LHDC-related state
  LOG_DEBUG( "%s: has_lhdc_handle %d",
      __func__, a2dp_lhdc_encoder_cb.has_lhdc_handle);

  if (a2dp_lhdc_encoder_cb.has_lhdc_handle && lhdc_free_handle != nullptr) {
    int32_t ret = lhdc_free_handle(a2dp_lhdc_encoder_cb.lhdc_handle);
    if (ret < 0) {
      LOG_ERROR( "%s: free handle error %d", __func__, ret);
      return false;
    }
  } else {
    LOG_ERROR( "%s: unload encoder error", __func__);
    return false;
  }

  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));

  lhdc_get_handle = nullptr;
  lhdc_free_handle = nullptr;
  lhdc_get_bitrate = nullptr;
  lhdc_set_bitrate = nullptr;
  lhdc_set_max_bitrate = nullptr;
  lhdc_set_min_bitrate = nullptr;
  lhdc_auto_adjust_bitrate = nullptr;
  lhdc_set_ext_func = nullptr;
  lhdc_init_encoder = nullptr;
  lhdc_get_block_size =nullptr;
  lhdc_encode_func = nullptr;

  //
  // LHDC extend function API
  //
#ifdef LHDCV5_SRC_EXT_API_ENABLE
  lhdcv5_ext_get_ApiVer = nullptr;
  lhdcv5_ext_get_config = nullptr;
  lhdcv5_ext_set_config = nullptr;
  lhdcv5_ext_set_data = nullptr;
#endif

  dlclose(lhdc_encoder_lib_handle);
  lhdc_encoder_lib_handle = nullptr;

  return true;
}

//tA2DP_ENCODER_INTERFACE::(encoder_init)
void a2dp_vendor_lhdcv5_encoder_init(
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
    A2dpCodecConfig* a2dp_codec_config,
    a2dp_source_read_callback_t read_callback,
    a2dp_source_enqueue_callback_t enqueue_callback) {

  if (p_peer_params == nullptr || a2dp_codec_config == nullptr ||
      read_callback == nullptr || enqueue_callback == nullptr) {
    LOG_ERROR( "%s: null input", __func__);
    return;
  }

  if (a2dp_lhdc_encoder_cb.has_lhdc_handle) {
    int32_t ret = lhdc_free_handle(a2dp_lhdc_encoder_cb.lhdc_handle);
    if (ret < 0) {
      LOG_ERROR( "%s: free handle error %d", __func__, ret);
      return;
    }
  }

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
  a2dp_vendor_lhdcv5_encoder_update(a2dp_lhdc_encoder_cb.peer_mtu,
      a2dp_codec_config, &restart_input,
      &restart_output, &config_updated);
}

//
// LHDC Extended API
//
#ifndef LHDCV5_SRC_EXT_API_ENABLE
int A2dpCodecConfigLhdcV5Source::getEncoderExtendFuncUserApiVer(const char* version, const int clen) {
  return BT_STATUS_FAIL;
}
int A2dpCodecConfigLhdcV5Source::getEncoderExtendFuncUserConfig(const char* userConfig, const int clen) {
  return BT_STATUS_FAIL;
}
int A2dpCodecConfigLhdcV5Source::setEncoderExtendFuncUserConfig(const char* userConfig, const int clen) {
  return BT_STATUS_FAIL;
}
bool A2dpCodecConfigLhdcV5Source::setEncoderExtendFuncUserData(const char* codecData, const int clen) {
  return false;
}
#else
int A2dpCodecConfigLhdcV5Source::getEncoderExtendFuncUserApiVer(const char* version, const int clen)
{
  if (version == nullptr) {
    return BT_STATUS_FAIL;
  }

  if(lhdcv5_ext_get_ApiVer != nullptr) {
    LOG_VERBOSE( "%s: lhdc_ext_API(GetApiVer): clen:%d", __func__, clen);
    return lhdcv5_ext_get_ApiVer(a2dp_lhdc_encoder_cb.lhdc_handle, version, clen);
  } else {
    LOG_DEBUG( "%s: lib func not found", __func__);
    return BT_STATUS_FAIL;
  }
}

int A2dpCodecConfigLhdcV5Source::getEncoderExtendFuncUserConfig(const char* userConfig, const int clen)
{
  if (userConfig == nullptr) {
    return BT_STATUS_FAIL;
  }

  if(lhdcv5_ext_get_config != nullptr) {
    LOG_VERBOSE( "%s: lhdc_ext_API(GetConfig): clen:%d", __func__, clen);
    return lhdcv5_ext_get_config(a2dp_lhdc_encoder_cb.lhdc_handle, userConfig, clen);
  } else {
    LOG_DEBUG( "%s: lib func not found", __func__);
    return BT_STATUS_FAIL;
  }
}

int A2dpCodecConfigLhdcV5Source::setEncoderExtendFuncUserConfig(const char* userConfig, const int clen)
{
  if (userConfig == nullptr) {
    return BT_STATUS_FAIL;
  }

  if(lhdcv5_ext_set_config != nullptr) {
    LOG_VERBOSE( "%s: lhdc_ext_API(SetConfig): clen:%d", __func__, clen);
    return lhdcv5_ext_set_config(a2dp_lhdc_encoder_cb.lhdc_handle, userConfig, clen);
  } else {
    LOG_DEBUG( "%s: lib func not found", __func__);
    return BT_STATUS_FAIL;
  }
}

bool A2dpCodecConfigLhdcV5Source::setEncoderExtendFuncUserData(const char* codecData, const int clen)
{
  if (codecData == nullptr) {
    return BT_STATUS_FAIL;
  }

  if(lhdcv5_ext_set_data != nullptr) {
    LOG_VERBOSE( "%s: lhdc_ext_API(SetData): clen:%d", __func__, clen);
    lhdcv5_ext_set_data(a2dp_lhdc_encoder_cb.lhdc_handle, codecData, clen);
    return true;
  } else {
    LOG_DEBUG( "%s: lib func not found", __func__);
    return false;
  }
}
#endif  //LHDCV5_SRC_EXT_API_ENABLE

#if 0
bool A2dpCodecConfigLhdcV5Source::updateEncoderUserConfig(
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params, bool* p_restart_input,
    bool* p_restart_output, bool* p_config_updated) {

  if (p_peer_params == nullptr || p_restart_input == nullptr ||
      p_restart_output == nullptr || p_config_updated == nullptr) {
    LOG_ERROR( "%s: null input", __func__);
    return false;
  }

  if (a2dp_lhdc_encoder_cb.has_lhdc_handle) {
    lhdc_free_handle(a2dp_lhdc_encoder_cb.lhdc_handle);
    a2dp_lhdc_encoder_cb.has_lhdc_handle = false;
    LOG_DEBUG("%s: clean lhdc handle", __func__);
  }

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

  a2dp_vendor_lhdcv5_encoder_update(a2dp_lhdc_encoder_cb.peer_mtu, this,
      p_restart_input, p_restart_output,
      p_config_updated);
  return true;
}
#endif

// wrap index mapping from bt stack to codec library
static bool a2dp_vendor_lhdcv5_qualitymode_wrapper(uint32_t *out, uint32_t in) {
  if (!out) return false;

  switch(in) {
  case A2DP_LHDC_QUALITY_ABR:
    *out = LHDCV5_QUALITY_AUTO;
    return true;
  case A2DP_LHDC_QUALITY_HIGH1:
    *out = LHDCV5_QUALITY_HIGH1;
    return true;
  case A2DP_LHDC_QUALITY_HIGH:
    *out = LHDCV5_QUALITY_HIGH;
    return true;
  case A2DP_LHDC_QUALITY_MID:
    *out = LHDCV5_QUALITY_MID;
    return true;
  case A2DP_LHDC_QUALITY_LOW:
    *out = LHDCV5_QUALITY_LOW;
    return true;
  case A2DP_LHDC_QUALITY_LOW4:
    *out = LHDCV5_QUALITY_LOW4;
    return true;
  case A2DP_LHDC_QUALITY_LOW3:
    *out = LHDCV5_QUALITY_LOW3;
    return true;
  case A2DP_LHDC_QUALITY_LOW2:
    *out = LHDCV5_QUALITY_LOW2;
    return true;
  case A2DP_LHDC_QUALITY_LOW1:
    *out = LHDCV5_QUALITY_LOW1;
    return true;
  case A2DP_LHDC_QUALITY_LOW0:
    *out = LHDCV5_QUALITY_LOW0;
    return true;
  }

  return false;
}


// Update the A2DP LHDC encoder.
// |peer_mtu| is the peer MTU.
// |a2dp_codec_config| is the A2DP codec to use for the update.
static void a2dp_vendor_lhdcv5_encoder_update(uint16_t peer_mtu,
    A2dpCodecConfig* a2dp_codec_config,
    bool* p_restart_input,
    bool* p_restart_output,
    bool* p_config_updated) {

  tA2DP_LHDCV5_ENCODER_PARAMS* p_encoder_params =
      &a2dp_lhdc_encoder_cb.lhdc_encoder_params;
  uint8_t codec_info[AVDT_CODEC_SIZE];
  uint32_t verCode = 0;
  int32_t lib_ret = 0;
  uint8_t ret_value8 = 0;
  uint32_t mtu_size = 0;
  uint32_t max_mtu_len = 0;
  uint32_t newValue_bt = 0, newValue_lib = 0;
  tA2DP_FEEDING_PARAMS* p_feeding_params;

  const uint8_t *p_codec_info;

  *p_restart_input = false;
  *p_restart_output = false;
  *p_config_updated = false;

  if (!a2dp_codec_config->copyOutOtaCodecConfig(codec_info)) {
    LOG_ERROR(
        "%s: Cannot update the codec encoder for %s: "
        "invalid codec config",
        __func__, a2dp_codec_config->name().c_str());
    return;
  }
  p_codec_info = codec_info;

  btav_a2dp_codec_config_t codec_config = a2dp_codec_config->getCodecConfig();

  // get version
  if (!A2DP_VendorGetVersionLhdcV5(&verCode, p_codec_info)) {
    LOG_ERROR( "%s: get version error!", __func__);
    goto fail;
  }
  LOG_DEBUG( "%s: get version: %d" , __func__, verCode);

  // get new handle
  if (!a2dp_lhdc_encoder_cb.has_lhdc_handle) {
    a2dp_lhdc_encoder_cb.lhdc_handle = nullptr;
    lib_ret = lhdc_get_handle(verCode, &a2dp_lhdc_encoder_cb.lhdc_handle);
    if (lib_ret != LHDCV5_FRET_SUCCESS) {
      LOG_ERROR( "%s: [lib_ret] lhdc_get_handle error %d", __func__, lib_ret);
      goto fail;
    }

    if (a2dp_lhdc_encoder_cb.lhdc_handle == nullptr) {
      LOG_ERROR( "%s: Cannot get LHDC encoder handle", __func__);
      goto fail;
    }
    a2dp_lhdc_encoder_cb.has_lhdc_handle = true;
  }
  LOG_DEBUG( "%s: lhdc handle addr = %p", __func__, a2dp_lhdc_encoder_cb.lhdc_handle);

  //
  // setup feeding parameters for encoder feeding process
  //
  p_feeding_params = &a2dp_lhdc_encoder_cb.feeding_params;
  // sample rate (uint32_t)
  p_feeding_params->sample_rate = A2DP_VendorGetTrackSampleRateLhdcV5(p_codec_info);
  if (p_feeding_params->sample_rate < 0) {
    LOG_ERROR( "%s: get track sample rate error", __func__);
    goto fail;
  }

  // bit per sample (uint8_t)
  p_feeding_params->bits_per_sample = A2DP_VendorGetTrackBitsPerSampleLhdcV5(p_codec_info);
  if (p_feeding_params->bits_per_sample < 0) {
    LOG_ERROR( "%s: get bit per sample error", __func__);
    goto fail;
  }

  //channel count (uint8_t)
  p_feeding_params->channel_count = A2DP_VendorGetTrackChannelCountLhdcV5(p_codec_info);
  if (p_feeding_params->channel_count < 0) {
    LOG_ERROR( "%s: get channel count error", __func__);
    goto fail;
  }
  LOG_DEBUG( "%s: (feeding param) sample_rate=%u bits_per_sample=%u channel_count=%u",
      __func__, p_feeding_params->sample_rate,
      p_feeding_params->bits_per_sample,
      p_feeding_params->channel_count);

  //
  // setup encoder parameters for configuring encoder
  //
  // sample rate tA2DP_SAMPLE_RATE(uint32_t)
  p_encoder_params->sample_rate = a2dp_lhdc_encoder_cb.feeding_params.sample_rate;

  // default mtu size (uint32_t)
  mtu_size = (BT_DEFAULT_BUFFER_SIZE - A2DP_LHDC_OFFSET - sizeof(BT_HDR));
  // allowed mtu size (uint32_t)
  a2dp_lhdc_encoder_cb.TxAaMtuSize = (mtu_size < peer_mtu) ? mtu_size : (uint32_t)peer_mtu;
  // real mtu size (uint32_t)
#if (BTA_AV_CO_CP_SCMS_T == TRUE)
  max_mtu_len = (uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN - 1);
#else
  max_mtu_len = (uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN);
#endif

  // max target bit rate
  if (!A2DP_VendorGetMaxBitRateLhdcV5(&newValue_bt, p_codec_info) ) {
    LOG_ERROR( "%s: get max_target_bitrate error", __func__);
    goto fail;
  }
  if (!a2dp_vendor_lhdcv5_qualitymode_wrapper(&newValue_lib, newValue_bt)) {
    LOG_ERROR( "%s: wrap MBR qualiity mode error", __func__);
    goto fail;
  }
  p_encoder_params->max_target_bitrate = newValue_lib;

  // min target bit rate
  if (!A2DP_VendorGetMinBitRateLhdcV5(&newValue_bt, p_codec_info) ) {
    LOG_ERROR( "%s: get min_target_bitrate error", __func__);
    goto fail;
  }
  if (!a2dp_vendor_lhdcv5_qualitymode_wrapper(&newValue_lib, newValue_bt)) {
    LOG_ERROR( "%s: wrap mBR qualiity mode error", __func__);
    goto fail;
  }
  p_encoder_params->min_target_bitrate = newValue_lib;

  // Low latency mode
  if (!A2DP_VendorHasLLFlagLhdcV5(&(p_encoder_params->isLLEnabled), p_codec_info)){
    LOG_ERROR( "%s: get Low latency enable error", __func__);
    goto fail;
  }

  // bit per sample
  switch((int)p_feeding_params->bits_per_sample) {
  case 16:
    p_encoder_params->pcm_fmt = LHDCV5BT_SMPL_FMT_S16;
    break;
  case 24:
    p_encoder_params->pcm_fmt = LHDCV5BT_SMPL_FMT_S24;
    break;
  case 32:
    p_encoder_params->pcm_fmt = LHDCV5BT_SMPL_FMT_S32;
    break;
  }

  // quality mode
  if ((codec_config.codec_specific_1 & A2DP_LHDC_VENDOR_CMD_MASK) == A2DP_LHDC_QUALITY_MAGIC_NUM) {
    newValue_bt = (codec_config.codec_specific_1 & A2DP_LHDC_QUALITY_MASK);
    if (!a2dp_vendor_lhdcv5_qualitymode_wrapper(&newValue_lib, newValue_bt)) {
      LOG_ERROR( "%s: wrap quality mode error", __func__);
      goto fail;
    }

    if (newValue_lib != p_encoder_params->quality_mode_index) {
      p_encoder_params->quality_mode_index = newValue_lib;
    }
  } else {
    //give default: ABR
    newValue_bt = A2DP_LHDC_QUALITY_ABR;
      if (!a2dp_vendor_lhdcv5_qualitymode_wrapper(&newValue_lib, newValue_bt)) {
        LOG_ERROR( "%s: (default) wrap quality mode error", __func__);
        goto fail;
      }
      p_encoder_params->quality_mode_index = newValue_lib;
      }

  LOG_DEBUG( "%s: (encode param) sample_rate=%u pcm_fmt=%d peer_mtu=%d mtu=%d "
      "maxBitRateIdx=%u minBitRateIdx=%u isLLEnabled=%u Reserved=%u quality_mode=%s(%d)", __func__,
      p_encoder_params->sample_rate,                //44100, 48000, ...
      p_encoder_params->pcm_fmt,                    //16, 24, 32...
      peer_mtu, max_mtu_len,                        //number of bytes
      p_encoder_params->max_target_bitrate,         //index
      p_encoder_params->min_target_bitrate,         //index
      p_encoder_params->isLLEnabled,
      p_encoder_params->Reserved,
      quality_mode_index_to_name(p_encoder_params->quality_mode_index).c_str(),
      p_encoder_params->quality_mode_index);

  // setup features to encoder
  if (A2DP_VendorHasARFlagLhdcV5(&ret_value8, p_codec_info)) {
    lib_ret = lhdc_set_ext_func(a2dp_lhdc_encoder_cb.lhdc_handle, LHDCV5_EXT_FUNC_AR, (bool)ret_value8, NULL, 0);
    if (lib_ret != LHDCV5_FRET_SUCCESS) {
      LOG_ERROR( "%s: [lib_ret] lhdc_set_ext_func AR(0x%X) %d", __func__, LHDCV5_EXT_FUNC_AR, lib_ret);
      goto fail;
    }
  }

  if (A2DP_VendorHasJASFlagLhdcV5(&ret_value8, p_codec_info)) {
    lib_ret = lhdc_set_ext_func(a2dp_lhdc_encoder_cb.lhdc_handle, LHDCV5_EXT_FUNC_JAS, (bool)ret_value8, NULL, 0);
    if (lib_ret != LHDCV5_FRET_SUCCESS) {
      LOG_ERROR( "%s: [lib_ret] lhdc_set_ext_func JAS(0x%X) %d", __func__, LHDCV5_EXT_FUNC_JAS, lib_ret);
      goto fail;
    }
  }

  // Initialize the encoder.
  // NOTE: MTU in the initialization must include the AVDT media header size.
  lib_ret = lhdc_init_encoder(
      a2dp_lhdc_encoder_cb.lhdc_handle,
      p_encoder_params->sample_rate,
      p_encoder_params->pcm_fmt,
      p_encoder_params->quality_mode_index,
      max_mtu_len,
      (uint32_t)a2dp_vendor_lhdcv5_get_encoder_interval_ms(),
      p_encoder_params->Reserved);
  if (lib_ret != LHDCV5_FRET_SUCCESS) {
    LOG_ERROR( "%s: [lib_ret] lhdc_init_encoder %d", __func__, lib_ret);
    goto fail;
  }
  // setup after encoder initialized
  lib_ret = lhdc_set_max_bitrate(a2dp_lhdc_encoder_cb.lhdc_handle, p_encoder_params->max_target_bitrate);
  if (lib_ret != LHDCV5_FRET_SUCCESS) {
    LOG_ERROR( "%s: [lib_ret] set_max_bitrate %d", __func__, lib_ret);
    goto fail;
  }
  lib_ret = lhdc_set_min_bitrate(a2dp_lhdc_encoder_cb.lhdc_handle, p_encoder_params->min_target_bitrate);
  if (lib_ret != LHDCV5_FRET_SUCCESS) {
    LOG_ERROR( "%s: [lib_ret] set_min_bitrate %d", __func__, lib_ret);
    goto fail;
  }
  lib_ret = lhdc_set_bitrate(a2dp_lhdc_encoder_cb.lhdc_handle, p_encoder_params->quality_mode_index);
  if (lib_ret != LHDCV5_FRET_SUCCESS) {
    LOG_ERROR( "%s: [lib_ret] set_bitrate %d", __func__, lib_ret);
    goto fail;
  }

#ifdef MCPS_FRAME_ENCODE
  mcpsStat_all_enc_us_v5 = 0;
  mcpsStat_all_enc_frm_v5 = 0;
  mcpsStat_time_dur_cnt_v5 = 0;
  mcpsStat_enc_frm_per_sec_v5 = 0;
  mcpsStat_all_time_dur_ms_v5 = 0;
#endif

#if defined(_V5ENC_REC_FILE_)
  if (recFile == NULL) {
    recFile = fopen(V5ENC_RAW_NAME,"wb");
    LOG_DEBUG( "%s: create recode file = %p", __func__, recFile);
  }
  if (pcmFile == NULL) {
    pcmFile = fopen(V5ENC_PCM_NAME,"wb");
    LOG_DEBUG( "%s: create recode file = %p", __func__, pcmFile);
  }
#endif
  return;

  fail:
  if (a2dp_lhdc_encoder_cb.lhdc_handle) {
    a2dp_vendor_lhdcv5_encoder_cleanup();
  }
}

//tA2DP_ENCODER_INTERFACE::(encoder_cleanup)
void a2dp_vendor_lhdcv5_encoder_cleanup(void) {
  if (a2dp_lhdc_encoder_cb.has_lhdc_handle && a2dp_lhdc_encoder_cb.lhdc_handle) {
    int32_t lib_ret = lhdc_free_handle(a2dp_lhdc_encoder_cb.lhdc_handle);
    if (lib_ret != LHDCV5_FRET_SUCCESS) {
      LOG_ERROR( "%s: free handle error %d", __func__, lib_ret);
      return;
    }
  } else {
    LOG_DEBUG( "%s: nothing to clean", __func__);
    return;
  }
  memset(&a2dp_lhdc_encoder_cb, 0, sizeof(a2dp_lhdc_encoder_cb));

#if defined(_V5ENC_REC_FILE_)
  if (recFile != NULL) {
    fclose(recFile);
    recFile = NULL;
    remove(V5ENC_RAW_NAME);
  }
  if (pcmFile != NULL) {
    fclose(pcmFile);
    pcmFile = NULL;
    remove(V5ENC_PCM_NAME);
  }
#endif

  LOG_DEBUG( "%s: encoder cleaned up", __func__);
}

//tA2DP_ENCODER_INTERFACE::(feeding_reset)
void a2dp_vendor_lhdcv5_feeding_reset(void) {
  /* By default, just clear the entire state */
  memset(&a2dp_lhdc_encoder_cb.lhdc_feeding_state, 0,
      sizeof(a2dp_lhdc_encoder_cb.lhdc_feeding_state));

  uint32_t encoder_interval = (uint32_t)a2dp_vendor_lhdcv5_get_encoder_interval_ms();
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.bytes_per_tick =
      (a2dp_lhdc_encoder_cb.feeding_params.sample_rate *
          a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8 *
          a2dp_lhdc_encoder_cb.feeding_params.channel_count *
          encoder_interval) / 1000;
  a2dp_lhdc_encoder_cb.buf_seq = 0;
  a2dp_lhdc_encoder_cb.bytes_read = 0;
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us = 0;

  tA2DP_LHDCV5_ENCODER_PARAMS* p_encoder_params = &a2dp_lhdc_encoder_cb.lhdc_encoder_params;
  if (p_encoder_params->quality_mode_index == LHDCV5_QUALITY_AUTO) {
    if (lhdc_set_bitrate != NULL && a2dp_lhdc_encoder_cb.has_lhdc_handle) {
      lhdc_set_bitrate(a2dp_lhdc_encoder_cb.lhdc_handle, LHDCV5_QUALITY_CTRL_RESET_ABR);
    }
  }

#ifdef MCPS_FRAME_ENCODE
  mcpsStat_all_enc_us_v5 = 0;
  mcpsStat_all_enc_frm_v5 = 0;
  mcpsStat_time_dur_cnt_v5 = 0;
  mcpsStat_enc_frm_per_sec_v5 = 0;
  mcpsStat_all_time_dur_ms_v5 = 0;
#endif

  LOG_VERBOSE( "%s: PCM bytes per tick %u, reset timestamp", __func__,
      a2dp_lhdc_encoder_cb.lhdc_feeding_state.bytes_per_tick);
}

//tA2DP_ENCODER_INTERFACE::(feeding_flush)
void a2dp_vendor_lhdcv5_feeding_flush(void) {
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter = 0;
  LOG_DEBUG( "%s", __func__);
}

//tA2DP_ENCODER_INTERFACE::(get_encoder_interval_ms)
uint64_t a2dp_vendor_lhdcv5_get_encoder_interval_ms(void) {
  LOG_VERBOSE( "%s: A2DP_LHDC_ENCODER_INTERVAL_MS %u",  __func__,
      a2dp_lhdc_encoder_cb.lhdc_encoder_params.isLLEnabled ? A2DP_LHDC_ENCODER_SHORT_INTERVAL_MS : A2DP_LHDC_ENCODER_INTERVAL_MS);

  if (a2dp_lhdc_encoder_cb.lhdc_encoder_params.isLLEnabled){
    return A2DP_LHDC_ENCODER_SHORT_INTERVAL_MS;
  }else{
    return A2DP_LHDC_ENCODER_INTERVAL_MS;
  }
}

int a2dp_vendor_lhdcv5_get_effective_frame_size() {
  return a2dp_lhdc_encoder_cb.TxAaMtuSize;
}

//tA2DP_ENCODER_INTERFACE::(send_frames)
void a2dp_vendor_lhdcv5_send_frames(uint64_t timestamp_us) {
  uint8_t nb_frame = 0;
  uint8_t nb_iterations = 0;

  a2dp_lhdcv5_get_num_frame_iteration(&nb_iterations, &nb_frame, timestamp_us);
  LOG_VERBOSE( "%s: Sending %d frames per iteration, %d iterations",
      __func__, nb_frame, nb_iterations);

  if (nb_frame == 0) return;

  for (uint8_t counter = 0; counter < nb_iterations; counter++) {
    // Transcode frame and enqueue
    a2dp_lhdcV5_encode_frames(nb_frame);
  }
}

// Obtains the number of frames to send and number of iterations
// to be used. |num_of_iterations| and |num_of_frames| parameters
// are used as output param for returning the respective values.
static void a2dp_lhdcv5_get_num_frame_iteration(uint8_t* num_of_iterations,
    uint8_t* num_of_frames,
    uint64_t timestamp_us) {

  uint32_t result = 0;
  uint8_t nof = 0;
  uint8_t noi = 1;
  uint32_t pcm_bytes_per_frame = 0;
  uint32_t samples_per_frame = 0;
  int32_t lib_ret = 0;

  lib_ret = lhdc_get_block_size(a2dp_lhdc_encoder_cb.lhdc_handle, &samples_per_frame);
  if (lib_ret != LHDCV5_FRET_SUCCESS) {
    LOG_DEBUG( "%s: get block size error %d", __func__, lib_ret);
    return;
  }

  pcm_bytes_per_frame = samples_per_frame *
      a2dp_lhdc_encoder_cb.feeding_params.channel_count *
      a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;

  uint32_t encoder_interval = (uint32_t)a2dp_vendor_lhdcv5_get_encoder_interval_ms();
  uint32_t us_this_tick = encoder_interval * 1000;
  uint64_t now_us = timestamp_us;

  // not the first time, calculate time offset
  if (a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us != 0) {
    us_this_tick = (now_us - a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us);
  }
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.last_frame_us = now_us;

  a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter +=
      a2dp_lhdc_encoder_cb.lhdc_feeding_state.bytes_per_tick * us_this_tick /
      (encoder_interval * 1000);

  result =
      a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter / pcm_bytes_per_frame;
  a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter -=
      result * pcm_bytes_per_frame;
  nof = result;

  LOG_VERBOSE( "%s: samples_per_frame=%u pcm_bytes_per_frame=%u nb_frame=%u",
      __func__, samples_per_frame, pcm_bytes_per_frame, nof);

  *num_of_frames = nof;
  *num_of_iterations = noi;
}

static BT_HDR *bt_buf_new( void) {
  BT_HDR *p_buf = ( BT_HDR*)osi_malloc(BT_DEFAULT_BUFFER_SIZE);
  if ( p_buf == NULL) {
    // LeoKu(C): should not happen
    LOG_ERROR(  "%s: bt_buf_new failed!", __func__);
    return NULL;
  }

  p_buf->offset = A2DP_LHDC_OFFSET;
  p_buf->len = 0;
  p_buf->layer_specific = 0;
  return p_buf;
}

static void a2dp_lhdcV5_encode_frames(uint8_t nb_frame){
  static float mtu_usage = 0;
  static int mtu_usage_cnt = 0;
  static uint32_t time_prev = bluetooth::common::time_get_os_boottime_ms();
  static uint32_t all_send_bytes = 0;
  uint8_t read_buffer[LHDCV5_MAX_SAMPLE_FRAME * 2 * 4];
  uint32_t samples_per_frame = 0;
  uint32_t out_frames = 0;
  uint8_t remain_nb_frame = nb_frame;
  uint32_t written = 0;
  uint32_t bytes_read = 0;
  uint8_t *packet = nullptr;
  BT_HDR *p_buf = nullptr;
  int32_t lib_ret = 0;
  uint32_t pcm_bytes_per_frame = 0;
  uint32_t max_mtu_len = 0;

  uint32_t written_frame = 0;
  uint32_t temp_bytes_read = 0;

#ifdef MCPS_FRAME_ENCODE
  uint64_t time_prev_enc_us_2 = 0;
  uint64_t time_aft_enc_us_2 = 0;
#endif

  lib_ret = lhdc_get_block_size(a2dp_lhdc_encoder_cb.lhdc_handle, &samples_per_frame);
  if (lib_ret != LHDCV5_FRET_SUCCESS) {
    LOG_DEBUG( "%s: [lib_ret] lhdc_get_block_size error %d", __func__, lib_ret);
    return;
  }
  pcm_bytes_per_frame = samples_per_frame *
      a2dp_lhdc_encoder_cb.feeding_params.channel_count *
      a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;

  if (pcm_bytes_per_frame > sizeof(read_buffer)) {
    LOG_ERROR( "%s: expected read size error", __func__);
    return;
  }

  // check codec handle existed
  if (!a2dp_lhdc_encoder_cb.has_lhdc_handle || !a2dp_lhdc_encoder_cb.lhdc_handle) {
    LOG_ERROR( "%s: encoder handle invalid error", __func__);
    return;
  }

#if (BTA_AV_CO_CP_SCMS_T == TRUE)
  max_mtu_len = ( uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN - 1);
#else
  max_mtu_len = ( uint32_t)( a2dp_lhdc_encoder_cb.TxAaMtuSize - A2DP_LHDC_MPL_HDR_LEN);
#endif

  while (nb_frame) {
    // create a temp output buffer
    if ((p_buf = bt_buf_new()) == NULL) {
      LOG_ERROR( "%s: create buf error", __func__);
      return;
    }

    written_frame = 0;
    do {
      temp_bytes_read = 0;
      // read from feeding buffer
      if (a2dp_lhdcv5_read_feeding(read_buffer, &temp_bytes_read)) {
        a2dp_lhdc_encoder_cb.bytes_read += temp_bytes_read;
        packet = (uint8_t*)(p_buf + 1) + p_buf->offset + p_buf->len;

#if defined(_V5ENC_REC_FILE_)
        if (pcmFile != NULL) {
          fwrite(read_buffer, sizeof(uint8_t), pcm_bytes_per_frame, pcmFile);
        }
#endif

        LOG_VERBOSE( "%s: nb_frame(%d) to encode...", __func__, nb_frame);
        // to encode

#ifdef MCPS_FRAME_ENCODE
        time_prev_enc_us_2 = bluetooth::common::time_get_os_boottime_ms();
#endif
        lib_ret = lhdc_encode_func(a2dp_lhdc_encoder_cb.lhdc_handle,
            read_buffer, temp_bytes_read,
            packet, (BT_DEFAULT_BUFFER_SIZE-(p_buf->offset + p_buf->len + sizeof(BT_HDR))),
            &written, &out_frames);

        if (lib_ret != LHDCV5_FRET_SUCCESS) {
          LOG_ERROR( "%s: [lib_ret] lhdc_encode_func error %d", __func__, lib_ret);
          a2dp_lhdc_encoder_cb.stats.media_read_total_dropped_packets++;
          osi_free(p_buf);
          return;
        }
#ifdef MCPS_FRAME_ENCODE
        if (mcpsStat_time_dur_cnt_v5 > 0) { //begin after statistics initialized
          if (out_frames > 0) {
            time_aft_enc_us_2 = bluetooth::common::time_get_os_boottime_ms();
            mcpsStat_all_enc_us_v5 += (time_aft_enc_us_2 - time_prev_enc_us_2);
            mcpsStat_all_enc_frm_v5 += out_frames;
          }
        }
#endif

#if defined(_V5ENC_REC_FILE_)
        if (recFile != NULL && written > 0) {
          fwrite(packet, sizeof(uint8_t), written, recFile);
        }
#endif
        LOG_VERBOSE( "%s: nb_frame(%d) - written:%d, out_frames:%d", __func__,
            nb_frame, written, out_frames);
        p_buf->len += written;
        all_send_bytes += written;
        nb_frame--;
        written_frame += out_frames;  // added a frame to the buffer
      } else {
        LOG_VERBOSE( "%s: nb_frame(%d) - underflow", __func__, nb_frame);
        a2dp_lhdc_encoder_cb.lhdc_feeding_state.counter +=
            nb_frame * samples_per_frame *
            a2dp_lhdc_encoder_cb.feeding_params.channel_count *
            a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;
        // no more pcm to read
        nb_frame = 0;
      }
    } while ((written == 0) && nb_frame);

    if (p_buf->len) {
      /*
       * Timestamp of the media packet header represent the TS of the
       * first frame, i.e the timestamp before including this frame.
       */
      p_buf->layer_specific = a2dp_lhdc_encoder_cb.buf_seq++;
      p_buf->layer_specific <<= 8;
      p_buf->layer_specific |= ((written_frame << A2DP_LHDC_HDR_NUM_SHIFT));

      *( ( uint32_t*)( p_buf + 1)) = a2dp_lhdc_encoder_cb.timestamp;
      LOG_VERBOSE( "%s: Timestamp (%d)", __func__, a2dp_lhdc_encoder_cb.timestamp);

      a2dp_lhdc_encoder_cb.timestamp += (written_frame * samples_per_frame);

      remain_nb_frame = nb_frame;
      LOG_VERBOSE( "%s: nb_frame(%d) - remain_nb_frame:%d", __func__, (nb_frame+1), remain_nb_frame);

      mtu_usage += ((float)p_buf->len) / max_mtu_len;
      mtu_usage_cnt++;

      LOG_VERBOSE( "%s: Bytes read for pkt(%d)", __func__, a2dp_lhdc_encoder_cb.bytes_read);
      LOG_VERBOSE( "%s: Output frames(%d) encoded pkt len(%d)", __func__, written_frame, p_buf->len);
      bytes_read = a2dp_lhdc_encoder_cb.bytes_read;
      a2dp_lhdc_encoder_cb.bytes_read = 0;

      if (!a2dp_lhdc_encoder_cb.enqueue_callback(p_buf, 1, bytes_read))
        return;
    } else {
      LOG_VERBOSE( "%s: free buffer len(%d)", __func__, p_buf->len);
      a2dp_lhdc_encoder_cb.stats.media_read_total_dropped_packets++;
      osi_free(p_buf);
    }
  }

  // for statistics
  uint64_t now_ms = bluetooth::common::time_get_os_boottime_ms();
  if (now_ms - time_prev >= 1000 ) {
#ifdef MCPS_FRAME_ENCODE
    mcpsStat_time_dur_cnt_v5++;

    if (mcpsStat_time_dur_cnt_v5 <= MCPS_FRAME_ENCODE_DUR) {  //how long we make statistics
      if (mcpsStat_time_dur_cnt_v5 <= 1) {
        mcpsStat_all_time_dur_ms_v5 = 0;  //ignore previous uninitialized records
      } else {
        mcpsStat_all_time_dur_ms_v5 += (now_ms - time_prev);
      }

      /*
       * MCPS STATISTICS LOG:
       *  [cnt:%u now_ms(%u)-time_prev(%u)=(%u) %.2fs]:
       *    cnt       : the number of rounds
       *    now_ms    : now system time point (ms)
       *    time_prev : previous system time point (ms)
       *  [%u %u %d kbps]: sampleRate, bitDepth, realBitrate
       *  frmPerTheSec: number of frames encoded in "this second"
       *  avgFrmPerSec: so far, the average number of encoded frames per second
       *  avgUsPerfrm:  so far, the average execution time of each encoded frame
       *  avgMCPS:  so far, the average *MCPS(million cycles per second)*
       */
      LOG_DEBUG( "[MCPS_STAT]: [cnt:%u now_ms(%u)-time_prev(%u)=(%u) %.2fs] [%u %u %d kbps] frmPerTheSec(%u) avgFrmPerSec(%.2f) avgUsPerfrm:(%.2f)us, avgMCPS:(%.3f)",
          (uint32_t)mcpsStat_time_dur_cnt_v5,
          (uint32_t)now_ms,
          (uint32_t)time_prev,
          (uint32_t)mcpsStat_all_time_dur_ms_v5,
          (float)mcpsStat_all_time_dur_ms_v5 / 1000,            //so far elapsed time (sec)
          a2dp_lhdc_encoder_cb.feeding_params.sample_rate,      //current sample rate
          a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample,  //current bit depth
          (all_send_bytes * 8) / 1000,                          //current bit rate

          (uint32_t)(mcpsStat_all_enc_frm_v5 - mcpsStat_enc_frm_per_sec_v5),              // frmPerTheSec
          ((float)mcpsStat_all_enc_frm_v5 / ((float)mcpsStat_all_time_dur_ms_v5 / 1000)), // avgFrmPerSec
          ((float)mcpsStat_all_enc_us_v5 / (float)mcpsStat_all_enc_frm_v5),               // avgUsPerfrm

          ((float)mcpsStat_all_enc_us_v5 / (float)mcpsStat_all_enc_frm_v5) / STATIS_MILLION_UNIT * STATIS_CPU_FREQ_MHZ *
            ((float)mcpsStat_all_enc_frm_v5 / ((float)mcpsStat_all_time_dur_ms_v5 / 1000)));  // avgMCPS
    }
    mcpsStat_enc_frm_per_sec_v5 = mcpsStat_all_enc_frm_v5;
#endif
    LOG_DEBUG( "%s: current data rate about %d kbps, packet usage %.2f%%", __func__,
        (all_send_bytes * 8) / 1000, (mtu_usage * 100)/mtu_usage_cnt);
    all_send_bytes = 0;
    mtu_usage_cnt = 0;
    mtu_usage = 0;
    time_prev = now_ms;
  }
}

static bool a2dp_lhdcv5_read_feeding(uint8_t* read_buffer, uint32_t *bytes_read) {
  uint32_t read_size = 0;
  uint32_t samples_per_frame = 0;
  uint32_t bytes_per_sample =
      a2dp_lhdc_encoder_cb.feeding_params.channel_count *
      a2dp_lhdc_encoder_cb.feeding_params.bits_per_sample / 8;
  uint32_t nb_byte_read;

  if (read_buffer == nullptr || bytes_read == nullptr) {
    LOG_ERROR( "%s: null input", __func__);
    return false;
  }

  int32_t lib_ret = lhdc_get_block_size(a2dp_lhdc_encoder_cb.lhdc_handle, &samples_per_frame);
  if (lib_ret != LHDCV5_FRET_SUCCESS) {
    LOG_DEBUG( "%s: [lib_ret] lhdc_get_block_size error %d", __func__, lib_ret);
    return false;
  }
  read_size = samples_per_frame * bytes_per_sample;

  a2dp_lhdc_encoder_cb.stats.media_read_total_expected_reads_count++;
  a2dp_lhdc_encoder_cb.stats.media_read_total_expected_read_bytes += read_size;

  /* Read Data from UIPC channel */
  nb_byte_read =
      a2dp_lhdc_encoder_cb.read_callback(read_buffer, read_size);
  LOG_VERBOSE( "%s: expected read bytes %u, actual read bytes %u",
      __func__, read_size, nb_byte_read);

  //TODO: what to do if not alignment?
  if ((nb_byte_read % bytes_per_sample) != 0) {
    LOG_DEBUG( "%s: PCM data not alignment. The audio sample is shfit %d bytes!",
        __func__, (nb_byte_read % bytes_per_sample));
  }
  a2dp_lhdc_encoder_cb.stats.media_read_total_actual_read_bytes += nb_byte_read;

  // if actual read < want to read
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

// library index mapping: quality mode index
static std::string quality_mode_index_to_name(uint32_t quality_mode_index) {
  switch (quality_mode_index) {
  case LHDCV5_QUALITY_AUTO:
    return "ABR";
  case LHDCV5_QUALITY_HIGH1:
    return "HIGH1_1000";
  case LHDCV5_QUALITY_HIGH:
    return "HIGH_900";
  case LHDCV5_QUALITY_MID:
    return "MID_500";
  case LHDCV5_QUALITY_LOW:
    return "LOW_400";
  case LHDCV5_QUALITY_LOW4:
    return "LOW_320";
  case LHDCV5_QUALITY_LOW3:
    return "LOW_256";
  case LHDCV5_QUALITY_LOW2:
    return "LOW_192";
  case LHDCV5_QUALITY_LOW1:
    return "LOW_128";
  case LHDCV5_QUALITY_LOW0:
    return "LOW_64";
  default:
    return "Unknown";
  }
}

//tA2DP_ENCODER_INTERFACE::(set_transmit_queue_length)
void a2dp_vendor_lhdcv5_set_transmit_queue_length(size_t transmit_queue_length) {
  int32_t lib_ret = 0;

  a2dp_lhdc_encoder_cb.TxQueueLength = transmit_queue_length;
  tA2DP_LHDCV5_ENCODER_PARAMS* p_encoder_params = &a2dp_lhdc_encoder_cb.lhdc_encoder_params;

  if (p_encoder_params->quality_mode_index == LHDCV5_QUALITY_AUTO) {
    if (lhdc_auto_adjust_bitrate != NULL) {
      lib_ret = lhdc_auto_adjust_bitrate(a2dp_lhdc_encoder_cb.lhdc_handle, (uint32_t)transmit_queue_length);
      if (lib_ret != LHDCV5_FRET_SUCCESS){
        LOG_ERROR( "%s: [lib_ret] lhdc_auto_adjust_bitrate error %d", __func__, lib_ret);
      }
    }
  }
}

#if 0
uint64_t A2dpCodecConfigLhdcV5Source::encoderIntervalMs() const {
  return a2dp_vendor_lhdcv5_get_encoder_interval_ms();
}
#endif

#if 0
int A2dpCodecConfigLhdcV5Source::getEffectiveMtu() const {
  return a2dp_lhdc_encoder_cb.TxAaMtuSize;
}
#endif

void A2dpCodecConfigLhdcV5Source::debug_codec_dump(int fd) {
  a2dp_lhdcv5_encoder_stats_t* stats = &a2dp_lhdc_encoder_cb.stats;
  tA2DP_LHDCV5_ENCODER_PARAMS* p_encoder_params =
      &a2dp_lhdc_encoder_cb.lhdc_encoder_params;

  uint32_t lib_value = 0;
  int32_t lib_ret = 0;

  A2dpCodecConfig::debug_codec_dump(fd);

  dprintf(fd,
      "  Packet counts (expected/dropped)                        : %zu / "
      "%zu\n",
      (size_t)stats->media_read_total_expected_packets,
      (size_t)stats->media_read_total_dropped_packets);

  dprintf(fd,
      "  PCM read counts (expected/actual)                       : %zu / "
      "%zu\n",
      (size_t)stats->media_read_total_expected_reads_count,
      (size_t)stats->media_read_total_actual_reads_count);

  dprintf(fd,
      "  PCM read bytes (expected/actual)                        : %zu / "
      "%zu\n",
      (size_t)stats->media_read_total_expected_read_bytes,
      (size_t)stats->media_read_total_actual_read_bytes);

  dprintf(fd,
      "  LHDC quality mode                                       : %s\n",
      quality_mode_index_to_name(p_encoder_params->quality_mode_index).c_str());

  lib_ret = lhdc_get_bitrate(a2dp_lhdc_encoder_cb.lhdc_handle, &lib_value);
  if (lib_ret == LHDCV5_FRET_SUCCESS) {
    dprintf(fd,
        "  LHDC transmission bitrate (Kbps)                        : %d\n",
        lib_value);
  }

  dprintf(fd,
      "  LHDC saved transmit queue length                        : %zu\n",
      (size_t)a2dp_lhdc_encoder_cb.TxQueueLength);
}

