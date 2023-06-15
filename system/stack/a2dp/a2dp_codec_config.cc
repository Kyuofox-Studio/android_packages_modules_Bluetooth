/*
 * Copyright 2016 The Android Open Source Project
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

/**
 * A2DP Codecs Configuration
 */

#define LOG_TAG "a2dp_codec"

#include "a2dp_codec_api.h"

#include <base/logging.h>
#include <inttypes.h>

#include "a2dp_aac.h"
#include "a2dp_sbc.h"
#include "a2dp_vendor.h"

#if !defined(EXCLUDE_NONSTANDARD_CODECS)
#include "a2dp_vendor_aptx.h"
#include "a2dp_vendor_aptx_hd.h"
#include "a2dp_vendor_ldac.h"
#include "a2dp_vendor_lhdcv2.h"
#include "a2dp_vendor_lhdcv3.h"
#include "a2dp_vendor_lhdcv3_dec.h"
#include "a2dp_vendor_lhdcv5.h"
#endif

#include "bta/av/bta_av_int.h"
#include "osi/include/log.h"
#include "osi/include/properties.h"
#include "stack/include/bt_hdr.h"

/* The Media Type offset within the codec info byte array */
#define A2DP_MEDIA_TYPE_OFFSET 1

/* A2DP Offload enabled in stack */
static bool a2dp_offload_status;

// Initializes the codec config.
// |codec_config| is the codec config to initialize.
// |codec_index| and |codec_priority| are the codec type and priority to use
// for the initialization.

static void init_btav_a2dp_codec_config(
    btav_a2dp_codec_config_t* codec_config, btav_a2dp_codec_index_t codec_index,
    btav_a2dp_codec_priority_t codec_priority) {
  memset(codec_config, 0, sizeof(btav_a2dp_codec_config_t));
  codec_config->codec_type = codec_index;
  codec_config->codec_priority = codec_priority;
}

A2dpCodecConfig::A2dpCodecConfig(btav_a2dp_codec_index_t codec_index,
                                 const std::string& name,
                                 btav_a2dp_codec_priority_t codec_priority)
    : codec_index_(codec_index),
      name_(name),
      default_codec_priority_(codec_priority) {
  setCodecPriority(codec_priority);

  init_btav_a2dp_codec_config(&codec_config_, codec_index_, codecPriority());
  init_btav_a2dp_codec_config(&codec_capability_, codec_index_,
                              codecPriority());
  init_btav_a2dp_codec_config(&codec_local_capability_, codec_index_,
                              codecPriority());
  init_btav_a2dp_codec_config(&codec_selectable_capability_, codec_index_,
                              codecPriority());
  init_btav_a2dp_codec_config(&codec_user_config_, codec_index_,
                              BTAV_A2DP_CODEC_PRIORITY_DEFAULT);
  init_btav_a2dp_codec_config(&codec_audio_config_, codec_index_,
                              BTAV_A2DP_CODEC_PRIORITY_DEFAULT);

  memset(ota_codec_config_, 0, sizeof(ota_codec_config_));
  memset(ota_codec_peer_capability_, 0, sizeof(ota_codec_peer_capability_));
  memset(ota_codec_peer_config_, 0, sizeof(ota_codec_peer_config_));
}

A2dpCodecConfig::~A2dpCodecConfig() {}

void A2dpCodecConfig::setCodecPriority(
    btav_a2dp_codec_priority_t codec_priority) {
  if (codec_priority == BTAV_A2DP_CODEC_PRIORITY_DEFAULT) {
    // Compute the default codec priority
    setDefaultCodecPriority();
  } else {
    codec_priority_ = codec_priority;
  }
  codec_config_.codec_priority = codec_priority_;
}

void A2dpCodecConfig::setDefaultCodecPriority() {
  if (default_codec_priority_ != BTAV_A2DP_CODEC_PRIORITY_DEFAULT) {
    codec_priority_ = default_codec_priority_;
  } else {
    // Compute the default codec priority
    uint32_t priority = 1000 * (codec_index_ + 1) + 1;
    codec_priority_ = static_cast<btav_a2dp_codec_priority_t>(priority);
  }
  codec_config_.codec_priority = codec_priority_;
}

A2dpCodecConfig* A2dpCodecConfig::createCodec(
    btav_a2dp_codec_index_t codec_index,
    btav_a2dp_codec_priority_t codec_priority) {
  LOG_INFO("%s: codec %s", __func__, A2DP_CodecIndexStr(codec_index));

  A2dpCodecConfig* codec_config = nullptr;
  switch (codec_index) {
    case BTAV_A2DP_CODEC_INDEX_SOURCE_SBC:
      codec_config = new A2dpCodecConfigSbcSource(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SINK_SBC:
      codec_config = new A2dpCodecConfigSbcSink(codec_priority);
      break;
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case BTAV_A2DP_CODEC_INDEX_SOURCE_AAC:
      codec_config = new A2dpCodecConfigAacSource(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SINK_AAC:
      codec_config = new A2dpCodecConfigAacSink(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SOURCE_APTX:
      codec_config = new A2dpCodecConfigAptx(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SOURCE_APTX_HD:
      codec_config = new A2dpCodecConfigAptxHd(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SOURCE_LDAC:
      codec_config = new A2dpCodecConfigLdacSource(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SINK_LDAC:
      codec_config = new A2dpCodecConfigLdacSink(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV2:
      codec_config = new A2dpCodecConfigLhdcV2(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV3:
      codec_config = new A2dpCodecConfigLhdcV3(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV5:
      codec_config = new A2dpCodecConfigLhdcV5Source(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SINK_LHDCV3:
      codec_config = new A2dpCodecConfigLhdcV3Sink(codec_priority);
      break;
    case BTAV_A2DP_CODEC_INDEX_SINK_LHDCV5:
      codec_config = new A2dpCodecConfigLhdcV5Sink(codec_priority);
      break;
#endif
    case BTAV_A2DP_CODEC_INDEX_MAX:
    default:
      break;
  }

  if (codec_config != nullptr) {
    if (!codec_config->init()) {
      delete codec_config;
      codec_config = nullptr;
    }
  }

  return codec_config;
}

int A2dpCodecConfig::getTrackBitRate() const {
  uint8_t p_codec_info[AVDT_CODEC_SIZE];
  memcpy(p_codec_info, ota_codec_config_, sizeof(ota_codec_config_));
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_GetBitrateSbc();
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_GetBitRateAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorGetBitRate(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return -1;
}

bool A2dpCodecConfig::getCodecSpecificConfig(tBT_A2DP_OFFLOAD* p_a2dp_offload) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  uint8_t codec_config[AVDT_CODEC_SIZE];
  uint32_t vendor_id;
  uint16_t codec_id;

  memset(p_a2dp_offload->codec_info, 0, sizeof(p_a2dp_offload->codec_info));

  if (!A2DP_IsSourceCodecValid(ota_codec_config_)) {
    return false;
  }

  memcpy(codec_config, ota_codec_config_, sizeof(ota_codec_config_));
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(codec_config);
  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      p_a2dp_offload->codec_info[0] =
          codec_config[4];  // blk_len | subbands | Alloc Method
      p_a2dp_offload->codec_info[1] = codec_config[5];  // Min bit pool
      p_a2dp_offload->codec_info[2] = codec_config[6];  // Max bit pool
      p_a2dp_offload->codec_info[3] =
          codec_config[3];  // Sample freq | channel mode
      break;
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      p_a2dp_offload->codec_info[0] = codec_config[3];  // object type
      p_a2dp_offload->codec_info[1] = codec_config[6];  // VBR | BR
      break;
    case A2DP_MEDIA_CT_NON_A2DP:
      vendor_id = A2DP_VendorCodecGetVendorId(codec_config);
      codec_id = A2DP_VendorCodecGetCodecId(codec_config);
      p_a2dp_offload->codec_info[0] = (vendor_id & 0x000000FF);
      p_a2dp_offload->codec_info[1] = (vendor_id & 0x0000FF00) >> 8;
      p_a2dp_offload->codec_info[2] = (vendor_id & 0x00FF0000) >> 16;
      p_a2dp_offload->codec_info[3] = (vendor_id & 0xFF000000) >> 24;
      p_a2dp_offload->codec_info[4] = (codec_id & 0x000000FF);
      p_a2dp_offload->codec_info[5] = (codec_id & 0x0000FF00) >> 8;
      if (vendor_id == A2DP_LDAC_VENDOR_ID && codec_id == A2DP_LDAC_CODEC_ID) {
        if (codec_config_.codec_specific_1 == 0) {  // default is 0, ABR
          p_a2dp_offload->codec_info[6] =
              A2DP_LDAC_QUALITY_ABR_OFFLOAD;  // ABR in offload
        } else {
          switch (codec_config_.codec_specific_1 % 10) {
            case 0:
              p_a2dp_offload->codec_info[6] =
                  A2DP_LDAC_QUALITY_HIGH;  // High bitrate
              break;
            case 1:
              p_a2dp_offload->codec_info[6] =
                  A2DP_LDAC_QUALITY_MID;  // Mid birate
              break;
            case 2:
              p_a2dp_offload->codec_info[6] =
                  A2DP_LDAC_QUALITY_LOW;  // Low birate
              break;
            case 3:
              FALLTHROUGH_INTENDED; /* FALLTHROUGH */
            default:
              p_a2dp_offload->codec_info[6] =
                  A2DP_LDAC_QUALITY_ABR_OFFLOAD;  // ABR in offload
              break;
          }
        }
        p_a2dp_offload->codec_info[7] =
            codec_config[10];  // LDAC specific channel mode
        LOG_VERBOSE("%s: Ldac specific channelmode =%d", __func__,
                    p_a2dp_offload->codec_info[7]);
      }
      // Savitech Patch - START  Offload
      else if (vendor_id == A2DP_LHDC_VENDOR_ID && codec_id == A2DP_LHDCV3_CODEC_ID) {
        //
        // LHDC V3
        //
        // Main Version
        if ((codec_config[10] & A2DP_LHDC_VERSION_MASK) != A2DP_LHDC_VER3 &&
            (codec_config[10] & A2DP_LHDC_VERSION_MASK) != A2DP_LHDC_VER6) {
          LOG_ERROR("%s: [LHDC V3] Unsupported version 0x%x", __func__,
              (codec_config[10] & A2DP_LHDC_VERSION_MASK));
          goto fail;
        }

        LOG_DEBUG("%s: [LHDC V3] isLLAC=%d isLHDCV4=%d", __func__,
            (codec_config[10] & A2DP_LHDC_FEATURE_LLAC),
            (codec_config[11] & A2DP_LHDC_FEATURE_LHDCV4));
        // LHDC/LLAC handle Version
        if ((codec_config[10] & A2DP_LHDC_FEATURE_LLAC) != 0 &&
            (codec_config[11] & A2DP_LHDC_FEATURE_LHDCV4) == 0) {
          // LLAC (isLLAC && !isLHDCV4)
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER] = 1 << (A2DP_OFFLOAD_LHDCV3_LLAC-1);
          LOG_DEBUG("%s: [LHDC V3] init to LLAC (0x%02X)", __func__,
              p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER]);
        } else if ((codec_config[10] & A2DP_LHDC_FEATURE_LLAC) == 0 &&
            (codec_config[11] & A2DP_LHDC_FEATURE_LHDCV4) != 0) {
          // LHDC V4 Only (!isLLAC && isLHDCV4)
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER] = 1 << (A2DP_OFFLOAD_LHDCV3_V4_ONLY-1);
          LOG_DEBUG("%s: [LHDC V3] init to LHDCV4 only (0x%02X)", __func__,
              p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER]);
        } else if ((codec_config[10] & A2DP_LHDC_FEATURE_LLAC) == 0 &&
            (codec_config[11] & A2DP_LHDC_FEATURE_LHDCV4) == 0) {
          // LHDC V3 Only (!isLLAC && !isLHDCV4)
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER] = 1 << (A2DP_OFFLOAD_LHDCV3_V3_ONLY-1);
          LOG_DEBUG("%s: [LHDC V3] init to LHDCV3 only (0x%02X)", __func__,
              p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER]);
        }else {
          // LHDC V3 Only - default
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER] = 1 << (A2DP_OFFLOAD_LHDCV3_V3_ONLY-1);
          LOG_DEBUG("%s: [LHDC V3] flags check incorrect. So init to LHDCV3 only (0x%02X)", __func__,
              p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER]);
        }

        // bit rate index
        switch (codec_config_.codec_specific_1 & 0x0F) {
        case A2DP_LHDC_QUALITY_LOW0:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW0) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW0) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW1:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW2:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW2) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW2) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW3:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW3) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW3) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW4:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW4) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW4) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_MID:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_HIGH:
        case A2DP_LHDC_QUALITY_HIGH1: //HIGH1 not supported in LHDC V3
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_ABR:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) >> 8) & ((uint16_t) 0xFF));
          break;
        default:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) >> 8) & ((uint16_t) 0xFF));
          break;
        }
        LOG_DEBUG("%s: [LHDC V3] Bit Rate = 0x%02X", __func__,
            (uint8_t)(codec_config_.codec_specific_1 & 0x0F));

        // max bit rate index
        if ((codec_config[10] & A2DP_LHDC_MAX_BIT_RATE_MASK) == A2DP_LHDC_MAX_BIT_RATE_400K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) >> 8) & ((uint16_t) 0xFF));
        } else if ((codec_config[10] & A2DP_LHDC_MAX_BIT_RATE_MASK) == A2DP_LHDC_MAX_BIT_RATE_500K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) >> 8) & ((uint16_t) 0xFF));
        } else {  //default option: A2DP_LHDC_MAX_BIT_RATE_900K
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) >> 8) & ((uint16_t) 0xFF));
        }
        LOG_DEBUG("%s: [LHDC V3] Max Bit Rate = 0x%02X", __func__,
            codec_config[10] & A2DP_LHDC_MAX_BIT_RATE_MASK);

        // min bit rate index
        if ((codec_config[11] & A2DP_LHDC_FEATURE_MIN_BR) == A2DP_LHDC_FEATURE_MIN_BR) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW4) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW4) >> 8) & ((uint16_t) 0xFF));
        } else {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) >> 8) & ((uint16_t) 0xFF));
        }
        LOG_DEBUG("%s: [LHDC V3] Min Bit Rate = 0x%02X", __func__,
            codec_config[11] & A2DP_LHDC_FEATURE_MIN_BR);

        // frameDuration - not supported
        // p_a2dp_offload->codec_info[13]

        // data interval
        if ((codec_config[10] & A2DP_LHDC_LL_MASK) != 0) {
          // LL is enabled (10 ms)
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_INTERVAL] = A2DP_OFFLOAD_LHDC_DATA_INTERVAL_10MS;
          LOG_DEBUG("%s: [LHDC V3] Low Latency mode", __func__);
        } else {
          // LL is disabled (20ms)
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_INTERVAL] = A2DP_OFFLOAD_LHDC_DATA_INTERVAL_20MS;
          LOG_DEBUG("%s: [LHDC V3] Normal Latency mode", __func__);
        }

        // Codec specific 1
        if ((codec_config[9] & A2DP_LHDC_FEATURE_AR) != 0) {
          // AR
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC1] |= A2DP_OFFLOAD_LHDC_SPECIFIC_FEATURE_AR;
          LOG_DEBUG("%s: [LHDC V3] Has feature AR", __func__);
        }
        if ((codec_config[9] & A2DP_LHDC_FEATURE_JAS) != 0) {
          // JAS
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC1] |= A2DP_OFFLOAD_LHDC_SPECIFIC_FEATURE_JAS;
          LOG_DEBUG("%s: [LHDC V3] Has feature JAS", __func__);
        }
        if ((codec_config[11] & A2DP_LHDC_FEATURE_META) != 0) {
          // META
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC1] |= A2DP_OFFLOAD_LHDC_SPECIFIC_FEATURE_META;
          LOG_DEBUG("%s: [LHDC V3] Has feature META", __func__);
        }

        // Codec specific 2
        if ((codec_config[11] & A2DP_LHDC_CH_SPLIT_MSK) == A2DP_LHDC_CH_SPLIT_NONE) {
          // split mode disabled
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC2] = 0;
          LOG_DEBUG("%s: [LHDC V3] No ch split", __func__);
        } else if ((codec_config[11] & A2DP_LHDC_CH_SPLIT_MSK) == A2DP_LHDC_CH_SPLIT_TWS) {
          // split mode enabled
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC2] |= A2DP_OFFLOAD_LHDC_SPECIFIC_FEATURE_SPLIT;
          LOG_DEBUG("%s: [LHDC V3] Has ch split", __func__);
        } else {
          LOG_ERROR("%s: [LHDC V3] Unsupported split mode 0x%x", __func__,
              codec_config[11] & A2DP_LHDC_CH_SPLIT_MSK);
          goto fail;
        }

      } else if (vendor_id == A2DP_LHDC_VENDOR_ID && codec_id == A2DP_LHDCV2_CODEC_ID) {
        //
        // LHDC V2
        //
        // Version
        if ((codec_config[10] & A2DP_LHDC_VERSION_MASK) > A2DP_LHDC_VER2) {
          LOG_ERROR("%s: [LHDC V2] Unsupported version 0x%x", __func__,
              (codec_config[10] & A2DP_LHDC_VERSION_MASK));
          goto fail;
        }
        p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER] = 1 << (A2DP_OFFLOAD_LHDCV2_VER_1-1);
        LOG_DEBUG("%s: [LHDC V2] version (0x%02X)", __func__,
            p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER]);

        // bit rate index
        switch (codec_config_.codec_specific_1 & 0x0F) {
        case A2DP_LHDC_QUALITY_LOW0:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW0) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW0) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW1:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW2:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW2) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW2) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW3:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW3) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW3) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW4:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW4) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW4) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_MID:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_HIGH:
        case A2DP_LHDC_QUALITY_HIGH1: //HIGH1 not supported in LHDC V2
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_ABR:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) >> 8) & ((uint16_t) 0xFF));
          break;
        default:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) >> 8) & ((uint16_t) 0xFF));
          break;
        }
        LOG_DEBUG("%s: [LHDC V2] Bit Rate = 0x%02X", __func__,
            (uint8_t)codec_config_.codec_specific_1 & 0x0F);

        // max bit rate index
        if ((codec_config[10] & A2DP_LHDC_MAX_BIT_RATE_MASK) == A2DP_LHDC_MAX_BIT_RATE_400K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) >> 8) & ((uint16_t) 0xFF));
        } else if ((codec_config[10] & A2DP_LHDC_MAX_BIT_RATE_MASK) == A2DP_LHDC_MAX_BIT_RATE_500K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) >> 8) & ((uint16_t) 0xFF));
        } else {  //default option: A2DP_LHDC_MAX_BIT_RATE_900K
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) >> 8) & ((uint16_t) 0xFF));
        }
        LOG_DEBUG("%s: [LHDC V2] Max Bit Rate = 0x%02X", __func__,
            codec_config[10] & A2DP_LHDC_MAX_BIT_RATE_MASK);

        // min bit rate index - not supported
        //p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_L]
        //p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_H]

        // frameDuration - not supported
        //p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_FRAMEDUR]

        // data interval
        if ((codec_config[10] & A2DP_LHDC_LL_MASK) != 0) {
          // LL is enabled (10 ms)
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_INTERVAL] = A2DP_OFFLOAD_LHDC_DATA_INTERVAL_10MS;
          LOG_DEBUG("%s: [LHDC V2] Low Latency mode", __func__);
        } else {
          // LL is disabled (20ms)
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_INTERVAL] = A2DP_OFFLOAD_LHDC_DATA_INTERVAL_20MS;
          LOG_DEBUG("%s: [LHDC V2] Normal Latency mode", __func__);
        }

        // Codec specific 1 - not supported
        // p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC1]

        // Codec specific 2
        if ((codec_config[11] & A2DP_LHDC_CH_SPLIT_MSK) == A2DP_LHDC_CH_SPLIT_NONE) {
          // split mode disabled
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC2] = 0;
          LOG_DEBUG("%s: [LHDC V2] No ch split", __func__);
        } else if ((codec_config[11] & A2DP_LHDC_CH_SPLIT_MSK) == A2DP_LHDC_CH_SPLIT_TWS) {
          // split mode enabled
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC2] |= A2DP_OFFLOAD_LHDC_SPECIFIC_FEATURE_SPLIT;
          LOG_DEBUG("%s: [LHDC V2] Has ch split", __func__);
        } else {
          LOG_ERROR("%s: [LHDC V2] Unsupported split mode 0x%x", __func__,
              codec_config[11] & A2DP_LHDC_CH_SPLIT_MSK);
          goto fail;
        }
      } else if (vendor_id == A2DP_LHDC_VENDOR_ID && codec_id == A2DP_LHDCV5_CODEC_ID) {
        //
        // LHDC V5
        //

        // Version
        if ((codec_config[11] & A2DP_LHDCV5_VERSION_MASK) != A2DP_LHDCV5_VER_1) {
          LOG_ERROR("%s: [LHDC V5] unsupported version 0x%x", __func__,
              (codec_config[11] & A2DP_LHDCV5_VERSION_MASK));
          goto fail;
        }
        p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER] = 1 << (A2DP_OFFLOAD_LHDCV5_VER_1-1);
        LOG_DEBUG("%s: [LHDC V5] version (0x%02X)", __func__,
            p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_VER]);

        // bit rate index
        //TODO: Lossless: (VBR, HIGH2 ~ HIGH5)
        switch (codec_config_.codec_specific_1 & 0xF) {
        case A2DP_LHDC_QUALITY_LOW0:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW0) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW0) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW1:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW2:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW2) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW2) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW3:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW3) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW3) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW4:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW4) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW4) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_LOW:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_MID:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_HIGH:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_HIGH1:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH1) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH1) >> 8) & ((uint16_t) 0xFF));
          break;
        case A2DP_LHDC_QUALITY_ABR:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) >> 8) & ((uint16_t) 0xFF));
          break;
        default:
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_BITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_ABR) >> 8) & ((uint16_t) 0xFF));
          break;
        }
        LOG_DEBUG("%s: [LHDC V5] Bit Rate = 0x%02X", __func__,
            (uint8_t)codec_config_.codec_specific_1 & 0x0F);

        // max bit rate index
        if ((codec_config[10] & A2DP_LHDCV5_MAX_BIT_RATE_MASK) == A2DP_LHDCV5_MAX_BIT_RATE_400K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) >> 8) & ((uint16_t) 0xFF));
        } else if ((codec_config[10] & A2DP_LHDCV5_MAX_BIT_RATE_MASK) == A2DP_LHDCV5_MAX_BIT_RATE_500K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_MID) >> 8) & ((uint16_t) 0xFF));
        } else if ((codec_config[10] & A2DP_LHDCV5_MAX_BIT_RATE_MASK) == A2DP_LHDCV5_MAX_BIT_RATE_900K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH) >> 8) & ((uint16_t) 0xFF));
        } else {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH1) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MAXBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_HIGH1) >> 8) & ((uint16_t) 0xFF));
        }
        LOG_DEBUG("%s: [LHDC V5] Max Bit Rate = 0x%02X", __func__,
            codec_config[10] & A2DP_LHDCV5_MAX_BIT_RATE_MASK);

        // min bit rate index
        if ((codec_config[10] & A2DP_LHDCV5_MIN_BIT_RATE_MASK) == A2DP_LHDCV5_MIN_BIT_RATE_64K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW0) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW0) >> 8) & ((uint16_t) 0xFF));
        } else if ((codec_config[10] & A2DP_LHDCV5_MIN_BIT_RATE_MASK) == A2DP_LHDCV5_MIN_BIT_RATE_128K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW1) >> 8) & ((uint16_t) 0xFF));
        } else if ((codec_config[10] & A2DP_LHDCV5_MIN_BIT_RATE_MASK) == A2DP_LHDCV5_MIN_BIT_RATE_256K) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW3) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW3) >> 8) & ((uint16_t) 0xFF));
        } else {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_L] = (uint8_t) (((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) & ((uint16_t) 0xFF));
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_MINBITRATE_H] = (uint8_t) ((((uint16_t) A2DP_OFFLOAD_LHDC_QUALITY_LOW) >> 8) & ((uint16_t) 0xFF));
        }
        LOG_DEBUG("%s: [LHDC V5] Min Bit Rate = 0x%02X", __func__,
            codec_config[10] & A2DP_LHDCV5_MIN_BIT_RATE_MASK);

        // frame duration
        if ((codec_config[11] & A2DP_LHDCV5_FRAME_LEN_MASK) != 0) {
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_FRAMEDUR] = A2DP_OFFLOAD_LHDC_FRAME_DURATION_5000US;
          LOG_DEBUG("%s: [LHDC V5] Frame Duration: 5ms ", __func__);
        } else {
          LOG_ERROR("%s: [LHDC V5] unsupported frame duration 0x%x", __func__,
              codec_config[11] & A2DP_LHDCV5_FRAME_LEN_MASK);
          goto fail;
        }

        // data interval
        if ((codec_config[12] & A2DP_LHDCV5_FEATURE_LL) != 0) {
          // LL is disabled (10 ms)
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_INTERVAL] = A2DP_OFFLOAD_LHDC_DATA_INTERVAL_10MS;
          LOG_DEBUG("%s: [LHDC V5] Low Latency mode", __func__);
        } else {
          // LL is enabled (20ms)
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_INTERVAL] = A2DP_OFFLOAD_LHDC_DATA_INTERVAL_20MS;
          LOG_DEBUG("%s: [LHDC V5] Normal Latency mode", __func__);
        }

        // Codec specific 1
        if ((codec_config[12] & A2DP_LHDCV5_FEATURE_AR) != 0) {
          // AR
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC1] |= A2DP_OFFLOAD_LHDC_SPECIFIC_FEATURE_AR;
          LOG_DEBUG("%s: [LHDC V5] Has feature AR", __func__);
        }
        if ((codec_config[12] & A2DP_LHDCV5_FEATURE_JAS) != 0) {
          // JAS
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC1] |= A2DP_OFFLOAD_LHDC_SPECIFIC_FEATURE_JAS;
          LOG_DEBUG("%s: [LHDC V5] Has feature JAS", __func__);
        }
        if ((codec_config[12] & A2DP_LHDCV5_FEATURE_META) != 0) {
          // META
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC1] |= A2DP_OFFLOAD_LHDC_SPECIFIC_FEATURE_META;
          LOG_DEBUG("%s: [LHDC V5] Has feature META", __func__);
        }

        // Codec specific 2
        if ((codec_config[13] & A2DP_LHDCV5_AR_ON) != 0) {
          // AR_ON
          p_a2dp_offload->codec_info[A2DP_OFFLOAD_LHDC_CFG_SPEC2] |= A2DP_OFFLOAD_LHDC_SPECIFIC_ACTION_AR_ON;
          LOG_DEBUG("%s: [LHDC V5] AR_ON is set", __func__);
        }
      }
      // Savitech Patch - END
      break;
#endif
    default:
      break;
  }
  return true;

fail:
  return false;
}

bool A2dpCodecConfig::isValid() const { return true; }

bool A2dpCodecConfig::copyOutOtaCodecConfig(uint8_t* p_codec_info) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  // TODO: We should use a mechanism to verify codec config,
  // not codec capability.
  if (!A2DP_IsSourceCodecValid(ota_codec_config_)) {
    return false;
  }
  memcpy(p_codec_info, ota_codec_config_, sizeof(ota_codec_config_));
  return true;
}

btav_a2dp_codec_config_t A2dpCodecConfig::getCodecConfig() {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  // TODO: We should check whether the codec config is valid
  return codec_config_;
}

btav_a2dp_codec_config_t A2dpCodecConfig::getCodecCapability() {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  // TODO: We should check whether the codec capability is valid
  return codec_capability_;
}

btav_a2dp_codec_config_t A2dpCodecConfig::getCodecLocalCapability() {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  // TODO: We should check whether the codec capability is valid
  return codec_local_capability_;
}

btav_a2dp_codec_config_t A2dpCodecConfig::getCodecSelectableCapability() {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  // TODO: We should check whether the codec capability is valid
  return codec_selectable_capability_;
}

btav_a2dp_codec_config_t A2dpCodecConfig::getCodecUserConfig() {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  return codec_user_config_;
}

btav_a2dp_codec_config_t A2dpCodecConfig::getCodecAudioConfig() {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  return codec_audio_config_;
}

uint8_t A2dpCodecConfig::getAudioBitsPerSample() {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  switch (codec_config_.bits_per_sample) {
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
      return 16;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
      return 24;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
      return 32;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE:
      break;
  }
  return 0;
}

bool A2dpCodecConfig::isCodecConfigEmpty(
    const btav_a2dp_codec_config_t& codec_config) {
  return (
      (codec_config.codec_priority == BTAV_A2DP_CODEC_PRIORITY_DEFAULT) &&
      (codec_config.sample_rate == BTAV_A2DP_CODEC_SAMPLE_RATE_NONE) &&
      (codec_config.bits_per_sample == BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE) &&
      (codec_config.channel_mode == BTAV_A2DP_CODEC_CHANNEL_MODE_NONE) &&
      (codec_config.codec_specific_1 == 0) &&
      (codec_config.codec_specific_2 == 0) &&
      (codec_config.codec_specific_3 == 0) &&
      (codec_config.codec_specific_4 == 0));
}

bool A2dpCodecConfig::setCodecUserConfig(
    const btav_a2dp_codec_config_t& codec_user_config,
    const btav_a2dp_codec_config_t& codec_audio_config,
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
    const uint8_t* p_peer_codec_info, bool is_capability,
    uint8_t* p_result_codec_config, bool* p_restart_input,
    bool* p_restart_output, bool* p_config_updated) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  *p_restart_input = false;
  *p_restart_output = false;
  *p_config_updated = false;

  // Save copies of the current codec config, and the OTA codec config, so they
  // can be compared for changes.
  btav_a2dp_codec_config_t saved_codec_config = getCodecConfig();
  uint8_t saved_ota_codec_config[AVDT_CODEC_SIZE];
  memcpy(saved_ota_codec_config, ota_codec_config_, sizeof(ota_codec_config_));

  btav_a2dp_codec_config_t saved_codec_user_config = codec_user_config_;
  codec_user_config_ = codec_user_config;
  btav_a2dp_codec_config_t saved_codec_audio_config = codec_audio_config_;
  codec_audio_config_ = codec_audio_config;
  bool success =
      setCodecConfig(p_peer_codec_info, is_capability, p_result_codec_config);
  if (!success) {
    // Restore the local copy of the user and audio config
    codec_user_config_ = saved_codec_user_config;
    codec_audio_config_ = saved_codec_audio_config;
    return false;
  }

  //
  // The input (audio data) should be restarted if the audio format has changed
  //
  btav_a2dp_codec_config_t new_codec_config = getCodecConfig();
  if ((saved_codec_config.sample_rate != new_codec_config.sample_rate) ||
      (saved_codec_config.bits_per_sample != new_codec_config.bits_per_sample) ||
      (saved_codec_config.channel_mode != new_codec_config.channel_mode) ||
      (saved_codec_config.codec_specific_1 != new_codec_config.codec_specific_1) || // Savitech LHDC
      (saved_codec_config.codec_specific_2 != new_codec_config.codec_specific_2) ||
      (saved_codec_config.codec_specific_3 != new_codec_config.codec_specific_3)) {
    *p_restart_input = true;
  }

  //
  // The output (the connection) should be restarted if OTA codec config
  // has changed.
  //
  if (!A2DP_CodecEquals(saved_ota_codec_config, p_result_codec_config)) {
    *p_restart_output = true;
  }

  if (*p_restart_input || *p_restart_output) *p_config_updated = true;

  return true;
}

bool A2dpCodecConfig::codecConfigIsValid(
    const btav_a2dp_codec_config_t& codec_config) {
  return (codec_config.codec_type < BTAV_A2DP_CODEC_INDEX_MAX) &&
         (codec_config.sample_rate != BTAV_A2DP_CODEC_SAMPLE_RATE_NONE) &&
         (codec_config.bits_per_sample !=
          BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE) &&
         (codec_config.channel_mode != BTAV_A2DP_CODEC_CHANNEL_MODE_NONE);
}

std::string A2dpCodecConfig::codecConfig2Str(
    const btav_a2dp_codec_config_t& codec_config) {
  std::string result;

  if (!codecConfigIsValid(codec_config)) return "Invalid";

  result.append("Rate=");
  result.append(codecSampleRate2Str(codec_config.sample_rate));
  result.append(" Bits=");
  result.append(codecBitsPerSample2Str(codec_config.bits_per_sample));
  result.append(" Mode=");
  result.append(codecChannelMode2Str(codec_config.channel_mode));

  return result;
}

std::string A2dpCodecConfig::codecSampleRate2Str(
    btav_a2dp_codec_sample_rate_t codec_sample_rate) {
  std::string result;

  if (codec_sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_44100) {
    if (!result.empty()) result += "|";
    result += "44100";
  }
  if (codec_sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_48000) {
    if (!result.empty()) result += "|";
    result += "48000";
  }
  if (codec_sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_88200) {
    if (!result.empty()) result += "|";
    result += "88200";
  }
  if (codec_sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_96000) {
    if (!result.empty()) result += "|";
    result += "96000";
  }
  if (codec_sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_176400) {
    if (!result.empty()) result += "|";
    result += "176400";
  }
  if (codec_sample_rate & BTAV_A2DP_CODEC_SAMPLE_RATE_192000) {
    if (!result.empty()) result += "|";
    result += "192000";
  }
  if (result.empty()) {
    std::stringstream ss;
    ss << "UnknownSampleRate(0x" << std::hex << codec_sample_rate << ")";
    ss >> result;
  }

  return result;
}

std::string A2dpCodecConfig::codecBitsPerSample2Str(
    btav_a2dp_codec_bits_per_sample_t codec_bits_per_sample) {
  std::string result;

  if (codec_bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16) {
    if (!result.empty()) result += "|";
    result += "16";
  }
  if (codec_bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24) {
    if (!result.empty()) result += "|";
    result += "24";
  }
  if (codec_bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32) {
    if (!result.empty()) result += "|";
    result += "32";
  }
  if (result.empty()) {
    std::stringstream ss;
    ss << "UnknownBitsPerSample(0x" << std::hex << codec_bits_per_sample << ")";
    ss >> result;
  }

  return result;
}

std::string A2dpCodecConfig::codecChannelMode2Str(
    btav_a2dp_codec_channel_mode_t codec_channel_mode) {
  std::string result;

  if (codec_channel_mode & BTAV_A2DP_CODEC_CHANNEL_MODE_MONO) {
    if (!result.empty()) result += "|";
    result += "MONO";
  }
  if (codec_channel_mode & BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO) {
    if (!result.empty()) result += "|";
    result += "STEREO";
  }
  if (result.empty()) {
    std::stringstream ss;
    ss << "UnknownChannelMode(0x" << std::hex << codec_channel_mode << ")";
    ss >> result;
  }

  return result;
}

void A2dpCodecConfig::debug_codec_dump(int fd) {
  std::string result;
  dprintf(fd, "\nA2DP %s State:\n", name().c_str());
  dprintf(fd, "  Priority: %d\n", codecPriority());

  result = codecConfig2Str(getCodecConfig());
  dprintf(fd, "  Config: %s\n", result.c_str());

  result = codecConfig2Str(getCodecSelectableCapability());
  dprintf(fd, "  Selectable: %s\n", result.c_str());

  result = codecConfig2Str(getCodecLocalCapability());
  dprintf(fd, "  Local capability: %s\n", result.c_str());
}

//
// Compares two codecs |lhs| and |rhs| based on their priority.
// Returns true if |lhs| has higher priority (larger priority value).
// If |lhs| and |rhs| have same priority, the unique codec index is used
// as a tie-breaker: larger codec index value means higher priority.
//
static bool compare_codec_priority(const A2dpCodecConfig* lhs,
                                   const A2dpCodecConfig* rhs) {
  if (lhs->codecPriority() > rhs->codecPriority()) return true;
  if (lhs->codecPriority() < rhs->codecPriority()) return false;
  return (lhs->codecIndex() > rhs->codecIndex());
}

A2dpCodecs::A2dpCodecs(
    const std::vector<btav_a2dp_codec_config_t>& codec_priorities)
    : current_codec_config_(nullptr) {
  for (auto config : codec_priorities) {
    codec_priorities_.insert(
        std::make_pair(config.codec_type, config.codec_priority));
  }
}

A2dpCodecs::~A2dpCodecs() {
  std::unique_lock<std::recursive_mutex> lock(codec_mutex_);
  for (const auto& iter : indexed_codecs_) {
    delete iter.second;
  }
  for (const auto& iter : disabled_codecs_) {
    delete iter.second;
  }
  lock.unlock();
}

bool A2dpCodecs::init() {
  LOG_INFO("%s", __func__);
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  char* tok = NULL;
  char* tmp_token = NULL;
  bool offload_codec_support[BTAV_A2DP_CODEC_INDEX_MAX] = {false};
  char value_sup[PROPERTY_VALUE_MAX], value_dis[PROPERTY_VALUE_MAX];

  osi_property_get("ro.bluetooth.a2dp_offload.supported", value_sup, "false");
  osi_property_get("persist.bluetooth.a2dp_offload.disabled", value_dis,
                   "false");
  a2dp_offload_status =
      (strcmp(value_sup, "true") == 0) && (strcmp(value_dis, "false") == 0);

  if (a2dp_offload_status) {
    char value_cap[PROPERTY_VALUE_MAX];
    osi_property_get("persist.bluetooth.a2dp_offload.cap", value_cap, "");
    tok = strtok_r((char*)value_cap, "-", &tmp_token);
    while (tok != NULL) {
      if (strcmp(tok, "sbc") == 0) {
        LOG_INFO("%s: SBC offload supported", __func__);
        offload_codec_support[BTAV_A2DP_CODEC_INDEX_SOURCE_SBC] = true;
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
      } else if (strcmp(tok, "aac") == 0) {
        LOG_INFO("%s: AAC offload supported", __func__);
        offload_codec_support[BTAV_A2DP_CODEC_INDEX_SOURCE_AAC] = true;
      } else if (strcmp(tok, "aptx") == 0) {
        LOG_INFO("%s: APTX offload supported", __func__);
        offload_codec_support[BTAV_A2DP_CODEC_INDEX_SOURCE_APTX] = true;
      } else if (strcmp(tok, "aptxhd") == 0) {
        LOG_INFO("%s: APTXHD offload supported", __func__);
        offload_codec_support[BTAV_A2DP_CODEC_INDEX_SOURCE_APTX_HD] = true;
      } else if (strcmp(tok, "ldac") == 0) {
        LOG_INFO("%s: LDAC offload supported", __func__);
        offload_codec_support[BTAV_A2DP_CODEC_INDEX_SOURCE_LDAC] = true;
      }
      // Savitech LHDC - START
      else if (strcmp(tok, "lhdcv2") == 0) {
        LOG_INFO("%s: LHDCV2 offload not supported", __func__);
        offload_codec_support[BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV2] = false;
      } else if (strcmp(tok, "lhdcv3") == 0) {
        LOG_INFO("%s: LHDCV3 offload not supported", __func__);
        offload_codec_support[BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV3] = false;
      } else if (strcmp(tok, "lhdcv5") == 0) {
        LOG_INFO("%s: LHDCV5 offload not supported", __func__);
        offload_codec_support[BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV5] = false;
      }
      // Savitech LHDC - END
#endif
      tok = strtok_r(NULL, "-", &tmp_token);
    };
  }

  for (int i = BTAV_A2DP_CODEC_INDEX_MIN; i < BTAV_A2DP_CODEC_INDEX_MAX; i++) {
    btav_a2dp_codec_index_t codec_index =
        static_cast<btav_a2dp_codec_index_t>(i);

    // Select the codec priority if explicitly configured
    btav_a2dp_codec_priority_t codec_priority =
        BTAV_A2DP_CODEC_PRIORITY_DEFAULT;
    auto cp_iter = codec_priorities_.find(codec_index);
    if (cp_iter != codec_priorities_.end()) {
      codec_priority = cp_iter->second;
    }

    // In offload mode, disable the codecs based on the property
    if ((codec_index < BTAV_A2DP_CODEC_INDEX_SOURCE_MAX) &&
        a2dp_offload_status && (offload_codec_support[i] != true)) {
      codec_priority = BTAV_A2DP_CODEC_PRIORITY_DISABLED;
    }

    A2dpCodecConfig* codec_config =
        A2dpCodecConfig::createCodec(codec_index, codec_priority);
    if (codec_config == nullptr) continue;

    if (codec_priority != BTAV_A2DP_CODEC_PRIORITY_DEFAULT) {
      LOG_INFO("%s: updated %s codec priority to %d", __func__,
               codec_config->name().c_str(), codec_priority);
    }

    // Test if the codec is disabled
    if (codec_config->codecPriority() == BTAV_A2DP_CODEC_PRIORITY_DISABLED) {
      disabled_codecs_.insert(std::make_pair(codec_index, codec_config));
      continue;
    }

    indexed_codecs_.insert(std::make_pair(codec_index, codec_config));

    if (codec_index < BTAV_A2DP_CODEC_INDEX_SOURCE_MAX) {
      ordered_source_codecs_.push_back(codec_config);
      ordered_source_codecs_.sort(compare_codec_priority);
    } else {
      ordered_sink_codecs_.push_back(codec_config);
      ordered_sink_codecs_.sort(compare_codec_priority);
    }
  }

  if (ordered_source_codecs_.empty()) {
    LOG_ERROR("%s: no Source codecs were initialized", __func__);
  } else {
    for (auto iter : ordered_source_codecs_) {
      LOG_INFO("%s: initialized Source codec %s", __func__,
               iter->name().c_str());
    }
  }
  if (ordered_sink_codecs_.empty()) {
    LOG_ERROR("%s: no Sink codecs were initialized", __func__);
  } else {
    for (auto iter : ordered_sink_codecs_) {
      LOG_INFO("%s: initialized Sink codec %s", __func__, iter->name().c_str());
    }
  }

  return (!ordered_source_codecs_.empty() && !ordered_sink_codecs_.empty());
}

A2dpCodecConfig* A2dpCodecs::findSourceCodecConfig(
    const uint8_t* p_codec_info) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  btav_a2dp_codec_index_t codec_index = A2DP_SourceCodecIndex(p_codec_info);
  if (codec_index == BTAV_A2DP_CODEC_INDEX_MAX) return nullptr;

  auto iter = indexed_codecs_.find(codec_index);
  if (iter == indexed_codecs_.end()) return nullptr;
  return iter->second;
}

A2dpCodecConfig* A2dpCodecs::findSinkCodecConfig(const uint8_t* p_codec_info) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  btav_a2dp_codec_index_t codec_index = A2DP_SinkCodecIndex(p_codec_info);
  if (codec_index == BTAV_A2DP_CODEC_INDEX_MAX) return nullptr;

  auto iter = indexed_codecs_.find(codec_index);
  if (iter == indexed_codecs_.end()) return nullptr;
  return iter->second;
}

bool A2dpCodecs::isSupportedCodec(btav_a2dp_codec_index_t codec_index) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  return indexed_codecs_.find(codec_index) != indexed_codecs_.end();
}

bool A2dpCodecs::setCodecConfig(const uint8_t* p_peer_codec_info,
                                bool is_capability,
                                uint8_t* p_result_codec_config,
                                bool select_current_codec) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  A2dpCodecConfig* a2dp_codec_config = findSourceCodecConfig(p_peer_codec_info);
  if (a2dp_codec_config == nullptr) return false;
  if (!a2dp_codec_config->setCodecConfig(p_peer_codec_info, is_capability,
                                         p_result_codec_config)) {
    return false;
  }
  if (select_current_codec) {
    current_codec_config_ = a2dp_codec_config;
  }
  return true;
}

bool A2dpCodecs::setSinkCodecConfig(const uint8_t* p_peer_codec_info,
                                    bool is_capability,
                                    uint8_t* p_result_codec_config,
                                    bool select_current_codec) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  A2dpCodecConfig* a2dp_codec_config = findSinkCodecConfig(p_peer_codec_info);
  if (a2dp_codec_config == nullptr) return false;
  if (!a2dp_codec_config->setCodecConfig(p_peer_codec_info, is_capability,
                                         p_result_codec_config)) {
    return false;
  }
  if (select_current_codec) {
    current_codec_config_ = a2dp_codec_config;
  }
  return true;
}

/***********************************************
 * Savitech LHDC_EXT_API -- START
 ***********************************************/
static bool swapInt64toByteArray(unsigned char *byteArray, int64_t integer64)
{
  bool ret = false;
  if (!byteArray) {
    APPL_TRACE_ERROR("%s: null ptr", __func__);
    return ret;
  }

  byteArray[7] = ((integer64 & 0x00000000000000FF) >> 0);
  byteArray[6] = ((integer64 & 0x000000000000FF00) >> 8);
  byteArray[5] = ((integer64 & 0x0000000000FF0000) >> 16);
  byteArray[4] = ((integer64 & 0x00000000FF000000) >> 24);
  byteArray[3] = ((integer64 & 0x000000FF00000000) >> 32);
  byteArray[2] = ((integer64 & 0x0000FF0000000000) >> 40);
  byteArray[1] = ((integer64 & 0x00FF000000000000) >> 48);
  byteArray[0] = ((integer64 & 0xFF00000000000000) >> 56);

  ret = true;
  return ret;
}

static bool getLHDCA2DPSpecficV2(btav_a2dp_codec_config_t *a2dpCfg, unsigned char *pucConfig, const int clen)
{
  if (clen < (int)LHDC_EXTEND_FUNC_CONFIG_TOTAL_FIXED_SIZE_V2 ) {
    APPL_TRACE_ERROR("%s: payload size too small! clen=%d ",__func__, clen);
    return false;
  }

  /* copy specifics into buffer */
  if ( !(
      swapInt64toByteArray(&pucConfig[LHDC_EXTEND_FUNC_A2DP_SPECIFICS1_HEAD_V2], a2dpCfg->codec_specific_1) &&
      swapInt64toByteArray(&pucConfig[LHDC_EXTEND_FUNC_A2DP_SPECIFICS2_HEAD_V2], a2dpCfg->codec_specific_2) &&
      swapInt64toByteArray(&pucConfig[LHDC_EXTEND_FUNC_A2DP_SPECIFICS3_HEAD_V2], a2dpCfg->codec_specific_3) &&
      swapInt64toByteArray(&pucConfig[LHDC_EXTEND_FUNC_A2DP_SPECIFICS4_HEAD_V2], a2dpCfg->codec_specific_4)
      )) {
    APPL_TRACE_ERROR("%s: fail to copy specifics to buffer!",  __func__);
    return false;
  }

  /* fill capability metadata fields */
  if( A2DP_VendorGetSrcCapVectorLhdcv3(&pucConfig[LHDC_EXTEND_FUNC_A2DP_CAPMETA_HEAD_V2]) ) {
    APPL_TRACE_DEBUG("%s: Get metadata of capabilities success!", __func__);
  } else {
    APPL_TRACE_ERROR("%s: fail to get capability fields!",  __func__);
    return false;
  }

  return true;
}

static bool getLHDCA2DPSpecficV1(btav_a2dp_codec_config_t *a2dpCfg, unsigned char *pucConfig, const int clen)
{
  if (clen < (int)LHDC_EXTEND_FUNC_CONFIG_TOTAL_FIXED_SIZE_V1 ) {
    APPL_TRACE_ERROR("%s: payload size too small! clen=%d ",__func__, clen);
    return false;
  }

  /* copy specifics into buffer */
  if( !(
      swapInt64toByteArray(&pucConfig[LHDC_EXTEND_FUNC_A2DP_SPECIFICS1_HEAD_V1], a2dpCfg->codec_specific_1) &&
      swapInt64toByteArray(&pucConfig[LHDC_EXTEND_FUNC_A2DP_SPECIFICS2_HEAD_V1], a2dpCfg->codec_specific_2) &&
      swapInt64toByteArray(&pucConfig[LHDC_EXTEND_FUNC_A2DP_SPECIFICS3_HEAD_V1], a2dpCfg->codec_specific_3) &&
      swapInt64toByteArray(&pucConfig[LHDC_EXTEND_FUNC_A2DP_SPECIFICS4_HEAD_V1], a2dpCfg->codec_specific_4)
      )) {
    APPL_TRACE_ERROR("%s: fail to copy specifics to buffer!",  __func__);
    return false;
  }

  return true;
}

int A2dpCodecs::getLHDCCodecUserConfig(
    A2dpCodecConfig* peerCodec,
    const char* codecConfig, const int clen) {

  int result = BT_STATUS_FAIL;

  if (peerCodec == nullptr || codecConfig == nullptr) {
    APPL_TRACE_ERROR("A2dpCodecs::%s: null input(peerCodec:%p codecConfig:%p)", __func__, peerCodec, codecConfig);
    return BT_STATUS_FAIL;
  }
  btav_a2dp_codec_index_t peerCodecIndex = peerCodec->codecIndex();

  switch(peerCodecIndex)
  {
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV5:
    result = peerCodec->getLhdcExtendAPIConfig(peerCodec, codecConfig, clen);
    break;

  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV3:
    if (codecConfig[LHDC_EXTEND_FUNC_CONFIG_API_CODE_HEAD] == LHDC_EXTEND_FUNC_CODE_A2DP_TYPE_MASK) {
      /* **************************************
       * LHDC A2DP related APIs:
       * **************************************/
      unsigned char *pucConfig = (unsigned char *) codecConfig;
      unsigned int exFuncVer = 0;
      unsigned int exFuncCode = 0;

      if (pucConfig == NULL) {
          APPL_TRACE_ERROR("%s: User Config error!(%p)",  __func__, codecConfig);
          goto Fail;
      }

      /* check required buffer size for generic header */
      if (clen < (int)(LHDC_EXTEND_FUNC_CONFIG_API_VERSION_SIZE + LHDC_EXTEND_FUNC_CONFIG_API_CODE_SIZE)) {
           // Buffer is too small for generic header size
          APPL_TRACE_ERROR("%s: buffer is too small for command clen=%d",  __func__, clen);
          goto Fail;
      }

      if (current_codec_config_ == NULL) {
          APPL_TRACE_ERROR("%s: Can not get current a2dp codec config!",  __func__);
          goto Fail;
      }

      A2dpCodecConfig *a2dp_codec_config = current_codec_config_;
      btav_a2dp_codec_config_t codec_config_tmp;

      exFuncVer = (((unsigned int) pucConfig[3]) & ((unsigned int)0xff)) |
                 ((((unsigned int) pucConfig[2]) & ((unsigned int)0xff)) << 8)  |
                 ((((unsigned int) pucConfig[1]) & ((unsigned int)0xff)) << 16) |
                 ((((unsigned int) pucConfig[0]) & ((unsigned int)0xff)) << 24);
      exFuncCode = (((unsigned int) pucConfig[7]) & ((unsigned int)0xff)) |
                  ((((unsigned int) pucConfig[6]) & ((unsigned int)0xff)) << 8)  |
                  ((((unsigned int) pucConfig[5]) & ((unsigned int)0xff)) << 16) |
                  ((((unsigned int) pucConfig[4]) & ((unsigned int)0xff)) << 24);

      switch (exFuncCode)
      {
        case EXTEND_FUNC_CODE_GET_SPECIFIC:
          /* **************************************
           * API::Get A2DP Specifics
           * **************************************/
          switch(pucConfig[LHDC_EXTEND_FUNC_CONFIG_A2DPCFG_CODE_HEAD])
          {
            case LHDC_EXTEND_FUNC_A2DP_TYPE_SPECIFICS_FINAL_CFG:
              codec_config_tmp = a2dp_codec_config->getCodecConfig();
              break;
            case LHDC_EXTEND_FUNC_A2DP_TYPE_SPECIFICS_FINAL_CAP:
              codec_config_tmp = a2dp_codec_config->getCodecCapability();
              break;
            case LHDC_EXTEND_FUNC_A2DP_TYPE_SPECIFICS_LOCAL_CAP:
              codec_config_tmp = a2dp_codec_config->getCodecLocalCapability();
              break;
            case LHDC_EXTEND_FUNC_A2DP_TYPE_SPECIFICS_SELECTABLE_CAP:
              codec_config_tmp = a2dp_codec_config->getCodecSelectableCapability();
              break;
            case LHDC_EXTEND_FUNC_A2DP_TYPE_SPECIFICS_USER_CFG:
              codec_config_tmp = a2dp_codec_config->getCodecUserConfig();
              break;
            case LHDC_EXTEND_FUNC_A2DP_TYPE_SPECIFICS_AUDIO_CFG:
              codec_config_tmp = a2dp_codec_config->getCodecAudioConfig();
              break;
            default:
              APPL_TRACE_ERROR("%s: target a2dp config not found!",  __func__);
              goto Fail;
          }

          switch (exFuncVer)
          {
            case EXTEND_FUNC_VER_GET_SPECIFIC_V1:
              if (!getLHDCA2DPSpecficV1(&codec_config_tmp, pucConfig, clen))
                goto Fail;
              break;
            case EXTEND_FUNC_VER_GET_SPECIFIC_V2:
              if (!getLHDCA2DPSpecficV2(&codec_config_tmp, pucConfig, clen))
                goto Fail;
              break;
            default:
              APPL_TRACE_DEBUG("%s: Invalid Ex. Function Version!(0x%X)",  __func__, exFuncVer);
              goto Fail;
          }
          result = BT_STATUS_SUCCESS;
          break;

      default:
        APPL_TRACE_DEBUG("%s: Invalid Ex. Function Code!(0x%X)",  __func__, exFuncCode);
        goto Fail;
      } // switch (exFuncCode)
    } else if ( codecConfig[LHDC_EXTEND_FUNC_CONFIG_API_CODE_HEAD] == LHDC_EXTEND_FUNC_CODE_LIB_TYPE_MASK ) {
      result = A2dpCodecConfigLhdcV3::getEncoderExtendFuncUserConfig(codecConfig, clen);
    }
    break;
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV2:
  default:
    APPL_TRACE_DEBUG("%s: feature not support!", __func__);
    goto Fail;
  }

Fail:
  return result;
}

int A2dpCodecs::setLHDCCodecUserConfig(
    A2dpCodecConfig* peerCodec,
    const char* codecConfig, const int clen) {

  int result = BT_STATUS_FAIL;

  if (peerCodec == nullptr || codecConfig == nullptr) {
    APPL_TRACE_ERROR("A2dpCodecs::%s: null input(peerCodec:%p version:%p)", __func__, peerCodec, codecConfig);
    return BT_STATUS_FAIL;
  }
  btav_a2dp_codec_index_t peerCodecIndex = peerCodec->codecIndex();

  switch(peerCodecIndex)
  {
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV5:
    result = peerCodec->setLhdcExtendAPIConfig(peerCodec, codecConfig, clen);
    break;
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV3:
    result = A2dpCodecConfigLhdcV3::setEncoderExtendFuncUserConfig(codecConfig, clen);
    break;
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV2:
  default:
    APPL_TRACE_DEBUG("%s: peer codecIndex(%d) not support the feature!", __func__, peerCodecIndex);
    return result;
  }

  return result;
}

bool A2dpCodecs::setLHDCCodecUserData(
    A2dpCodecConfig* peerCodec,
    const char* codecData, const int clen) {

  if (peerCodec == nullptr || codecData == nullptr) {
    APPL_TRACE_ERROR("A2dpCodecs::%s: null input(peerCodec:%p version:%p)", __func__, peerCodec, codecData);
    return false;
  }
  btav_a2dp_codec_index_t peerCodecIndex = peerCodec->codecIndex();

  switch(peerCodecIndex)
  {
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV5:
    peerCodec->setLhdcExtendAPIData(peerCodec, codecData, clen);
    break;
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV3:
    A2dpCodecConfigLhdcV3::setEncoderExtendFuncUserData(codecData, clen);
    break;
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV2:
  default:
    APPL_TRACE_DEBUG("%s: peer codecIndex(%d) not support the feature!", __func__, peerCodecIndex);
    return false;
  }

  return true;
}

int A2dpCodecs::getLHDCCodecUserApiVer(
    A2dpCodecConfig* peerCodec,
    const char* version, const int clen) {
  int result = BT_STATUS_FAIL;

  if (peerCodec == nullptr || version == nullptr) {
    APPL_TRACE_ERROR("A2dpCodecs::%s: null input(peerCodec:%p version:%p)", __func__, peerCodec, version);
    return BT_STATUS_FAIL;
  }
  btav_a2dp_codec_index_t peerCodecIndex = peerCodec->codecIndex();

  switch(peerCodecIndex) {
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV5:
    result = peerCodec->getLhdcExtendAPIVersion(peerCodec, version, clen);
    break;
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV3:
    result = A2dpCodecConfigLhdcV3::getEncoderExtendFuncUserApiVer(version, clen);
    break;
  case BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV2:
  default:
    APPL_TRACE_DEBUG("%s: peer codecIndex(%d) not support the feature!", __func__, peerCodecIndex);
    return result;
  }

  return result;
}
/***********************************************
 * Savitech LHDC_EXT_API -- END
 ***********************************************/

bool A2dpCodecs::setCodecUserConfig(
    const btav_a2dp_codec_config_t& codec_user_config,
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
    const uint8_t* p_peer_sink_capabilities, uint8_t* p_result_codec_config,
    bool* p_restart_input, bool* p_restart_output, bool* p_config_updated) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  btav_a2dp_codec_config_t codec_audio_config;
  A2dpCodecConfig* a2dp_codec_config = nullptr;
  A2dpCodecConfig* last_codec_config = current_codec_config_;
  *p_restart_input = false;
  *p_restart_output = false;
  *p_config_updated = false;

  LOG_INFO("%s: Configuring: %s", __func__,
           codec_user_config.ToString().c_str());

  if (codec_user_config.codec_type < BTAV_A2DP_CODEC_INDEX_MAX) {
    auto iter = indexed_codecs_.find(codec_user_config.codec_type);
    if (iter == indexed_codecs_.end()) goto fail;
    a2dp_codec_config = iter->second;
  } else {
    // Update the default codec
    a2dp_codec_config = current_codec_config_;
  }
  if (a2dp_codec_config == nullptr) goto fail;

  // Reuse the existing codec audio config
  codec_audio_config = a2dp_codec_config->getCodecAudioConfig();
  if (!a2dp_codec_config->setCodecUserConfig(
          codec_user_config, codec_audio_config, p_peer_params,
          p_peer_sink_capabilities, true, p_result_codec_config,
          p_restart_input, p_restart_output, p_config_updated)) {
    goto fail;
  }

  // Update the codec priorities, and eventually restart the connection
  // if a new codec needs to be selected.
  do {
    // Update the codec priority
    btav_a2dp_codec_priority_t old_priority =
        a2dp_codec_config->codecPriority();
    btav_a2dp_codec_priority_t new_priority = codec_user_config.codec_priority;
    a2dp_codec_config->setCodecPriority(new_priority);
    // Get the actual (recomputed) priority
    new_priority = a2dp_codec_config->codecPriority();

    // Check if there was no previous codec
    if (last_codec_config == nullptr) {
      current_codec_config_ = a2dp_codec_config;
      *p_restart_input = true;
      *p_restart_output = true;
      break;
    }

    // Check if the priority of the current codec was updated
    if (a2dp_codec_config == last_codec_config) {
      if (old_priority == new_priority) break;  // No change in priority

      *p_config_updated = true;
      if (new_priority < old_priority) {
        // The priority has become lower - restart the connection to
        // select a new codec.
        *p_restart_output = true;
      }
      break;
    }

    if (new_priority <= old_priority) {
      // No change in priority, or the priority has become lower.
      // This wasn't the current codec, so we shouldn't select a new codec.
      if (*p_restart_input || *p_restart_output ||
          (old_priority != new_priority)) {
        *p_config_updated = true;
      }
      *p_restart_input = false;
      *p_restart_output = false;
      break;
    }

    *p_config_updated = true;
    if (new_priority >= last_codec_config->codecPriority()) {
      // The new priority is higher than the current codec. Restart the
      // connection to select a new codec.
      current_codec_config_ = a2dp_codec_config;
      last_codec_config->setDefaultCodecPriority();
      *p_restart_input = true;
      *p_restart_output = true;
    }
  } while (false);
  ordered_source_codecs_.sort(compare_codec_priority);

  if (*p_restart_input || *p_restart_output) *p_config_updated = true;

  LOG_INFO(
      "%s: Configured: restart_input = %d restart_output = %d "
      "config_updated = %d",
      __func__, *p_restart_input, *p_restart_output, *p_config_updated);

  return true;

fail:
  current_codec_config_ = last_codec_config;
  return false;
}

bool A2dpCodecs::setCodecAudioConfig(
    const btav_a2dp_codec_config_t& codec_audio_config,
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
    const uint8_t* p_peer_sink_capabilities, uint8_t* p_result_codec_config,
    bool* p_restart_output, bool* p_config_updated) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  btav_a2dp_codec_config_t codec_user_config;
  A2dpCodecConfig* a2dp_codec_config = current_codec_config_;
  *p_restart_output = false;
  *p_config_updated = false;

  if (a2dp_codec_config == nullptr) return false;

  // Reuse the existing codec user config
  codec_user_config = a2dp_codec_config->getCodecUserConfig();
  bool restart_input = false;  // Flag ignored - input was just restarted
  if (!a2dp_codec_config->setCodecUserConfig(
          codec_user_config, codec_audio_config, p_peer_params,
          p_peer_sink_capabilities, true, p_result_codec_config, &restart_input,
          p_restart_output, p_config_updated)) {
    return false;
  }

  return true;
}

bool A2dpCodecs::setCodecOtaConfig(
    const uint8_t* p_ota_codec_config,
    const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
    uint8_t* p_result_codec_config, bool* p_restart_input,
    bool* p_restart_output, bool* p_config_updated) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  btav_a2dp_codec_index_t codec_type;
  btav_a2dp_codec_config_t codec_user_config;
  btav_a2dp_codec_config_t codec_audio_config;
  A2dpCodecConfig* a2dp_codec_config = nullptr;
  A2dpCodecConfig* last_codec_config = current_codec_config_;
  *p_restart_input = false;
  *p_restart_output = false;
  *p_config_updated = false;

  // Check whether the current codec config is explicitly configured by
  // user configuration. If yes, then the OTA codec configuration is ignored.
  if (current_codec_config_ != nullptr) {
    codec_user_config = current_codec_config_->getCodecUserConfig();
    if (!A2dpCodecConfig::isCodecConfigEmpty(codec_user_config)) {
      LOG_WARN(
          "%s: ignoring peer OTA configuration for codec %s: "
          "existing user configuration for current codec %s",
          __func__, A2DP_CodecName(p_ota_codec_config),
          current_codec_config_->name().c_str());
      goto fail;
    }
  }

  // Check whether the codec config for the same codec is explicitly configured
  // by user configuration. If yes, then the OTA codec configuration is
  // ignored.
  codec_type = A2DP_SourceCodecIndex(p_ota_codec_config);
  if (codec_type == BTAV_A2DP_CODEC_INDEX_MAX) {
    LOG_WARN(
        "%s: ignoring peer OTA codec configuration: "
        "invalid codec",
        __func__);
    goto fail;  // Invalid codec
  } else {
    auto iter = indexed_codecs_.find(codec_type);
    if (iter == indexed_codecs_.end()) {
      LOG_WARN("%s: cannot find codec configuration for peer OTA codec %s",
               __func__, A2DP_CodecName(p_ota_codec_config));
      goto fail;
    }
    a2dp_codec_config = iter->second;
  }
  if (a2dp_codec_config == nullptr) goto fail;
  codec_user_config = a2dp_codec_config->getCodecUserConfig();
  if (!A2dpCodecConfig::isCodecConfigEmpty(codec_user_config)) {
    LOG_WARN(
        "%s: ignoring peer OTA configuration for codec %s: "
        "existing user configuration for same codec",
        __func__, A2DP_CodecName(p_ota_codec_config));
    goto fail;
  }
  current_codec_config_ = a2dp_codec_config;

  // Reuse the existing codec user config and codec audio config
  codec_audio_config = a2dp_codec_config->getCodecAudioConfig();
  if (!a2dp_codec_config->setCodecUserConfig(
          codec_user_config, codec_audio_config, p_peer_params,
          p_ota_codec_config, false, p_result_codec_config, p_restart_input,
          p_restart_output, p_config_updated)) {
    LOG_WARN("%s: cannot set codec configuration for peer OTA codec %s",
             __func__, A2DP_CodecName(p_ota_codec_config));
    goto fail;
  }
  CHECK(current_codec_config_ != nullptr);

  if (*p_restart_input || *p_restart_output) *p_config_updated = true;

  return true;

fail:
  current_codec_config_ = last_codec_config;
  return false;
}

bool A2dpCodecs::setPeerSinkCodecCapabilities(
    const uint8_t* p_peer_codec_capabilities) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  if (!A2DP_IsPeerSinkCodecValid(p_peer_codec_capabilities)) return false;
  A2dpCodecConfig* a2dp_codec_config =
      findSourceCodecConfig(p_peer_codec_capabilities);
  if (a2dp_codec_config == nullptr) return false;
  return a2dp_codec_config->setPeerCodecCapabilities(p_peer_codec_capabilities);
}

bool A2dpCodecs::setPeerSourceCodecCapabilities(
    const uint8_t* p_peer_codec_capabilities) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  if (!A2DP_IsPeerSourceCodecValid(p_peer_codec_capabilities)) return false;
  A2dpCodecConfig* a2dp_codec_config =
      findSinkCodecConfig(p_peer_codec_capabilities);
  if (a2dp_codec_config == nullptr) return false;
  return a2dp_codec_config->setPeerCodecCapabilities(p_peer_codec_capabilities);
}

bool A2dpCodecs::getCodecConfigAndCapabilities(
    btav_a2dp_codec_config_t* p_codec_config,
    std::vector<btav_a2dp_codec_config_t>* p_codecs_local_capabilities,
    std::vector<btav_a2dp_codec_config_t>* p_codecs_selectable_capabilities) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  if (current_codec_config_ != nullptr) {
    *p_codec_config = current_codec_config_->getCodecConfig();
  } else {
    btav_a2dp_codec_config_t codec_config;
    memset(&codec_config, 0, sizeof(codec_config));
    *p_codec_config = codec_config;
  }

  std::vector<btav_a2dp_codec_config_t> codecs_capabilities;
  for (auto codec : orderedSourceCodecs()) {
    codecs_capabilities.push_back(codec->getCodecLocalCapability());
  }
  *p_codecs_local_capabilities = codecs_capabilities;

  codecs_capabilities.clear();
  for (auto codec : orderedSourceCodecs()) {
    btav_a2dp_codec_config_t codec_capability =
        codec->getCodecSelectableCapability();
    // Don't add entries that cannot be used
    if ((codec_capability.sample_rate == BTAV_A2DP_CODEC_SAMPLE_RATE_NONE) ||
        (codec_capability.bits_per_sample ==
         BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE) ||
        (codec_capability.channel_mode == BTAV_A2DP_CODEC_CHANNEL_MODE_NONE)) {
      continue;
    }
    codecs_capabilities.push_back(codec_capability);
  }
  *p_codecs_selectable_capabilities = codecs_capabilities;

  return true;
}

void A2dpCodecs::debug_codec_dump(int fd) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  dprintf(fd, "\nA2DP Codecs State:\n");

  // Print the current codec name
  if (current_codec_config_ != nullptr) {
    dprintf(fd, "  Current Codec: %s\n", current_codec_config_->name().c_str());
  } else {
    dprintf(fd, "  Current Codec: None\n");
  }

  // Print the codec-specific state
  for (auto codec_config : ordered_source_codecs_) {
    codec_config->debug_codec_dump(fd);
  }
}

tA2DP_CODEC_TYPE A2DP_GetCodecType(const uint8_t* p_codec_info) {
  return (tA2DP_CODEC_TYPE)(p_codec_info[AVDT_CODEC_TYPE_INDEX]);
}

bool A2DP_IsSourceCodecValid(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_IsSourceCodecValidSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_IsSourceCodecValidAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_IsVendorSourceCodecValid(p_codec_info);
#endif
    default:
      break;
  }

  return false;
}

bool A2DP_IsSinkCodecValid(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_IsSinkCodecValidSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_IsSinkCodecValidAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_IsVendorSinkCodecValid(p_codec_info);
#endif
    default:
      break;
  }

  return false;
}

bool A2DP_IsPeerSourceCodecValid(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_IsPeerSourceCodecValidSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_IsPeerSourceCodecValidAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_IsVendorPeerSourceCodecValid(p_codec_info);
#endif
    default:
      break;
  }

  return false;
}

bool A2DP_IsPeerSinkCodecValid(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_IsPeerSinkCodecValidSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_IsPeerSinkCodecValidAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_IsVendorPeerSinkCodecValid(p_codec_info);
#endif
    default:
      break;
  }

  return false;
}

bool A2DP_IsSinkCodecSupported(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_IsSinkCodecSupportedSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_IsSinkCodecSupportedAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_IsVendorSinkCodecSupported(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return false;
}

bool A2DP_IsPeerSourceCodecSupported(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_IsPeerSourceCodecSupportedSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_IsPeerSourceCodecSupportedAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_IsVendorPeerSourceCodecSupported(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return false;
}

void A2DP_InitDefaultCodec(uint8_t* p_codec_info) {
  A2DP_InitDefaultCodecSbc(p_codec_info);
}

bool A2DP_UsesRtpHeader(bool content_protection_enabled,
                        const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  if (codec_type != A2DP_MEDIA_CT_NON_A2DP) return true;

#if !defined(EXCLUDE_NONSTANDARD_CODECS)
  return A2DP_VendorUsesRtpHeader(content_protection_enabled, p_codec_info);
#else
  return true;
#endif
}

uint8_t A2DP_GetMediaType(const uint8_t* p_codec_info) {
  uint8_t media_type = (p_codec_info[A2DP_MEDIA_TYPE_OFFSET] >> 4) & 0x0f;
  return media_type;
}

const char* A2DP_CodecName(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_CodecNameSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_CodecNameAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorCodecName(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return "UNKNOWN CODEC";
}

bool A2DP_CodecTypeEquals(const uint8_t* p_codec_info_a,
                          const uint8_t* p_codec_info_b) {
  tA2DP_CODEC_TYPE codec_type_a = A2DP_GetCodecType(p_codec_info_a);
  tA2DP_CODEC_TYPE codec_type_b = A2DP_GetCodecType(p_codec_info_b);

  if (codec_type_a != codec_type_b) return false;

  switch (codec_type_a) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_CodecTypeEqualsSbc(p_codec_info_a, p_codec_info_b);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_CodecTypeEqualsAac(p_codec_info_a, p_codec_info_b);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorCodecTypeEquals(p_codec_info_a, p_codec_info_b);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type_a);
  return false;
}

bool A2DP_CodecEquals(const uint8_t* p_codec_info_a,
                      const uint8_t* p_codec_info_b) {
  tA2DP_CODEC_TYPE codec_type_a = A2DP_GetCodecType(p_codec_info_a);
  tA2DP_CODEC_TYPE codec_type_b = A2DP_GetCodecType(p_codec_info_b);

  if (codec_type_a != codec_type_b) return false;

  switch (codec_type_a) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_CodecEqualsSbc(p_codec_info_a, p_codec_info_b);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_CodecEqualsAac(p_codec_info_a, p_codec_info_b);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorCodecEquals(p_codec_info_a, p_codec_info_b);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type_a);
  return false;
}

int A2DP_GetTrackSampleRate(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_GetTrackSampleRateSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_GetTrackSampleRateAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorGetTrackSampleRate(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return -1;
}

int A2DP_GetTrackBitsPerSample(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_GetTrackBitsPerSampleSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_GetTrackBitsPerSampleAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorGetTrackBitsPerSample(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return -1;
}

int A2DP_GetTrackChannelCount(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_GetTrackChannelCountSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_GetTrackChannelCountAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorGetTrackChannelCount(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return -1;
}

int A2DP_GetSinkTrackChannelType(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_GetSinkTrackChannelTypeSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_GetSinkTrackChannelTypeAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorGetSinkTrackChannelType(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return -1;
}

bool A2DP_GetPacketTimestamp(const uint8_t* p_codec_info, const uint8_t* p_data,
                             uint32_t* p_timestamp) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_GetPacketTimestampSbc(p_codec_info, p_data, p_timestamp);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_GetPacketTimestampAac(p_codec_info, p_data, p_timestamp);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorGetPacketTimestamp(p_codec_info, p_data, p_timestamp);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return false;
}

bool A2DP_BuildCodecHeader(const uint8_t* p_codec_info, BT_HDR* p_buf,
                           uint16_t frames_per_packet) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_BuildCodecHeaderSbc(p_codec_info, p_buf, frames_per_packet);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_BuildCodecHeaderAac(p_codec_info, p_buf, frames_per_packet);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorBuildCodecHeader(p_codec_info, p_buf,
                                         frames_per_packet);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return false;
}

const tA2DP_ENCODER_INTERFACE* A2DP_GetEncoderInterface(
    const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_GetEncoderInterfaceSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_GetEncoderInterfaceAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorGetEncoderInterface(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return NULL;
}

const tA2DP_DECODER_INTERFACE* A2DP_GetDecoderInterface(
    const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_GetDecoderInterfaceSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_GetDecoderInterfaceAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorGetDecoderInterface(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return NULL;
}

bool A2DP_AdjustCodec(uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_AdjustCodecSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_AdjustCodecAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorAdjustCodec(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return false;
}

btav_a2dp_codec_index_t A2DP_SourceCodecIndex(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_SourceCodecIndexSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_SourceCodecIndexAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorSourceCodecIndex(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return BTAV_A2DP_CODEC_INDEX_MAX;
}

btav_a2dp_codec_index_t A2DP_SinkCodecIndex(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  LOG_VERBOSE("%s: codec_type = 0x%x", __func__, codec_type);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_SinkCodecIndexSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_SinkCodecIndexAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorSinkCodecIndex(p_codec_info);
#endif
    default:
      break;
  }

  LOG_ERROR("%s: unsupported codec type 0x%x", __func__, codec_type);
  return BTAV_A2DP_CODEC_INDEX_MAX;
}

const char* A2DP_CodecIndexStr(btav_a2dp_codec_index_t codec_index) {
  switch (codec_index) {
    case BTAV_A2DP_CODEC_INDEX_SOURCE_SBC:
      return A2DP_CodecIndexStrSbc();
    case BTAV_A2DP_CODEC_INDEX_SINK_SBC:
      return A2DP_CodecIndexStrSbcSink();
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case BTAV_A2DP_CODEC_INDEX_SOURCE_AAC:
      return A2DP_CodecIndexStrAac();
    case BTAV_A2DP_CODEC_INDEX_SINK_AAC:
      return A2DP_CodecIndexStrAacSink();
#endif
    default:
      break;
  }

#if !defined(EXCLUDE_NONSTANDARD_CODECS)
  if (codec_index < BTAV_A2DP_CODEC_INDEX_MAX)
    return A2DP_VendorCodecIndexStr(codec_index);
#endif

  return "UNKNOWN CODEC INDEX";
}

bool A2DP_InitCodecConfig(btav_a2dp_codec_index_t codec_index,
                          AvdtpSepConfig* p_cfg) {
  LOG_VERBOSE("%s: codec %s", __func__, A2DP_CodecIndexStr(codec_index));

  /* Default: no content protection info */
  p_cfg->num_protect = 0;
  p_cfg->protect_info[0] = 0;

  switch (codec_index) {
    case BTAV_A2DP_CODEC_INDEX_SOURCE_SBC:
      return A2DP_InitCodecConfigSbc(p_cfg);
    case BTAV_A2DP_CODEC_INDEX_SINK_SBC:
      return A2DP_InitCodecConfigSbcSink(p_cfg);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case BTAV_A2DP_CODEC_INDEX_SOURCE_AAC:
      return A2DP_InitCodecConfigAac(p_cfg);
    case BTAV_A2DP_CODEC_INDEX_SINK_AAC:
      return A2DP_InitCodecConfigAacSink(p_cfg);
#endif
    default:
      break;
  }

#if !defined(EXCLUDE_NONSTANDARD_CODECS)
  if (codec_index < BTAV_A2DP_CODEC_INDEX_MAX)
    return A2DP_VendorInitCodecConfig(codec_index, p_cfg);
#endif

  return false;
}

std::string A2DP_CodecInfoString(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      return A2DP_CodecInfoStringSbc(p_codec_info);
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      return A2DP_CodecInfoStringAac(p_codec_info);
    case A2DP_MEDIA_CT_NON_A2DP:
      return A2DP_VendorCodecInfoString(p_codec_info);
#endif
    default:
      break;
  }

  return "Unsupported codec type: " + loghex(codec_type);
}

int A2DP_GetEecoderEffectiveFrameSize(const uint8_t* p_codec_info) {
  tA2DP_CODEC_TYPE codec_type = A2DP_GetCodecType(p_codec_info);

  const tA2DP_ENCODER_INTERFACE* a2dp_encoder_interface = nullptr;
  switch (codec_type) {
    case A2DP_MEDIA_CT_SBC:
      a2dp_encoder_interface = A2DP_GetEncoderInterfaceSbc(p_codec_info);
      break;
#if !defined(EXCLUDE_NONSTANDARD_CODECS)
    case A2DP_MEDIA_CT_AAC:
      a2dp_encoder_interface = A2DP_GetEncoderInterfaceAac(p_codec_info);
      break;
    case A2DP_MEDIA_CT_NON_A2DP:
      a2dp_encoder_interface = A2DP_VendorGetEncoderInterface(p_codec_info);
      break;
#endif
    default:
      break;
  }
  if (a2dp_encoder_interface == nullptr) {
    return 0;
  }
  return a2dp_encoder_interface->get_effective_frame_size();
}
