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

/******************************************************************************
 *
 *  Utility functions to help build and parse the LHDC Codec Information
 *  Element and Media Payload.
 *
 ******************************************************************************/

#define LOG_TAG "a2dp_vendor_lhdcv2"

#include "bt_target.h"

#include "a2dp_vendor_lhdcv2.h"

#include <string.h>

#include <base/logging.h>
#include "a2dp_vendor.h"
#include "a2dp_vendor_lhdcv2_encoder.h"
#include "bt_utils.h"
#include "btif_av_co.h"
#include "osi/include/log.h"
#include "osi/include/osi.h"

#define CASE_RETURN_STR(const) \
  case const:                  \
    return #const;

// data type for the LHDC Codec Information Element */
// NOTE: bits_per_sample is needed only for LHDC encoder initialization.
typedef struct {
  uint32_t vendorId;
  uint16_t codecId;    /* Codec ID for LHDC */
  uint8_t sampleRate;  /* Sampling Frequency */
  btav_a2dp_codec_bits_per_sample_t bits_per_sample;
  uint8_t channelSplitMode;
  uint8_t version;
  uint8_t maxTargetBitrate;
  bool isLLSupported;
} tA2DP_LHDC_CIE;

/* LHDC Source codec capabilities */
static const tA2DP_LHDC_CIE a2dp_lhdc_source_caps = {
    A2DP_LHDC_VENDOR_ID,  // vendorId
    A2DP_LHDCV2_CODEC_ID,   // codecId
    // sampleRate
    //(A2DP_LHDC_SAMPLING_FREQ_48000),
    (A2DP_LHDC_SAMPLING_FREQ_44100 | A2DP_LHDC_SAMPLING_FREQ_48000 | A2DP_LHDC_SAMPLING_FREQ_96000),
    // bits_per_sample
    (BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16 | BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24),
    //Channel Separation
    A2DP_LHDC_CH_SPLIT_NONE | A2DP_LHDC_CH_SPLIT_TWS,
    //Version number
    A2DP_LHDC_VER2,
    //Max target bit Rate
    A2DP_LHDC_MAX_BIT_RATE_900K,
    //LL supported ?
    false,
};
    //(BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16)};

/* Default LHDC codec configuration */
static const tA2DP_LHDC_CIE a2dp_lhdc_default_config = {
    A2DP_LHDC_VENDOR_ID,                // vendorId
    A2DP_LHDCV2_CODEC_ID,                 // codecId
    A2DP_LHDC_SAMPLING_FREQ_96000,      // sampleRate
    BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24,  // bits_per_sample
    A2DP_LHDC_CH_SPLIT_NONE,
    A2DP_LHDC_VER2,
    A2DP_LHDC_MAX_BIT_RATE_900K,
    false,
};

static const tA2DP_ENCODER_INTERFACE a2dp_encoder_interface_lhdcv2 = {
    a2dp_vendor_lhdcv2_encoder_init,
    a2dp_vendor_lhdcv2_encoder_cleanup,
    a2dp_vendor_lhdcv2_feeding_reset,
    a2dp_vendor_lhdcv2_feeding_flush,
    a2dp_vendor_lhdcv2_get_encoder_interval_ms,
    a2dp_vendor_lhdcv2_get_effective_frame_size,
    a2dp_vendor_lhdcv2_send_frames,
    a2dp_vendor_lhdcv2_set_transmit_queue_length};

UNUSED_ATTR static tA2DP_STATUS A2DP_CodecInfoMatchesCapabilityLhdcV2(
    const tA2DP_LHDC_CIE* p_cap, const uint8_t* p_codec_info,
    bool is_peer_codec_info);


// Builds the LHDC Media Codec Capabilities byte sequence beginning from the
// LOSC octet. |media_type| is the media type |AVDT_MEDIA_TYPE_*|.
// |p_ie| is a pointer to the LHDC Codec Information Element information.
// The result is stored in |p_result|. Returns A2DP_SUCCESS on success,
// otherwise the corresponding A2DP error status code.
static tA2DP_STATUS A2DP_BuildInfoLhdcV2(uint8_t media_type,
                                       const tA2DP_LHDC_CIE* p_ie,
                                       uint8_t* p_result) {

  const uint8_t* tmpInfo = p_result;
  if (p_ie == NULL || p_result == NULL) {
    return A2DP_INVALID_PARAMS;
  }

  *p_result++ = A2DP_LHDCV2_CODEC_LEN;    //0
  *p_result++ = (media_type << 4);      //1
  *p_result++ = A2DP_MEDIA_CT_NON_A2DP; //2

  // Vendor ID and Codec ID
  *p_result++ = (uint8_t)(p_ie->vendorId & 0x000000FF); //3
  *p_result++ = (uint8_t)((p_ie->vendorId & 0x0000FF00) >> 8);  //4
  *p_result++ = (uint8_t)((p_ie->vendorId & 0x00FF0000) >> 16); //5
  *p_result++ = (uint8_t)((p_ie->vendorId & 0xFF000000) >> 24); //6
  *p_result++ = (uint8_t)(p_ie->codecId & 0x00FF);  //7
  *p_result++ = (uint8_t)((p_ie->codecId & 0xFF00) >> 8);   //8

  // Sampling Frequency & Bits per sample
  uint8_t para = 0;

  // sample rate bit0 ~ bit2
  para = (uint8_t)(p_ie->sampleRate & A2DP_LHDC_SAMPLING_FREQ_MASK);

  if (p_ie->bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24)
      para |= A2DP_LHDC_BIT_FMT_24;
  if(p_ie->bits_per_sample == BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16)
      para |= A2DP_LHDC_BIT_FMT_16;

  // Save octet 9
  *p_result++ = para;   //9

  para = p_ie->version;

  para |= p_ie->maxTargetBitrate;

  para |= p_ie->isLLSupported ? A2DP_LHDC_LL_SUPPORTED : A2DP_LHDC_LL_NONE;

  // Save octet 10
  *p_result++ = para;   //a

  //Save octet 11
  para = p_ie->channelSplitMode;

  *p_result++ = para;   //b

  LOG_DEBUG( "%s: Info build result = [0]:0x%x, [1]:0x%x, [2]:0x%x, [3]:0x%x, "
                     "[4]:0x%x, [5]:0x%x, [6]:0x%x, [7]:0x%x, [8]:0x%x, [9]:0x%x, [10]:0x%x, [11]:0x%x",
     __func__, tmpInfo[0], tmpInfo[1], tmpInfo[2], tmpInfo[3],
                    tmpInfo[4], tmpInfo[5], tmpInfo[6], tmpInfo[7], tmpInfo[8], tmpInfo[9], tmpInfo[10], tmpInfo[11]);
  return A2DP_SUCCESS;
}

// Parses the LHDC Media Codec Capabilities byte sequence beginning from the
// LOSC octet. The result is stored in |p_ie|. The byte sequence to parse is
// |p_codec_info|. If |is_capability| is true, the byte sequence is
// codec capabilities, otherwise is codec configuration.
// Returns A2DP_SUCCESS on success, otherwise the corresponding A2DP error
// status code.
static tA2DP_STATUS A2DP_ParseInfoLhdcV2(tA2DP_LHDC_CIE* p_ie,
                                       const uint8_t* p_codec_info,
                                       bool is_capability) {
  uint8_t losc;
  uint8_t media_type;
  tA2DP_CODEC_TYPE codec_type;
  const uint8_t* tmpInfo = p_codec_info;

  //LOG_DEBUG( "%s: p_ie = %p, p_codec_info = %p", __func__, p_ie, p_codec_info);
  if (p_ie == NULL || p_codec_info == NULL) return A2DP_INVALID_PARAMS;

  // Check the codec capability length
  losc = *p_codec_info++;

  if (losc != A2DP_LHDCV2_CODEC_LEN) return A2DP_WRONG_CODEC;

  media_type = (*p_codec_info++) >> 4;
  codec_type = *p_codec_info++;
    //LOG_DEBUG( "%s: media_type = %d, codec_type = %d", __func__, media_type, codec_type);
  /* Check the Media Type and Media Codec Type */
  if (media_type != AVDT_MEDIA_TYPE_AUDIO ||
      codec_type != A2DP_MEDIA_CT_NON_A2DP) {
    return A2DP_WRONG_CODEC;
  }

  // Check the Vendor ID and Codec ID */
  p_ie->vendorId = (*p_codec_info & 0x000000FF) |
                   (*(p_codec_info + 1) << 8 & 0x0000FF00) |
                   (*(p_codec_info + 2) << 16 & 0x00FF0000) |
                   (*(p_codec_info + 3) << 24 & 0xFF000000);
  p_codec_info += 4;
  p_ie->codecId =
      (*p_codec_info & 0x00FF) | (*(p_codec_info + 1) << 8 & 0xFF00);
  p_codec_info += 2;
  LOG_DEBUG( "%s:Vendor(0x%08x), Codec(0x%04x)", __func__, p_ie->vendorId, p_ie->codecId);
  if (p_ie->vendorId != A2DP_LHDC_VENDOR_ID ||
      p_ie->codecId != A2DP_LHDCV2_CODEC_ID) {
    return A2DP_WRONG_CODEC;
  }

  p_ie->sampleRate = *p_codec_info & A2DP_LHDC_SAMPLING_FREQ_MASK;
  if ((*p_codec_info & A2DP_LHDC_BIT_FMT_MASK) == 0) {
    return A2DP_WRONG_CODEC;
  }

  p_ie->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;
  if (*p_codec_info & A2DP_LHDC_BIT_FMT_24)
    p_ie->bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;
  if (*p_codec_info & A2DP_LHDC_BIT_FMT_16) {
    p_ie->bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
  }


  p_codec_info += 1;

  p_ie->version = (*p_codec_info) & A2DP_LHDC_VERSION_MASK;
  //p_ie->version = 1;

  p_ie->maxTargetBitrate = (*p_codec_info) & A2DP_LHDC_MAX_BIT_RATE_MASK;
  //p_ie->maxTargetBitrate = A2DP_LHDC_MAX_BIT_RATE_900K;

  p_ie->isLLSupported = ((*p_codec_info & A2DP_LHDC_LL_MASK) != 0)? true : false;
  //p_ie->isLLSupported = false;

  p_codec_info += 1;

  p_ie->channelSplitMode = (*p_codec_info) & A2DP_LHDC_CH_SPLIT_MSK;
  //p_ie->channelSplitMode = A2DP_LHDC_CH_SPLIT_NONE;




    LOG_DEBUG( "%s: codec info = [0]:0x%x, [1]:0x%x, [2]:0x%x, [3]:0x%x, [4]:0x%x, [5]:0x%x, [6]:0x%x, [7]:0x%x, [8]:0x%x, [9]:0x%x, [10]:0x%x, [11]:0x%x",
            __func__, tmpInfo[0], tmpInfo[1], tmpInfo[2], tmpInfo[3], tmpInfo[4], tmpInfo[5], tmpInfo[6],
                        tmpInfo[7], tmpInfo[8], tmpInfo[9], tmpInfo[10], tmpInfo[11]);

  if (is_capability) return A2DP_SUCCESS;

  if (A2DP_BitsSet(p_ie->sampleRate) != A2DP_SET_ONE_BIT)
    return A2DP_BAD_SAMP_FREQ;

  return A2DP_SUCCESS;
}

// Build the LHDC Media Payload Header.
// |p_dst| points to the location where the header should be written to.
// If |frag| is true, the media payload frame is fragmented.
// |start| is true for the first packet of a fragmented frame.
// |last| is true for the last packet of a fragmented frame.
// If |frag| is false, |num| is the number of number of frames in the packet,
// otherwise is the number of remaining fragments (including this one).
/*
static void A2DP_BuildMediaPayloadHeaderLhdc(uint8_t* p, uint16_t num) {
  if (p == NULL) return;
  *p = ( uint8_t)( num & 0xff);
}
*/

static bool A2DP_MaxBitRatetoQualityLevelLhdcV2(uint8_t *mode, uint8_t bitrate) {
  if (mode == nullptr) {
    LOG_ERROR( "%s: nullptr input", __func__);
    return false;
  }

  switch (bitrate & A2DP_LHDC_MAX_BIT_RATE_MASK) {
  case A2DP_LHDC_MAX_BIT_RATE_900K:
    *mode = A2DP_LHDC_QUALITY_HIGH;
    return true;
  case A2DP_LHDC_MAX_BIT_RATE_500K:
    *mode = A2DP_LHDC_QUALITY_MID;
    return true;
  case A2DP_LHDC_MAX_BIT_RATE_400K:
    *mode = A2DP_LHDC_QUALITY_LOW;
    return true;
  }
  return false;
}

static std::string lhdcV2_QualityModeBitRate_toString(uint8_t value) {
  switch((int)value)
  {
  case A2DP_LHDC_QUALITY_ABR:
    return "ABR";
  case A2DP_LHDC_QUALITY_HIGH1:
    return "HIGH 1 (1000 Kbps)";
  case A2DP_LHDC_QUALITY_HIGH:
    return "HIGH (900 Kbps)";
  case A2DP_LHDC_QUALITY_MID:
    return "MID (500 Kbps)";
  case A2DP_LHDC_QUALITY_LOW:
    return "LOW (400 Kbps)";
  case A2DP_LHDC_QUALITY_LOW4:
    return "LOW 4 (320 Kbps)";
  case A2DP_LHDC_QUALITY_LOW3:
    return "LOW 3 (256 Kbps)";
  case A2DP_LHDC_QUALITY_LOW2:
    return "LOW 2 (192 Kbps)";
  case A2DP_LHDC_QUALITY_LOW1:
    return "LOW 1 (128 Kbps)";
  case A2DP_LHDC_QUALITY_LOW0:
    return "LOW 0 (64 Kbps)";
  default:
    return "Unknown Bit Rate Mode";
  }
}

bool A2DP_IsVendorSourceCodecValidLhdcV2(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE cfg_cie;

  /* Use a liberal check when parsing the codec info */
  return (A2DP_ParseInfoLhdcV2(&cfg_cie, p_codec_info, false) == A2DP_SUCCESS) ||
         (A2DP_ParseInfoLhdcV2(&cfg_cie, p_codec_info, true) == A2DP_SUCCESS);
}

bool A2DP_IsVendorPeerSinkCodecValidLhdcV2(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE cfg_cie;

  /* Use a liberal check when parsing the codec info */
  return (A2DP_ParseInfoLhdcV2(&cfg_cie, p_codec_info, false) == A2DP_SUCCESS) ||
         (A2DP_ParseInfoLhdcV2(&cfg_cie, p_codec_info, true) == A2DP_SUCCESS);
}

// Checks whether A2DP LHDC codec configuration matches with a device's codec
// capabilities. |p_cap| is the LHDC codec configuration. |p_codec_info| is
// the device's codec capabilities.
// If |is_capability| is true, the byte sequence is codec capabilities,
// otherwise is codec configuration.
// |p_codec_info| contains the codec capabilities for a peer device that
// is acting as an A2DP source.
// Returns A2DP_SUCCESS if the codec configuration matches with capabilities,
// otherwise the corresponding A2DP error status code.
static tA2DP_STATUS A2DP_CodecInfoMatchesCapabilityLhdcV2(
    const tA2DP_LHDC_CIE* p_cap, const uint8_t* p_codec_info,
    bool is_capability) {
  tA2DP_STATUS status;
  tA2DP_LHDC_CIE cfg_cie;

  /* parse configuration */
  status = A2DP_ParseInfoLhdcV2(&cfg_cie, p_codec_info, is_capability);
  if (status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: parsing failed %d", __func__, status);
    return status;
  }

  /* verify that each parameter is in range */

  LOG_DEBUG( "%s: FREQ peer: 0x%x, capability 0x%x", __func__,
            cfg_cie.sampleRate, p_cap->sampleRate);

  LOG_DEBUG( "%s: BIT_FMT peer: 0x%x, capability 0x%x", __func__,
            cfg_cie.bits_per_sample, p_cap->bits_per_sample);

  /* sampling frequency */
  if ((cfg_cie.sampleRate & p_cap->sampleRate) == 0) return A2DP_NS_SAMP_FREQ;

  /* bit per sample */
  if ((cfg_cie.bits_per_sample & p_cap->bits_per_sample) == 0) return A2DP_NS_CH_MODE;

  return A2DP_SUCCESS;
}

bool A2DP_VendorUsesRtpHeaderLhdcV2(UNUSED_ATTR bool content_protection_enabled,
                                  UNUSED_ATTR const uint8_t* p_codec_info) {
  // TODO: Is this correct? The RTP header is always included?
  return true;
}

const char* A2DP_VendorCodecNameLhdcV2(UNUSED_ATTR const uint8_t* p_codec_info) {
  return "LHDC V2";
}

bool A2DP_VendorCodecTypeEqualsLhdcV2(const uint8_t* p_codec_info_a,
                                    const uint8_t* p_codec_info_b) {
  tA2DP_LHDC_CIE lhdc_cie_a;
  tA2DP_LHDC_CIE lhdc_cie_b;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status =
      A2DP_ParseInfoLhdcV2(&lhdc_cie_a, p_codec_info_a, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }
  a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie_b, p_codec_info_b, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }

  return true;
}

bool A2DP_VendorCodecEqualsLhdcV2(const uint8_t* p_codec_info_a,
                                const uint8_t* p_codec_info_b) {
  tA2DP_LHDC_CIE lhdc_cie_a;
  tA2DP_LHDC_CIE lhdc_cie_b;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status =
      A2DP_ParseInfoLhdcV2(&lhdc_cie_a, p_codec_info_a, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }
  a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie_b, p_codec_info_b, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }

  return (lhdc_cie_a.sampleRate == lhdc_cie_b.sampleRate) &&
         (lhdc_cie_a.bits_per_sample == lhdc_cie_b.bits_per_sample);
}

// Savitech Patch - START  Offload
int A2DP_VendorGetBitRateLhdcV2(const uint8_t* p_codec_info) {

  A2dpCodecConfig* current_codec = bta_av_get_a2dp_current_codec();
  btav_a2dp_codec_config_t codec_config_ = current_codec->getCodecConfig();
  uint8_t bitRateIndex = 0;

  if ((codec_config_.codec_specific_1 & A2DP_LHDC_VENDOR_CMD_MASK) ==
      A2DP_LHDC_QUALITY_MAGIC_NUM) {
    bitRateIndex = codec_config_.codec_specific_1 & A2DP_LHDC_QUALITY_MASK;
    switch (bitRateIndex) {
      case A2DP_LHDC_QUALITY_LOW0:
        return 64000;
      case A2DP_LHDC_QUALITY_LOW1:
        return 128000;
      case A2DP_LHDC_QUALITY_LOW2:
        return 192000;
      case A2DP_LHDC_QUALITY_LOW3:
        return 256000;
      case A2DP_LHDC_QUALITY_LOW4:
        return 320000;
      case A2DP_LHDC_QUALITY_LOW:
        return 400000;
      case A2DP_LHDC_QUALITY_MID:
        return 500000;
      case A2DP_LHDC_QUALITY_HIGH:
        return 900000;
      case A2DP_LHDC_QUALITY_ABR:
        return 9999999;
      case A2DP_LHDC_QUALITY_HIGH1:
      default:
        LOG_DEBUG("%s: non-supported bitrate index (%d)", __func__, bitRateIndex);
        return -1;
    }
  }
  return 400000;
}
// Savitech Patch - END

int A2DP_VendorGetTrackSampleRateLhdcV2(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }

  switch (lhdc_cie.sampleRate) {
    case A2DP_LHDC_SAMPLING_FREQ_44100:
      return 44100;
    case A2DP_LHDC_SAMPLING_FREQ_48000:
      return 48000;
    case A2DP_LHDC_SAMPLING_FREQ_88200:
      return 88200;
    case A2DP_LHDC_SAMPLING_FREQ_96000:
      return 96000;
  }

  return -1;
}

int A2DP_VendorGetTrackBitsPerSampleLhdcV2(const uint8_t* p_codec_info) {
    tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }

  switch (lhdc_cie.bits_per_sample) {
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
      return 16;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
      return 24;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
      return 32;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE:
      return -1;
  }
}

int A2DP_VendorGetTrackChannelCountLhdcV2(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }
  return 2;
}

int A2DP_VendorGetChannelModeCodeLhdcV2(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }
  return A2DP_LHDC_CHANNEL_MODE_STEREO;
}

bool A2DP_VendorGetPacketTimestampLhdcV2(UNUSED_ATTR const uint8_t* p_codec_info,
                                       const uint8_t* p_data,
                                       uint32_t* p_timestamp) {
  // TODO: Is this function really codec-specific?
  *p_timestamp = *(const uint32_t*)p_data;
  return true;
}

int16_t A2DP_VendorGetMaxDatarateLhdcV2(const uint8_t* p_codec_info){
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }

/*
#define A2DP_LHDC_MAX_BIT_RATE_900K       0x00
#define A2DP_LHDC_MAX_BIT_RATE_500K       0x10
#define A2DP_LHDC_MAX_BIT_RATE_400K       0x20

#define A2DP_LHDC_QUALITY_HIGH   7  // Equal to LHDCBT_EQMID_HQ 900kbps
#define A2DP_LHDC_QUALITY_MID    6   // Equal to LHDCBT_EQMID_SQ 500/560kbps
#define A2DP_LHDC_QUALITY_LOW    5   // Equal to LHDCBT_EQMID_MQ 400kbps
*/
  switch (lhdc_cie.maxTargetBitrate & A2DP_LHDC_MAX_BIT_RATE_MASK) {
      case A2DP_LHDC_MAX_BIT_RATE_900K:
      return A2DP_LHDC_QUALITY_HIGH;
      case A2DP_LHDC_MAX_BIT_RATE_500K:
      return A2DP_LHDC_QUALITY_MID;
      case A2DP_LHDC_MAX_BIT_RATE_400K:
      return A2DP_LHDC_QUALITY_LOW;
  }
  return -1;
}

bool A2DP_VendorGetLowLatencyStateLhdcV2(const uint8_t* p_codec_info){
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }

  LOG_INFO( "%s: isLLSupported =%d", __func__, lhdc_cie.isLLSupported);

  return lhdc_cie.isLLSupported ? true : false;
}

uint8_t A2DP_VendorGetVersionLhdcV2(const uint8_t* p_codec_info){
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }

  LOG_INFO( "%s: version =%d", __func__, lhdc_cie.version);

  return lhdc_cie.version;
}


int8_t A2DP_VendorGetChannelSplitModeLhdcV2(const uint8_t* p_codec_info){
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }

  LOG_INFO( "%s: channelSplitMode =%d", __func__, lhdc_cie.channelSplitMode);

  return lhdc_cie.channelSplitMode;
}

bool A2DP_VendorBuildCodecHeaderLhdcV2(UNUSED_ATTR const uint8_t* p_codec_info,
                                     BT_HDR* p_buf,
                                     uint16_t frames_per_packet) {
  uint8_t* p;

  p_buf->offset -= A2DP_LHDC_MPL_HDR_LEN;
  p = (uint8_t*)(p_buf + 1) + p_buf->offset;
  p_buf->len += A2DP_LHDC_MPL_HDR_LEN;
  p[0] = ( uint8_t)( frames_per_packet & 0xff);
  p[1] = ( uint8_t)( ( frames_per_packet >> 8) & 0xff);
  //A2DP_BuildMediaPayloadHeaderLhdc(p, frames_per_packet);
  return true;
}

void A2DP_VendorDumpCodecInfoLhdcV2(const uint8_t* p_codec_info) {
  tA2DP_STATUS a2dp_status;
  tA2DP_LHDC_CIE lhdc_cie;

  LOG_DEBUG( "%s", __func__);

  a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: A2DP_ParseInfoLhdcV2 fail:%d", __func__, a2dp_status);
    return;
  }

  LOG_DEBUG( "\tsamp_freq: 0x%x", lhdc_cie.sampleRate);
  if (lhdc_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100) {
    LOG_DEBUG( "\tsamp_freq: (44100)");
  }
  if (lhdc_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000) {
    LOG_DEBUG( "\tsamp_freq: (48000)");
  }
  if (lhdc_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_88200) {
    LOG_DEBUG( "\tsamp_freq: (88200)");
  }
  if (lhdc_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000) {
    LOG_DEBUG( "\tsamp_freq: (96000)");
  }
}

std::string A2DP_VendorCodecInfoStringLhdcV2(const uint8_t* p_codec_info) {
  std::stringstream res;
  std::string field;
  tA2DP_STATUS a2dp_status;
  tA2DP_LHDC_CIE lhdc_cie;

  a2dp_status = A2DP_ParseInfoLhdcV2(&lhdc_cie, p_codec_info, true);
  if (a2dp_status != A2DP_SUCCESS) {
    res << "A2DP_ParseInfoLhdcV2 fail: " << loghex(a2dp_status);
    return res.str();
  }

  res << "\tname: LHDC\n";

  // Sample frequency
  field.clear();
  AppendField(&field, (lhdc_cie.sampleRate == 0), "NONE");
  AppendField(&field, (lhdc_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100),
              "44100");
  AppendField(&field, (lhdc_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000),
              "48000");
  AppendField(&field, (lhdc_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_88200),
              "88200");
  AppendField(&field, (lhdc_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000),
              "96000");
  res << "\tsamp_freq: " << field << " (" << loghex(lhdc_cie.sampleRate)
      << ")\n";

  // Channel mode
  field.clear();
  AppendField(&field, 1,
             "Stereo");
  res << "\tch_mode: " << field << " (" << "Only support stereo."
      << ")\n";

  // bits per sample
  field.clear();
  AppendField(&field, (lhdc_cie.bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16),
              "16");
  AppendField(&field, (lhdc_cie.bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24),
              "24");
  res << "\tbits_depth: " << field << " bits (" << loghex((int)lhdc_cie.bits_per_sample)
      << ")\n";

  // Max data rate...
  field.clear();
  AppendField(&field, ((lhdc_cie.maxTargetBitrate & A2DP_LHDC_MAX_BIT_RATE_MASK) == A2DP_LHDC_MAX_BIT_RATE_900K),
              "900Kbps");
  AppendField(&field, ((lhdc_cie.maxTargetBitrate & A2DP_LHDC_MAX_BIT_RATE_MASK) == A2DP_LHDC_MAX_BIT_RATE_500K),
              "500Kbps");
  AppendField(&field, ((lhdc_cie.maxTargetBitrate & A2DP_LHDC_MAX_BIT_RATE_MASK) == A2DP_LHDC_MAX_BIT_RATE_400K),
              "400Kbps");
  res << "\tMax target-rate: " << field << " (" << loghex((lhdc_cie.maxTargetBitrate & A2DP_LHDC_MAX_BIT_RATE_MASK))
      << ")\n";

  // Version
  field.clear();
  AppendField(&field, (lhdc_cie.version <= A2DP_LHDC_VER2),
              "LHDC V2");
  res << "\tversion: " << field << " (" << loghex(lhdc_cie.version)
      << ")\n";


  /*
  field.clear();
  AppendField(&field, 0, "NONE");
  AppendField(&field, 0,
              "Mono");
  AppendField(&field, 0,
              "Dual");
  AppendField(&field, 1,
              "Stereo");
  res << "\tch_mode: " << field << " (" << loghex(lhdc_cie.channelMode)
      << ")\n";
*/
  return res.str();
}

const tA2DP_ENCODER_INTERFACE* A2DP_VendorGetEncoderInterfaceLhdcV2(
    const uint8_t* p_codec_info) {
  if (!A2DP_IsVendorSourceCodecValidLhdcV2(p_codec_info)) return NULL;

  return &a2dp_encoder_interface_lhdcv2;
}

bool A2DP_VendorAdjustCodecLhdcV2(uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE cfg_cie;

  // Nothing to do: just verify the codec info is valid
  if (A2DP_ParseInfoLhdcV2(&cfg_cie, p_codec_info, true) != A2DP_SUCCESS)
    return false;

  return true;
}

btav_a2dp_codec_index_t A2DP_VendorSourceCodecIndexLhdcV2(
    UNUSED_ATTR const uint8_t* p_codec_info) {
  return BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV2;
}

const char* A2DP_VendorCodecIndexStrLhdcV2(void) { return "LHDC V2"; }

bool A2DP_VendorInitCodecConfigLhdcV2(AvdtpSepConfig* p_cfg) {
  if (A2DP_BuildInfoLhdcV2(AVDT_MEDIA_TYPE_AUDIO, &a2dp_lhdc_source_caps,
                         p_cfg->codec_info) != A2DP_SUCCESS) {
    return false;
  }

#if (BTA_AV_CO_CP_SCMS_T == TRUE)
  /* Content protection info - support SCMS-T */
  uint8_t* p = p_cfg->protect_info;
  *p++ = AVDT_CP_LOSC;
  UINT16_TO_STREAM(p, AVDT_CP_SCMS_T_ID);
  p_cfg->num_protect = 1;
#endif

  return true;
}

UNUSED_ATTR static void build_codec_config(const tA2DP_LHDC_CIE& config_cie,
                                           btav_a2dp_codec_config_t* result) {
  if (config_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100)
    result->sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
  if (config_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000)
    result->sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
  if (config_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_88200)
    result->sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_88200;
  if (config_cie.sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000)
    result->sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_96000;

  result->bits_per_sample = config_cie.bits_per_sample;

  result->channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
}

A2dpCodecConfigLhdcV2::A2dpCodecConfigLhdcV2(
    btav_a2dp_codec_priority_t codec_priority)
    : A2dpCodecConfig(BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV2, "LHDC V2",
                      codec_priority) {
  // Compute the local capability
  if (a2dp_lhdc_source_caps.sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100) {
    codec_local_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
  }
  if (a2dp_lhdc_source_caps.sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000) {
    codec_local_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
  }
  if (a2dp_lhdc_source_caps.sampleRate & A2DP_LHDC_SAMPLING_FREQ_88200) {
    codec_local_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_88200;
  }
  if (a2dp_lhdc_source_caps.sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000) {
    codec_local_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
  }
  codec_local_capability_.bits_per_sample = a2dp_lhdc_source_caps.bits_per_sample;

  codec_local_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
}

A2dpCodecConfigLhdcV2::~A2dpCodecConfigLhdcV2() {}

bool A2dpCodecConfigLhdcV2::init() {
  if (!isValid()) return false;

  // Load the encoder
  if (!A2DP_VendorLoadEncoderLhdcV2()) {
    LOG_ERROR( "%s: cannot load the encoder", __func__);
    return false;
  }

  return true;
}

bool A2dpCodecConfigLhdcV2::useRtpHeaderMarkerBit() const { return false; }

//
// Selects the best sample rate from |sampleRate|.
// The result is stored in |p_result| and |p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_best_sample_rate(uint8_t sampleRate,
                                    tA2DP_LHDC_CIE* p_result,
                                    btav_a2dp_codec_config_t* p_codec_config) {
  if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000) {
    p_result->sampleRate = A2DP_LHDC_SAMPLING_FREQ_96000;
    p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
    return true;
  }
  if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_88200) {
    p_result->sampleRate = A2DP_LHDC_SAMPLING_FREQ_88200;
    p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_88200;
    return true;
  }
  if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000) {
    p_result->sampleRate = A2DP_LHDC_SAMPLING_FREQ_48000;
    p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
    return true;
  }
  if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100) {
    p_result->sampleRate = A2DP_LHDC_SAMPLING_FREQ_44100;
    p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
    return true;
  }
  return false;
}

//
// Selects the audio sample rate from |p_codec_audio_config|.
// |sampleRate| contains the capability.
// The result is stored in |p_result| and |p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_audio_sample_rate(
    const btav_a2dp_codec_config_t* p_codec_audio_config, uint8_t sampleRate,
    tA2DP_LHDC_CIE* p_result, btav_a2dp_codec_config_t* p_codec_config) {
  switch (p_codec_audio_config->sample_rate) {
    case BTAV_A2DP_CODEC_SAMPLE_RATE_44100:
      if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100) {
        p_result->sampleRate = A2DP_LHDC_SAMPLING_FREQ_44100;
        p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_48000:
      if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000) {
        p_result->sampleRate = A2DP_LHDC_SAMPLING_FREQ_48000;
        p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_88200:
      if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_88200) {
        p_result->sampleRate = A2DP_LHDC_SAMPLING_FREQ_88200;
        p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_88200;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_96000:
      if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000) {
        p_result->sampleRate = A2DP_LHDC_SAMPLING_FREQ_96000;
        p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_16000:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_24000:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_176400:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_192000:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_NONE:
      break;
  }
  return false;
}

//
// Selects the best bits per sample from |bits_per_sample|.
// |bits_per_sample| contains the capability.
// The result is stored in |p_result| and |p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_best_bits_per_sample(
    btav_a2dp_codec_bits_per_sample_t bits_per_sample, tA2DP_LHDC_CIE* p_result,
    btav_a2dp_codec_config_t* p_codec_config) {
  if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24) {
    p_codec_config->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;
    p_result->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;
    return true;
  }
  if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16) {
    p_codec_config->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
    p_result->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
    return true;
  }
  return false;
}

//
// Selects the audio bits per sample from |p_codec_audio_config|.
// |bits_per_sample| contains the capability.
// The result is stored in |p_result| and |p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_audio_bits_per_sample(
    const btav_a2dp_codec_config_t* p_codec_audio_config,
    btav_a2dp_codec_bits_per_sample_t bits_per_sample, tA2DP_LHDC_CIE* p_result,
    btav_a2dp_codec_config_t* p_codec_config) {
  switch (p_codec_audio_config->bits_per_sample) {
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
      if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16) {
        p_codec_config->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
        p_result->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
      if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24) {
        p_codec_config->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;
        p_result->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE:
      break;
  }
  return false;
}

bool A2dpCodecConfigLhdcV2::copySinkCapability(uint8_t * codec_info){
    std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
    memcpy(codec_info, ota_codec_peer_capability_, AVDT_CODEC_SIZE);
    return true;
}

bool A2dpCodecConfigLhdcV2::setCodecConfig(const uint8_t* p_peer_codec_info,
                                         bool is_capability,
                                         uint8_t* p_result_codec_config) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  tA2DP_LHDC_CIE sink_info_cie;
  tA2DP_LHDC_CIE result_config_cie;
  uint8_t sampleRate;
  uint8_t qualityMode = 0;
  uint8_t bitRateQmode = 0;
  bool isLLEnabled;
  btav_a2dp_codec_bits_per_sample_t bits_per_sample;

  // Save the internal state
  btav_a2dp_codec_config_t saved_codec_config = codec_config_;
  btav_a2dp_codec_config_t saved_codec_capability = codec_capability_;
  btav_a2dp_codec_config_t saved_codec_selectable_capability =
      codec_selectable_capability_;
  btav_a2dp_codec_config_t saved_codec_user_config = codec_user_config_;
  btav_a2dp_codec_config_t saved_codec_audio_config = codec_audio_config_;
  uint8_t saved_ota_codec_config[AVDT_CODEC_SIZE];
  uint8_t saved_ota_codec_peer_capability[AVDT_CODEC_SIZE];
  uint8_t saved_ota_codec_peer_config[AVDT_CODEC_SIZE];
  memcpy(saved_ota_codec_config, ota_codec_config_, sizeof(ota_codec_config_));
  memcpy(saved_ota_codec_peer_capability, ota_codec_peer_capability_,
         sizeof(ota_codec_peer_capability_));
  memcpy(saved_ota_codec_peer_config, ota_codec_peer_config_,
         sizeof(ota_codec_peer_config_));

  tA2DP_STATUS status =
      A2DP_ParseInfoLhdcV2(&sink_info_cie, p_peer_codec_info, is_capability);
  if (status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: can't parse peer's Sink capabilities: error = %d",
              __func__, status);
    goto fail;
  }

  //
  // Build the preferred configuration
  //
  memset(&result_config_cie, 0, sizeof(result_config_cie));
  result_config_cie.vendorId = a2dp_lhdc_source_caps.vendorId;
  result_config_cie.codecId = a2dp_lhdc_source_caps.codecId;

  if (sink_info_cie.version > a2dp_lhdc_source_caps.version) {
    LOG_ERROR( "%s: Sink capbility version miss match! peer(%d), host(%d)",
              __func__, sink_info_cie.version, a2dp_lhdc_source_caps.version);
    goto fail;
  }

  result_config_cie.version = sink_info_cie.version;

  if (sink_info_cie.channelSplitMode & A2DP_LHDC_CH_SPLIT_TWS) {
      result_config_cie.channelSplitMode = A2DP_LHDC_CH_SPLIT_TWS;
  }else{
      result_config_cie.channelSplitMode = A2DP_LHDC_CH_SPLIT_NONE;
  }


  isLLEnabled = (a2dp_lhdc_source_caps.isLLSupported & sink_info_cie.isLLSupported);
  result_config_cie.isLLSupported = false;
  switch (codec_user_config_.codec_specific_2 & 0x1ULL) {
    case A2DP_LHDC_LL_ENABLE:
    if (isLLEnabled) {
      result_config_cie.isLLSupported = true;
      codec_config_.codec_specific_2 |= 0x1ULL;
    }
    break;
    case A2DP_LHDC_LL_DISABLE:
      result_config_cie.isLLSupported = false;
      codec_config_.codec_specific_2 &= ~0x1ULL;
    break;
  }
  //result_config_cie.isLLSupported = sink_info_cie.isLLSupported;
  LOG_ERROR( "%s: isLLSupported, Sink(0x%02x) Set(0x%08x), result(0x%02x)", __func__,
                                sink_info_cie.isLLSupported,
                                (uint32_t)codec_user_config_.codec_specific_2,
                                result_config_cie.isLLSupported);

  //
  // Select the sample frequency
  //
  sampleRate = a2dp_lhdc_source_caps.sampleRate & sink_info_cie.sampleRate;
  LOG_ERROR( "%s: samplrate = 0x%x", __func__, sampleRate);
  codec_config_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_NONE;
  switch (codec_user_config_.sample_rate) {
    case BTAV_A2DP_CODEC_SAMPLE_RATE_44100:
      if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100) {
        result_config_cie.sampleRate = A2DP_LHDC_SAMPLING_FREQ_44100;
        codec_capability_.sample_rate = codec_user_config_.sample_rate;
        codec_config_.sample_rate = codec_user_config_.sample_rate;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_48000:
      if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000) {
        result_config_cie.sampleRate = A2DP_LHDC_SAMPLING_FREQ_48000;
        codec_capability_.sample_rate = codec_user_config_.sample_rate;
        codec_config_.sample_rate = codec_user_config_.sample_rate;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_88200:
      if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_88200) {
        result_config_cie.sampleRate = A2DP_LHDC_SAMPLING_FREQ_88200;
        codec_capability_.sample_rate = codec_user_config_.sample_rate;
        codec_config_.sample_rate = codec_user_config_.sample_rate;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_96000:
      if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000) {
        result_config_cie.sampleRate = A2DP_LHDC_SAMPLING_FREQ_96000;
        codec_capability_.sample_rate = codec_user_config_.sample_rate;
        codec_config_.sample_rate = codec_user_config_.sample_rate;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_16000:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_24000:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_176400:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_192000:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_NONE:
      codec_capability_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_NONE;
      codec_config_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_NONE;
      break;
  }

  // Select the sample frequency if there is no user preference
  do {
    // Compute the selectable capability
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100) {
      codec_selectable_capability_.sample_rate |=
          BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
    }
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000) {
      codec_selectable_capability_.sample_rate |=
          BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
    }
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_88200) {
      codec_selectable_capability_.sample_rate |=
          BTAV_A2DP_CODEC_SAMPLE_RATE_88200;
    }
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000) {
      codec_selectable_capability_.sample_rate |=
          BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
    }

    if (codec_config_.sample_rate != BTAV_A2DP_CODEC_SAMPLE_RATE_NONE) break;

    // Compute the common capability
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100)
      codec_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000)
      codec_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_88200)
      codec_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_88200;
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000)
      codec_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_96000;

    // No user preference - try the codec audio config
    if (select_audio_sample_rate(&codec_audio_config_, sampleRate,
                                 &result_config_cie, &codec_config_)) {
      break;
    }

    // No user preference - try the default config
    if (select_best_sample_rate(
            a2dp_lhdc_default_config.sampleRate & sink_info_cie.sampleRate,
            &result_config_cie, &codec_config_)) {
      break;
    }

    // No user preference - use the best match
    if (select_best_sample_rate(sampleRate, &result_config_cie,
                                &codec_config_)) {
      break;
    }
  } while (false);
  if (codec_config_.sample_rate == BTAV_A2DP_CODEC_SAMPLE_RATE_NONE) {
    LOG_ERROR(
              "%s: cannot match sample frequency: source caps = 0x%x "
              "sink info = 0x%x",
              __func__, a2dp_lhdc_source_caps.sampleRate, sink_info_cie.sampleRate);
    goto fail;
  }

  //
  // Select the bits per sample
  //
  // NOTE: this information is NOT included in the LHDC A2DP codec description
  // that is sent OTA.
  bits_per_sample = a2dp_lhdc_source_caps.bits_per_sample & sink_info_cie.bits_per_sample;
  LOG_ERROR( "%s: a2dp_lhdc_source_caps.bits_per_sample = 0x%02x, sink_info_cie.bits_per_sample = 0x%02x", __func__, a2dp_lhdc_source_caps.bits_per_sample, sink_info_cie.bits_per_sample);
  codec_config_.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;
  switch (codec_user_config_.bits_per_sample) {
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
      if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16) {
        result_config_cie.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
        codec_capability_.bits_per_sample = codec_user_config_.bits_per_sample;
        codec_config_.bits_per_sample = codec_user_config_.bits_per_sample;
      }
      break;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
      if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24) {
        result_config_cie.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;
        codec_capability_.bits_per_sample = codec_user_config_.bits_per_sample;
        codec_config_.bits_per_sample = codec_user_config_.bits_per_sample;
      }
      break;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE:
      result_config_cie.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;
      codec_capability_.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;
      codec_config_.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;
      break;
  }

  // Select the bits per sample if there is no user preference
  do {
    // Compute the selectable capability
      // Compute the selectable capability
      if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16)
        codec_selectable_capability_.bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
      if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24)
        codec_selectable_capability_.bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;

    if (codec_config_.bits_per_sample != BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE) break;

    // Compute the common capability
    if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16)
      codec_capability_.bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
    if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24)
      codec_capability_.bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;

    // No user preference - the the codec audio config
    if (select_audio_bits_per_sample(&codec_audio_config_, bits_per_sample,
                                     &result_config_cie, &codec_config_)) {
      break;
    }

    // No user preference - try the default config
    if (select_best_bits_per_sample(a2dp_lhdc_default_config.bits_per_sample & sink_info_cie.bits_per_sample,
                                    &result_config_cie, &codec_config_)) {
      break;
    }

    // No user preference - use the best match
    if (select_best_bits_per_sample(bits_per_sample,
                                    &result_config_cie, &codec_config_)) {
      break;
    }
  } while (false);
  if (codec_config_.bits_per_sample == BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE) {
    LOG_ERROR(
              "%s: cannot match bits per sample: default = 0x%x "
              "user preference = 0x%x",
              __func__, a2dp_lhdc_default_config.bits_per_sample,
              codec_user_config_.bits_per_sample);
    goto fail;
  }

  //
  // Select the channel mode
  //
  LOG_ERROR( "%s: channelMode = Only supported stereo", __func__);
  codec_config_.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_NONE;
  switch (codec_user_config_.channel_mode) {
    case BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
      codec_capability_.channel_mode = codec_user_config_.channel_mode;
      codec_config_.channel_mode = codec_user_config_.channel_mode;
      break;
    case BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
    case BTAV_A2DP_CODEC_CHANNEL_MODE_NONE:
      codec_capability_.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_NONE;
      codec_config_.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_NONE;
      break;
  }
  codec_selectable_capability_.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
  codec_capability_.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
  codec_config_.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
  if (codec_config_.channel_mode == BTAV_A2DP_CODEC_CHANNEL_MODE_NONE) {
    LOG_ERROR(
              "%s: codec_config_.channel_mode != BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO "
              , __func__);
    goto fail;
  }

  result_config_cie.maxTargetBitrate = sink_info_cie.maxTargetBitrate;

  LOG_DEBUG("%s: Config bitrate result(0x%02x), prev(0x%02x)", __func__, result_config_cie.maxTargetBitrate, sink_info_cie.maxTargetBitrate);


  result_config_cie.channelSplitMode = sink_info_cie.channelSplitMode;
  LOG_ERROR("%s: channelSplitMode = %d", __func__, result_config_cie.channelSplitMode);

  // quality mode (BitRate) adjust
  if ((codec_user_config_.codec_specific_1 & A2DP_LHDC_VENDOR_CMD_MASK) != A2DP_LHDC_QUALITY_MAGIC_NUM) {
    codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_ABR;
    LOG_DEBUG( "%s: tag not match, use default Mode: ABR", __func__);
  }
  qualityMode = (uint8_t)codec_user_config_.codec_specific_1 & A2DP_LHDC_QUALITY_MASK;

  //
  // quality mode adjust when non-ABR
  //
  if (qualityMode != A2DP_LHDC_QUALITY_ABR) {
    // get corresponding quality mode of the max target bit rate
    if (!A2DP_MaxBitRatetoQualityLevelLhdcV2(&bitRateQmode, result_config_cie.maxTargetBitrate)) {
      LOG_ERROR( "%s: get quality mode from maxTargetBitrate error", __func__);
      goto fail;
    }
    // downgrade audio quality according to the max target bit rate
    if (qualityMode > bitRateQmode) {
      codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | bitRateQmode;
      qualityMode = bitRateQmode;
      LOG_DEBUG( "%s: downgrade quality mode to 0x%02X by max target bitrate", __func__, qualityMode);
    }

    // (1000K) does not supported in V2, reset to 900K
    if (qualityMode == A2DP_LHDC_QUALITY_HIGH1) {
      LOG_DEBUG( "%s: set non-supported quality mode %s to %s", __func__,
          lhdcV2_QualityModeBitRate_toString(qualityMode).c_str(),
          lhdcV2_QualityModeBitRate_toString(A2DP_LHDC_QUALITY_HIGH).c_str());
      codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_HIGH;
      qualityMode = A2DP_LHDC_QUALITY_HIGH;
    }
  }

  LOG_DEBUG( "%s: => final quality mode(0x%02X) = %s", __func__,
      qualityMode,
      lhdcV2_QualityModeBitRate_toString(qualityMode).c_str());

  if (int ret = A2DP_BuildInfoLhdcV2(AVDT_MEDIA_TYPE_AUDIO, &result_config_cie,
                         p_result_codec_config) != A2DP_SUCCESS) {
    LOG_ERROR("%s: A2DP_BuildInfoLhdcV2 fail(0x%x)", __func__, ret);
    goto fail;
  }

  //
  // Copy the codec-specific fields if they are not zero
  //
  if (codec_user_config_.codec_specific_1 != 0)
    codec_config_.codec_specific_1 = codec_user_config_.codec_specific_1;
  if (codec_user_config_.codec_specific_2 != 0)
    codec_config_.codec_specific_2 = codec_user_config_.codec_specific_2;
  if (codec_user_config_.codec_specific_3 != 0)
    codec_config_.codec_specific_3 = codec_user_config_.codec_specific_3;
  //codec_config_.codec_specific_3 = result_config_cie.isLLSupported == true ? 1 : 0;

  if (codec_user_config_.codec_specific_4 != 0)
    codec_config_.codec_specific_4 = codec_user_config_.codec_specific_4;


  // Create a local copy of the peer codec capability, and the
  // result codec config.
    LOG_ERROR("%s: is_capability = %d", __func__, is_capability);
  if (is_capability) {
    status = A2DP_BuildInfoLhdcV2(AVDT_MEDIA_TYPE_AUDIO, &sink_info_cie,
                                ota_codec_peer_capability_);
  } else {
    status = A2DP_BuildInfoLhdcV2(AVDT_MEDIA_TYPE_AUDIO, &sink_info_cie,
                                ota_codec_peer_config_);
  }
  CHECK(status == A2DP_SUCCESS);

  status = A2DP_BuildInfoLhdcV2(AVDT_MEDIA_TYPE_AUDIO, &result_config_cie,
                              ota_codec_config_);
  CHECK(status == A2DP_SUCCESS);
  return true;

fail:
  // Restore the internal state
  codec_config_ = saved_codec_config;
  codec_capability_ = saved_codec_capability;
  codec_selectable_capability_ = saved_codec_selectable_capability;
  codec_user_config_ = saved_codec_user_config;
  codec_audio_config_ = saved_codec_audio_config;
  memcpy(ota_codec_config_, saved_ota_codec_config, sizeof(ota_codec_config_));
  memcpy(ota_codec_peer_capability_, saved_ota_codec_peer_capability,
         sizeof(ota_codec_peer_capability_));
  memcpy(ota_codec_peer_config_, saved_ota_codec_peer_config,
         sizeof(ota_codec_peer_config_));
  return false;
}



bool A2dpCodecConfigLhdcV2::setPeerCodecCapabilities(
                                                   const uint8_t* p_peer_codec_capabilities) {
    std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
    tA2DP_LHDC_CIE peer_info_cie;
    uint8_t sampleRate;
    uint8_t bits_per_sample;

    // Save the internal state
    btav_a2dp_codec_config_t saved_codec_selectable_capability =
    codec_selectable_capability_;
    uint8_t saved_ota_codec_peer_capability[AVDT_CODEC_SIZE];
    memcpy(saved_ota_codec_peer_capability, ota_codec_peer_capability_,
           sizeof(ota_codec_peer_capability_));

    tA2DP_STATUS status =
    A2DP_ParseInfoLhdcV2(&peer_info_cie, p_peer_codec_capabilities, true);
    if (status != A2DP_SUCCESS) {
        LOG_ERROR( "%s: can't parse peer's capabilities: error = %d",
                  __func__, status);
        goto fail;
    }

    if (peer_info_cie.version > a2dp_lhdc_source_caps.version) {
        LOG_ERROR( "%s: can't parse peer's capabilities: Missmatch version(%u:%u)",
                  __func__, a2dp_lhdc_source_caps.version, peer_info_cie.version);
        goto fail;
    }

    codec_selectable_capability_.codec_specific_3 = peer_info_cie.isLLSupported ? 1 : 0;

    // Compute the selectable capability - bits per sample
    //codec_selectable_capability_.bits_per_sample =
    //a2dp_lhdc_source_caps.bits_per_sample;
    bits_per_sample = a2dp_lhdc_source_caps.bits_per_sample & peer_info_cie.bits_per_sample;
    if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16) {
        codec_selectable_capability_.bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
    }
    if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24) {
        codec_selectable_capability_.bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;
    }


    // Compute the selectable capability - sample rate
    sampleRate = a2dp_lhdc_source_caps.sampleRate & peer_info_cie.sampleRate;
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_44100) {
        codec_selectable_capability_.sample_rate |=
        BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
    }
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_48000) {
        codec_selectable_capability_.sample_rate |=
        BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
    }
    if (sampleRate & A2DP_LHDC_SAMPLING_FREQ_96000) {
        codec_selectable_capability_.sample_rate |=
        BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
    }


    // Compute the selectable capability - channel mode
    codec_selectable_capability_.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;

    status = A2DP_BuildInfoLhdcV2(AVDT_MEDIA_TYPE_AUDIO, &peer_info_cie,
                                ota_codec_peer_capability_);
    CHECK(status == A2DP_SUCCESS);
    return true;

fail:
    // Restore the internal state
    codec_selectable_capability_ = saved_codec_selectable_capability;
    memcpy(ota_codec_peer_capability_, saved_ota_codec_peer_capability,
           sizeof(ota_codec_peer_capability_));
    return false;
}
