/******************************************************************************
 *
 *  Copyright 2002-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/******************************************************************************
 *
 *  Utility functions to help build and parse SBC Codec Information Element
 *  and Media Payload.
 *
 ******************************************************************************/

#define LOG_TAG "a2dp_vendor_lhdcv3_dec"

#include "bt_target.h"

#include "a2dp_vendor_lhdcv3_dec.h"

#include <string.h>

#include <base/logging.h>
#include "a2dp_vendor.h"
#include "a2dp_vendor_lhdcv3_decoder.h"
#include "bt_utils.h"
#include "osi/include/log.h"
#include "osi/include/osi.h"


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
  //uint8_t supportedBitrate;
  bool hasFeatureJAS;
  bool hasFeatureAR;
  bool hasFeatureLLAC;
  bool hasFeatureMETA;
  bool hasFeatureMinBitrate;
  bool hasFeatureLARC;
  bool hasFeatureLHDCV4;
} tA2DP_LHDCV3_SINK_CIE;

/* LHDC Sink codec capabilities */
static const tA2DP_LHDCV3_SINK_CIE a2dp_lhdcv3_sink_caps = {
    A2DP_LHDC_VENDOR_ID,  // vendorId
    A2DP_LHDCV3_CODEC_ID,   // codecId
    // sampleRate
    //(A2DP_LHDC_SAMPLING_FREQ_48000),
    (A2DP_LHDC_SAMPLING_FREQ_44100 | A2DP_LHDC_SAMPLING_FREQ_48000 | A2DP_LHDC_SAMPLING_FREQ_88200 | A2DP_LHDC_SAMPLING_FREQ_96000),
    // bits_per_sample
    (BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16 | BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24),
    //Channel Separation
    //A2DP_LHDC_CH_SPLIT_NONE | A2DP_LHDC_CH_SPLIT_TWS,
	A2DP_LHDC_CH_SPLIT_NONE,
    //Version number
    A2DP_LHDC_VER3,
    //Target bit Rate
    A2DP_LHDC_MAX_BIT_RATE_900K,
    //LL supported ?
    true,

    /*******************************
     *  LHDC features/capabilities:
     *  hasFeatureJAS
     *  hasFeatureAR
     *  hasFeatureLLAC
     *  hasFeatureMETA
     *  hasFeatureMinBitrate
     *  hasFeatureLARC
     *  hasFeatureLHDCV4
     *******************************/
    //bool hasFeatureJAS;
    true,

    //bool hasFeatureAR;
    true,

    //bool hasFeatureLLAC;
    true,

    //bool hasFeatureMETA;
    true,

    //bool hasFeatureMinBitrate;
    true,

    //bool hasFeatureLARC;
    false,

    //bool hasFeatureLHDCV4;
    true,
};

/* Default LHDC codec configuration */
static const tA2DP_LHDCV3_SINK_CIE a2dp_lhdcv3_sink_default_config = {
    A2DP_LHDC_VENDOR_ID,                // vendorId
    A2DP_LHDCV3_CODEC_ID,                 // codecId
    A2DP_LHDC_SAMPLING_FREQ_96000,      // sampleRate
    BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24,  // bits_per_sample
    A2DP_LHDC_CH_SPLIT_NONE,
    A2DP_LHDC_VER3,
    A2DP_LHDC_MAX_BIT_RATE_900K,
    false,

    //bool hasFeatureJAS;
    false,

    //bool hasFeatureAR;
    false,

    //bool hasFeatureLLAC;
    true,

    //bool hasFeatureMETA;
    false,

    //bool hasFeatureMinBitrate;
    true,

    //bool hasFeatureLARC;
    false,

    //bool hasFeatureLHDCV4;
    true,
};

static const tA2DP_DECODER_INTERFACE a2dp_decoder_interface_lhdcv3 = {
    a2dp_vendor_lhdcv3_decoder_init,
    a2dp_vendor_lhdcv3_decoder_cleanup,
    a2dp_vendor_lhdcv3_decoder_decode_packet,
    a2dp_vendor_lhdcv3_decoder_start,
    a2dp_vendor_lhdcv3_decoder_suspend,
    a2dp_vendor_lhdcv3_decoder_configure,
};

static tA2DP_STATUS A2DP_CodecInfoMatchesCapabilityLhdcV3Sink(
    const tA2DP_LHDCV3_SINK_CIE* p_cap, const uint8_t* p_codec_info,
    bool is_capability);


// Builds the LHDC Media Codec Capabilities byte sequence beginning from the
// LOSC octet. |media_type| is the media type |AVDT_MEDIA_TYPE_*|.
// |p_ie| is a pointer to the LHDC Codec Information Element information.
// The result is stored in |p_result|. Returns A2DP_SUCCESS on success,
// otherwise the corresponding A2DP error status code.
static tA2DP_STATUS A2DP_BuildInfoLhdcV3Sink(uint8_t media_type,
                                       const tA2DP_LHDCV3_SINK_CIE* p_ie,
                                       uint8_t* p_result) {

  const uint8_t* tmpInfo = p_result;
  if (p_ie == NULL || p_result == NULL) {
    return A2DP_INVALID_PARAMS;
  }

  *p_result++ = A2DP_LHDCV3_CODEC_LEN;    //0
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

  if (p_ie->bits_per_sample == (BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24 | BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16)) {
      para = para | (A2DP_LHDC_BIT_FMT_24 | A2DP_LHDC_BIT_FMT_16);
  }else if(p_ie->bits_per_sample == BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24){
      para = para | A2DP_LHDC_BIT_FMT_24;
  }else if(p_ie->bits_per_sample == BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16){
      para = para | A2DP_LHDC_BIT_FMT_16;
  }

  if (p_ie->hasFeatureJAS)
  {
    para |= A2DP_LHDC_FEATURE_JAS;
  }

  if (p_ie->hasFeatureAR)
  {
    para |= A2DP_LHDC_FEATURE_AR;
  }

  // Save octet 9
  *p_result++ = para;   //9

  para = p_ie->version;

  para |= p_ie->maxTargetBitrate;

  para |= p_ie->isLLSupported ? A2DP_LHDC_LL_SUPPORTED : A2DP_LHDC_LL_NONE;

  if (p_ie->hasFeatureLLAC)
  {
    para |= A2DP_LHDC_FEATURE_LLAC;
  }

  // Save octet 10
  *p_result++ = para;   //a

  //Save octet 11
  para = p_ie->channelSplitMode;

  if (p_ie->hasFeatureMETA)
  {
    para |= A2DP_LHDC_FEATURE_META;
  }

  if (p_ie->hasFeatureMinBitrate)
  {
    para |= A2DP_LHDC_FEATURE_MIN_BR;
  }

  if (p_ie->hasFeatureLARC)
  {
    para |= A2DP_LHDC_FEATURE_LARC;
  }

  if (p_ie->hasFeatureLHDCV4)
  {
    para |= A2DP_LHDC_FEATURE_LHDCV4;
  }

  *p_result++ = para;   //b

  //Save octet 12
  //para = p_ie->supportedBitrate;
  //*p_result++ = para;   //c

  LOG_DEBUG("%s: Info build result = [0]:0x%x, [1]:0x%x, [2]:0x%x, [3]:0x%x, "
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
static tA2DP_STATUS A2DP_ParseInfoLhdcV3Sink(tA2DP_LHDCV3_SINK_CIE* p_ie,
                                       const uint8_t* p_codec_info,
                                       bool is_capability) {
  uint8_t losc;
  uint8_t media_type;
  tA2DP_CODEC_TYPE codec_type;
  const uint8_t* tmpInfo = p_codec_info;
  const uint8_t* p_codec_Info_save = p_codec_info;

  //LOG_DEBUG("%s: p_ie = %p, p_codec_info = %p", __func__, p_ie, p_codec_info);
  if (p_ie == NULL || p_codec_info == NULL) return A2DP_INVALID_PARAMS;

  // Check the codec capability length
  losc = *p_codec_info++;

  if (losc != A2DP_LHDCV3_CODEC_LEN) return A2DP_WRONG_CODEC;

  media_type = (*p_codec_info++) >> 4;
  codec_type = *p_codec_info++;
    //LOG_DEBUG("%s: media_type = %d, codec_type = %d", __func__, media_type, codec_type);
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
  LOG_DEBUG("%s:Vendor(0x%08x), Codec(0x%04x)", __func__, p_ie->vendorId, p_ie->codecId);
  if (p_ie->vendorId != A2DP_LHDC_VENDOR_ID ||
      p_ie->codecId != A2DP_LHDCV3_CODEC_ID) {
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

  //p_codec_info += 1;

  //p_ie->supportedBitrate = (*p_codec_info);




    LOG_DEBUG("%s: codec info = [0]:0x%x, [1]:0x%x, [2]:0x%x, [3]:0x%x, [4]:0x%x, [5]:0x%x, [6]:0x%x, [7]:0x%x, [8]:0x%x, [9]:0x%x, [10]:0x%x, [11]:0x%x",
            __func__, tmpInfo[0], tmpInfo[1], tmpInfo[2], tmpInfo[3], tmpInfo[4], tmpInfo[5], tmpInfo[6],
                        tmpInfo[7], tmpInfo[8], tmpInfo[9], tmpInfo[10], tmpInfo[11]);

  if (is_capability) return A2DP_SUCCESS;

  if (A2DP_BitsSet(p_ie->sampleRate) != A2DP_SET_ONE_BIT)
    return A2DP_BAD_SAMP_FREQ;

  save_codec_info (p_codec_Info_save);

  return A2DP_SUCCESS;
}

const char* A2DP_VendorCodecNameLhdcV3Sink(UNUSED_ATTR const uint8_t* p_codec_info) {
  return "LHDC V3";
}

bool A2DP_IsVendorSinkCodecValidLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDCV3_SINK_CIE cfg_cie;

  /* Use a liberal check when parsing the codec info */
  return (A2DP_ParseInfoLhdcV3Sink(&cfg_cie, p_codec_info, false) == A2DP_SUCCESS) ||
         (A2DP_ParseInfoLhdcV3Sink(&cfg_cie, p_codec_info, true) == A2DP_SUCCESS);
}


bool A2DP_IsVendorPeerSourceCodecValidLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDCV3_SINK_CIE cfg_cie;

  /* Use a liberal check when parsing the codec info */
  return (A2DP_ParseInfoLhdcV3Sink(&cfg_cie, p_codec_info, false) == A2DP_SUCCESS) ||
         (A2DP_ParseInfoLhdcV3Sink(&cfg_cie, p_codec_info, true) == A2DP_SUCCESS);
}


bool A2DP_IsVendorSinkCodecSupportedLhdcV3(const uint8_t* p_codec_info) {
  return (A2DP_CodecInfoMatchesCapabilityLhdcV3Sink(&a2dp_lhdcv3_sink_caps, p_codec_info,
                                             false) == A2DP_SUCCESS);
}

bool A2DP_IsPeerSourceCodecSupportedLhdcV3(const uint8_t* p_codec_info) {
  return (A2DP_CodecInfoMatchesCapabilityLhdcV3Sink(&a2dp_lhdcv3_sink_caps, p_codec_info,
                                             true) == A2DP_SUCCESS);
}

void A2DP_InitDefaultCodecLhdcV3Sink(uint8_t* p_codec_info) {
  LOG_DEBUG("%s: enter", __func__);
  if (A2DP_BuildInfoLhdcV3Sink(AVDT_MEDIA_TYPE_AUDIO, &a2dp_lhdcv3_sink_default_config,
                        p_codec_info) != A2DP_SUCCESS) {
    LOG_ERROR("%s: A2DP_BuildInfoSbc failed", __func__);
  }
}

// Checks whether A2DP SBC codec configuration matches with a device's codec
// capabilities. |p_cap| is the SBC codec configuration. |p_codec_info| is
// the device's codec capabilities. |is_capability| is true if
// |p_codec_info| contains A2DP codec capability.
// Returns A2DP_SUCCESS if the codec configuration matches with capabilities,
// otherwise the corresponding A2DP error status code.
static tA2DP_STATUS A2DP_CodecInfoMatchesCapabilityLhdcV3Sink(
    const tA2DP_LHDCV3_SINK_CIE* p_cap, const uint8_t* p_codec_info,
    bool is_capability) {
  tA2DP_STATUS status;
  tA2DP_LHDCV3_SINK_CIE cfg_cie;

  /* parse configuration */
  status = A2DP_ParseInfoLhdcV3Sink(&cfg_cie, p_codec_info, is_capability);
  if (status != A2DP_SUCCESS) {
    LOG_ERROR("%s: parsing failed %d", __func__, status);
    return status;
  }

  /* verify that each parameter is in range */

  LOG_DEBUG("%s: FREQ peer: 0x%x, capability 0x%x", __func__,
            cfg_cie.sampleRate, p_cap->sampleRate);

  LOG_DEBUG("%s: BIT_FMT peer: 0x%x, capability 0x%x", __func__,
            cfg_cie.bits_per_sample, p_cap->bits_per_sample);

  /* sampling frequency */
  if ((cfg_cie.sampleRate & p_cap->sampleRate) == 0) return A2DP_NS_SAMP_FREQ;

  /* bit per sample */
  if ((cfg_cie.bits_per_sample & p_cap->bits_per_sample) == 0) return A2DP_NS_CH_MODE;

  return A2DP_SUCCESS;
}

bool A2DP_VendorCodecTypeEqualsLhdcV3Sink(const uint8_t* p_codec_info_a,
                                    const uint8_t* p_codec_info_b) {
  tA2DP_LHDCV3_SINK_CIE lhdc_cie_a;
  tA2DP_LHDCV3_SINK_CIE lhdc_cie_b;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status =
      A2DP_ParseInfoLhdcV3Sink(&lhdc_cie_a, p_codec_info_a, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR("%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }
  a2dp_status = A2DP_ParseInfoLhdcV3Sink(&lhdc_cie_b, p_codec_info_b, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR("%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }

  return true;
}

bool A2DP_VendorCodecEqualsLhdcV3Sink(const uint8_t* p_codec_info_a,
                                const uint8_t* p_codec_info_b) {
  tA2DP_LHDCV3_SINK_CIE lhdc_cie_a;
  tA2DP_LHDCV3_SINK_CIE lhdc_cie_b;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status =
      A2DP_ParseInfoLhdcV3Sink(&lhdc_cie_a, p_codec_info_a, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR("%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }
  a2dp_status = A2DP_ParseInfoLhdcV3Sink(&lhdc_cie_b, p_codec_info_b, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR("%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }

  return (lhdc_cie_a.sampleRate == lhdc_cie_b.sampleRate) &&
         (lhdc_cie_a.bits_per_sample == lhdc_cie_b.bits_per_sample) &&
         /*(lhdc_cie_a.supportedBitrate == lhdc_cie_b.supportedBitrate) &&*/
         (lhdc_cie_a.isLLSupported == lhdc_cie_b.isLLSupported);
}


int A2DP_VendorGetTrackSampleRateLhdcV3Sink(const uint8_t* p_codec_info) {
  tA2DP_LHDCV3_SINK_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3Sink(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR("%s: cannot decode codec information: %d", __func__,
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

int A2DP_VendorGetSinkTrackChannelTypeLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDCV3_SINK_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3Sink(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR("%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }

  return A2DP_LHDC_CHANNEL_MODE_STEREO;
}

int A2DP_VendorGetChannelModeCodeLhdcV3Sink(const uint8_t* p_codec_info) {
  tA2DP_LHDCV3_SINK_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3Sink(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR("%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }
  return A2DP_LHDC_CHANNEL_MODE_STEREO;
}

bool A2DP_VendorGetPacketTimestampLhdcV3Sink(UNUSED_ATTR const uint8_t* p_codec_info,
                                       const uint8_t* p_data,
                                       uint32_t* p_timestamp) {
  // TODO: Is this function really codec-specific?
  *p_timestamp = *(const uint32_t*)p_data;
  return true;
}
/*
bool A2DP_VendorBuildCodecHeaderLhdcV3Sink(UNUSED_ATTR const uint8_t* p_codec_info,
                              BT_HDR* p_buf, uint16_t frames_per_packet) {
  uint8_t* p;

  p_buf->offset -= A2DP_SBC_MPL_HDR_LEN;
  p = (uint8_t*)(p_buf + 1) + p_buf->offset;
  p_buf->len += A2DP_SBC_MPL_HDR_LEN;
  A2DP_BuildMediaPayloadHeaderSbc(p, false, false, false,
                                  (uint8_t)frames_per_packet);

  return true;
}
*/
std::string A2DP_VendorCodecInfoStringLhdcV3Sink(const uint8_t* p_codec_info) {
  std::stringstream res;
  std::string field;
  tA2DP_STATUS a2dp_status;
  tA2DP_LHDCV3_SINK_CIE lhdc_cie;

  a2dp_status = A2DP_ParseInfoLhdcV3Sink(&lhdc_cie, p_codec_info, true);
  if (a2dp_status != A2DP_SUCCESS) {
    res << "A2DP_ParseInfoLhdcV3Sink fail: " << loghex(a2dp_status);
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
  AppendField(&field, (lhdc_cie.version == A2DP_LHDC_VER3),
              "LHDC V3");
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

const tA2DP_DECODER_INTERFACE* A2DP_VendorGetDecoderInterfaceLhdcV3(
    const uint8_t* p_codec_info) {
  if (!A2DP_IsVendorSinkCodecValidLhdcV3(p_codec_info)) return NULL;

  return &a2dp_decoder_interface_lhdcv3;
}

bool A2DP_VendorAdjustCodecLhdcV3Sink(uint8_t* p_codec_info) {
  tA2DP_LHDCV3_SINK_CIE cfg_cie;

  // Nothing to do: just verify the codec info is valid
  if (A2DP_ParseInfoLhdcV3Sink(&cfg_cie, p_codec_info, true) != A2DP_SUCCESS)
    return false;

  return true;
}

btav_a2dp_codec_index_t A2DP_VendorSinkCodecIndexLhdcV3(
    UNUSED_ATTR const uint8_t* p_codec_info) {
  return BTAV_A2DP_CODEC_INDEX_SINK_LHDCV3;
}

const char* A2DP_VendorCodecIndexStrLhdcV3Sink(void) { return "LHDC V3 SINK"; }

bool A2DP_VendorInitCodecConfigLhdcV3Sink(AvdtpSepConfig* p_cfg) {
  LOG_DEBUG("%s: enter", __func__);
  if (A2DP_BuildInfoLhdcV3Sink(AVDT_MEDIA_TYPE_AUDIO, &a2dp_lhdcv3_sink_caps,
                        p_cfg->codec_info) != A2DP_SUCCESS) {
    return false;
  }

  return true;
}

UNUSED_ATTR static void build_codec_config(const tA2DP_LHDCV3_SINK_CIE& config_cie,
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





A2dpCodecConfigLhdcV3Sink::A2dpCodecConfigLhdcV3Sink(
    btav_a2dp_codec_priority_t codec_priority)
    : A2dpCodecConfigLhdcV3Base(BTAV_A2DP_CODEC_INDEX_SINK_LHDCV3,
                             A2DP_VendorCodecIndexStrLhdcV3Sink(), codec_priority,
                             false) {}

A2dpCodecConfigLhdcV3Sink::~A2dpCodecConfigLhdcV3Sink() {}

bool A2dpCodecConfigLhdcV3Sink::init() {
  if (!isValid()) return false;

  // Load the decoder
  if (!A2DP_VendorLoadDecoderLhdcV3()) {
    LOG_ERROR("%s: cannot load the decoder", __func__);
    return false;
  }

  return true;
}

bool A2dpCodecConfigLhdcV3Sink::useRtpHeaderMarkerBit() const {
  // TODO: This method applies only to Source codecs
  return false;
}

#if 0
bool A2dpCodecConfigLhdcV3Sink::updateEncoderUserConfig(
    UNUSED_ATTR const tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
    UNUSED_ATTR bool* p_restart_input, UNUSED_ATTR bool* p_restart_output,
    UNUSED_ATTR bool* p_config_updated) {
  // TODO: This method applies only to Source codecs
  return false;
}
#endif

#if 0
uint64_t A2dpCodecConfigLhdcV3Sink::encoderIntervalMs() const {
  // TODO: This method applies only to Source codecs
  return 0;
}
#endif

#if 0
int A2dpCodecConfigLhdcV3Sink::getEffectiveMtu() const {
  // TODO: This method applies only to Source codecs
  return 0;
}
#endif

bool A2dpCodecConfigLhdcV3Base::setCodecConfig(const uint8_t* p_peer_codec_info, bool is_capability,
                      uint8_t* p_result_codec_config) {
  is_source_ = false;
  return true;
}

bool A2dpCodecConfigLhdcV3Base::setPeerCodecCapabilities(
      const uint8_t* p_peer_codec_capabilities) {
  is_source_ = false;
  return true;
}


