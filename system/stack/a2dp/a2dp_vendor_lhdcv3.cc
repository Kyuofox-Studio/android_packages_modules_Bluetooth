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

#define LOG_TAG "a2dp_vendor_lhdcv3"

#include "bt_target.h"

#include "a2dp_vendor_lhdcv3.h"

#include <string.h>

#include <base/logging.h>
#include "a2dp_vendor.h"
#include "a2dp_vendor_lhdcv3_encoder.h"
#include "bt_utils.h"
#include "btif_av_co.h"
#include "osi/include/log.h"
#include "osi/include/osi.h"


typedef struct {
  btav_a2dp_codec_config_t *_codec_config_;
  btav_a2dp_codec_config_t *_codec_capability_;
  btav_a2dp_codec_config_t *_codec_local_capability_;
  btav_a2dp_codec_config_t *_codec_selectable_capability_;
  btav_a2dp_codec_config_t *_codec_user_config_;
  btav_a2dp_codec_config_t *_codec_audio_config_;
}tA2DP_CODEC_CONFIGS_PACK;

typedef struct {
  uint8_t   featureCode;    /* code definition for LHDC API ex: LHDC_EXTEND_FUNC_A2DP_LHDC_JAS_CODE */
  uint8_t   inSpecBank;     /* in which specific bank */
  uint8_t   bitPos;         /* at which bit index number of the specific bank */
}tA2DP_LHDC_FEATURE_POS;

/* source side metadata of JAS feature */
static const tA2DP_LHDC_FEATURE_POS a2dp_lhdc_source_caps_JAS = {
    LHDC_EXTEND_FUNC_A2DP_LHDC_JAS_CODE,
    LHDC_EXTEND_FUNC_A2DP_SPECIFIC3_INDEX,
    A2DP_LHDC_JAS_SPEC_BIT_POS,
};
/* source side metadata of AR feature */
static const tA2DP_LHDC_FEATURE_POS a2dp_lhdc_source_caps_AR = {
    LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE,
    LHDC_EXTEND_FUNC_A2DP_SPECIFIC3_INDEX,
    A2DP_LHDC_AR_SPEC_BIT_POS
};
/* source side metadata of LLAC feature */
static const tA2DP_LHDC_FEATURE_POS a2dp_lhdc_source_caps_LLAC = {
    LHDC_EXTEND_FUNC_A2DP_LHDC_LLAC_CODE,
    LHDC_EXTEND_FUNC_A2DP_SPECIFIC3_INDEX,
    A2DP_LHDC_LLAC_SPEC_BIT_POS
};
/* source side metadata of META feature */
static const tA2DP_LHDC_FEATURE_POS a2dp_lhdc_source_caps_META = {
    LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE,
    LHDC_EXTEND_FUNC_A2DP_SPECIFIC3_INDEX,
    A2DP_LHDC_META_SPEC_BIT_POS
};
/* source side metadata of MBR feature */
static const tA2DP_LHDC_FEATURE_POS a2dp_lhdc_source_caps_MBR = {
    LHDC_EXTEND_FUNC_A2DP_LHDC_MBR_CODE,
    LHDC_EXTEND_FUNC_A2DP_SPECIFIC3_INDEX,
    A2DP_LHDC_MBR_SPEC_BIT_POS
};
/* source side metadata of LARC feature */
static const tA2DP_LHDC_FEATURE_POS a2dp_lhdc_source_caps_LARC = {
    LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE,
    LHDC_EXTEND_FUNC_A2DP_SPECIFIC3_INDEX,
    A2DP_LHDC_LARC_SPEC_BIT_POS
};
/* source side metadata of LHDCV4 feature */
static const tA2DP_LHDC_FEATURE_POS a2dp_lhdc_source_caps_LHDCV4 = {
    LHDC_EXTEND_FUNC_A2DP_LHDC_V4_CODE,
    LHDC_EXTEND_FUNC_A2DP_SPECIFIC3_INDEX,
    A2DP_LHDC_V4_SPEC_BIT_POS
};

static const tA2DP_LHDC_FEATURE_POS a2dp_lhdc_source_caps_all[] = {
    a2dp_lhdc_source_caps_JAS,
    a2dp_lhdc_source_caps_AR,
    a2dp_lhdc_source_caps_LLAC,
    a2dp_lhdc_source_caps_META,
    a2dp_lhdc_source_caps_MBR,
    a2dp_lhdc_source_caps_LARC,
    a2dp_lhdc_source_caps_LHDCV4,
};


// data type for the LHDC Codec Information Element */
// NOTE: bits_per_sample is needed only for LHDC encoder initialization.
typedef struct {
  uint32_t vendorId;
  uint16_t codecId;    /* Codec ID for LHDC */
  uint8_t sampleRate;  /* Sampling Frequency for LHDC*/
  uint8_t llac_sampleRate;  /* Sampling Frequency for LLAC */
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
} tA2DP_LHDC_CIE;

/* LHDC Source codec capabilities */
static const tA2DP_LHDC_CIE a2dp_lhdc_source_caps = {
    A2DP_LHDC_VENDOR_ID,  // vendorId
    A2DP_LHDCV3_CODEC_ID,   // codecId
    // sampleRate
    //(A2DP_LHDC_SAMPLING_FREQ_48000),
    (A2DP_LHDC_SAMPLING_FREQ_44100 | A2DP_LHDC_SAMPLING_FREQ_48000 | A2DP_LHDC_SAMPLING_FREQ_96000),
	(A2DP_LHDC_SAMPLING_FREQ_48000),
    // bits_per_sample
    (BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16 | BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24),
    //Channel Separation
    //A2DP_LHDC_CH_SPLIT_NONE | A2DP_LHDC_CH_SPLIT_TWS,
    A2DP_LHDC_BITRATE_ALL,
    //Version number
    A2DP_LHDC_VER3 | A2DP_LHDC_VER4 | A2DP_LHDC_VER6,
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
    //(BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16)};

/* for response to API */
static int A2DP_VendorGetSrcCapNumberLhdcv3()
{
  return (sizeof(a2dp_lhdc_source_caps_all) / sizeof(tA2DP_LHDC_FEATURE_POS) );
}

/* for response to API */
bool A2DP_VendorGetSrcCapVectorLhdcv3(uint8_t *capVector)
{
    int capNumber = 0, run = 0;

    if(!capVector)
    {
        LOG_ERROR( "%s: null buffer!", __func__);
        return false;
    }

    /* count and check the number of source available capabilities */
    capNumber = A2DP_VendorGetSrcCapNumberLhdcv3();

    if(capNumber <= 0)
    {
      LOG_DEBUG( "%s: no capabilities, nothing to do!", __func__);
      return true;
    }

    /* configure capabilities vector for LHDC API */
    /* Byte-1:      featureCode
     * Byte-2[7-6]: inSpecBank
     * Byte-2[5-0]: bitPos
     */
    for(int i=0; i<capNumber; i++)
    {
      capVector[run] = a2dp_lhdc_source_caps_all[i].featureCode;
      capVector[run+1] = a2dp_lhdc_source_caps_all[i].inSpecBank | a2dp_lhdc_source_caps_all[i].bitPos;
      //LOG_DEBUG( "%s: fill cap(%d):[0x%02X 0x%02X]", __func__, i, capVector[run], capVector[run+1]);
      run+=2;
    }

    return true;
}

/* Default LHDC codec configuration */
static const tA2DP_LHDC_CIE a2dp_lhdc_default_config = {
    A2DP_LHDC_VENDOR_ID,                // vendorId
    A2DP_LHDCV3_CODEC_ID,                 // codecId
    A2DP_LHDC_SAMPLING_FREQ_96000,      // LHDC default best sampleRate
	A2DP_LHDC_SAMPLING_FREQ_48000,      // LLAC default best sampleRate
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

static std::string lhdcV3_QualityModeBitRate_toString(uint32_t value) {
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

static const tA2DP_ENCODER_INTERFACE a2dp_encoder_interface_lhdcv3 = {
    a2dp_vendor_lhdcv3_encoder_init,
    a2dp_vendor_lhdcv3_encoder_cleanup,
    a2dp_vendor_lhdcv3_feeding_reset,
    a2dp_vendor_lhdcv3_feeding_flush,
    a2dp_vendor_lhdcv3_get_encoder_interval_ms,
    a2dp_vendor_lhdcv3_get_effective_frame_size,
    a2dp_vendor_lhdcv3_send_frames,
    a2dp_vendor_lhdcv3_set_transmit_queue_length};

UNUSED_ATTR static tA2DP_STATUS A2DP_CodecInfoMatchesCapabilityLhdcV3(
    const tA2DP_LHDC_CIE* p_cap, const uint8_t* p_codec_info,
    bool is_peer_codec_info);


// Builds the LHDC Media Codec Capabilities byte sequence beginning from the
// LOSC octet. |media_type| is the media type |AVDT_MEDIA_TYPE_*|.
// |p_ie| is a pointer to the LHDC Codec Information Element information.
// The result is stored in |p_result|. Returns A2DP_SUCCESS on success,
// otherwise the corresponding A2DP error status code.
static tA2DP_STATUS A2DP_BuildInfoLhdcV3(uint8_t media_type,
                                       const tA2DP_LHDC_CIE* p_ie,
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
static tA2DP_STATUS A2DP_ParseInfoLhdcV3(tA2DP_LHDC_CIE* p_ie,
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

  if (losc != A2DP_LHDCV3_CODEC_LEN) return A2DP_WRONG_CODEC;

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
  LOG_VERBOSE( "%s:Vendor(0x%08x), Codec(0x%04x)", __func__, p_ie->vendorId, p_ie->codecId);
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

  p_ie->hasFeatureJAS = ((*p_codec_info & A2DP_LHDC_FEATURE_JAS) != 0) ? true : false;

  p_ie->hasFeatureAR = ((*p_codec_info & A2DP_LHDC_FEATURE_AR) != 0) ? true : false;

  p_codec_info += 1;

  p_ie->version = (*p_codec_info) & A2DP_LHDC_VERSION_MASK;

  p_ie->maxTargetBitrate = (*p_codec_info) & A2DP_LHDC_MAX_BIT_RATE_MASK;

  p_ie->isLLSupported = ((*p_codec_info & A2DP_LHDC_LL_MASK) != 0)? true : false;

  p_ie->hasFeatureLLAC = ((*p_codec_info & A2DP_LHDC_FEATURE_LLAC) != 0) ? true : false;

  p_codec_info += 1;

  p_ie->channelSplitMode = (*p_codec_info) & A2DP_LHDC_CH_SPLIT_MSK;

  p_ie->hasFeatureMETA = ((*p_codec_info & A2DP_LHDC_FEATURE_META) != 0) ? true : false;

  p_ie->hasFeatureMinBitrate = ((*p_codec_info & A2DP_LHDC_FEATURE_MIN_BR) != 0) ? true : false;

  p_ie->hasFeatureLARC = ((*p_codec_info & A2DP_LHDC_FEATURE_LARC) != 0) ? true : false;

  p_ie->hasFeatureLHDCV4 = ((*p_codec_info & A2DP_LHDC_FEATURE_LHDCV4) != 0) ? true : false;
  
  LOG_DEBUG( "%s:Has LL(%d) JAS(%d) AR(%d) META(%d) LLAC(%d) MBR(%d) LARC(%d) V4(%d)", __func__,
      p_ie->isLLSupported,
      p_ie->hasFeatureJAS,
      p_ie->hasFeatureAR,
      p_ie->hasFeatureMETA,
      p_ie->hasFeatureLLAC,
      p_ie->hasFeatureMinBitrate,
      p_ie->hasFeatureLARC,
      p_ie->hasFeatureLHDCV4);

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

bool A2DP_IsVendorSourceCodecValidLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE cfg_cie;

  /* Use a liberal check when parsing the codec info */
  return (A2DP_ParseInfoLhdcV3(&cfg_cie, p_codec_info, false) == A2DP_SUCCESS) ||
         (A2DP_ParseInfoLhdcV3(&cfg_cie, p_codec_info, true) == A2DP_SUCCESS);
}

bool A2DP_IsVendorPeerSinkCodecValidLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE cfg_cie;

  /* Use a liberal check when parsing the codec info */
  return (A2DP_ParseInfoLhdcV3(&cfg_cie, p_codec_info, false) == A2DP_SUCCESS) ||
         (A2DP_ParseInfoLhdcV3(&cfg_cie, p_codec_info, true) == A2DP_SUCCESS);
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
static tA2DP_STATUS A2DP_CodecInfoMatchesCapabilityLhdcV3(
    const tA2DP_LHDC_CIE* p_cap, const uint8_t* p_codec_info,
    bool is_capability) {
  tA2DP_STATUS status;
  tA2DP_LHDC_CIE cfg_cie;

  /* parse configuration */
  status = A2DP_ParseInfoLhdcV3(&cfg_cie, p_codec_info, is_capability);
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

bool A2DP_VendorUsesRtpHeaderLhdcV3(UNUSED_ATTR bool content_protection_enabled,
                                  UNUSED_ATTR const uint8_t* p_codec_info) {
  // TODO: Is this correct? The RTP header is always included?
  return true;
}

const char* A2DP_VendorCodecNameLhdcV3(UNUSED_ATTR const uint8_t* p_codec_info) {
  return "LHDC V3";
}

bool A2DP_VendorCodecTypeEqualsLhdcV3(const uint8_t* p_codec_info_a,
                                    const uint8_t* p_codec_info_b) {
  tA2DP_LHDC_CIE lhdc_cie_a;
  tA2DP_LHDC_CIE lhdc_cie_b;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status =
      A2DP_ParseInfoLhdcV3(&lhdc_cie_a, p_codec_info_a, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }
  a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie_b, p_codec_info_b, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }

  return true;
}

bool A2DP_VendorCodecEqualsLhdcV3(const uint8_t* p_codec_info_a,
                                const uint8_t* p_codec_info_b) {
  tA2DP_LHDC_CIE lhdc_cie_a;
  tA2DP_LHDC_CIE lhdc_cie_b;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status =
      A2DP_ParseInfoLhdcV3(&lhdc_cie_a, p_codec_info_a, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }
  a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie_b, p_codec_info_b, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return false;
  }

  return (lhdc_cie_a.sampleRate == lhdc_cie_b.sampleRate) &&
         (lhdc_cie_a.bits_per_sample == lhdc_cie_b.bits_per_sample) &&
         /*(lhdc_cie_a.supportedBitrate == lhdc_cie_b.supportedBitrate) &&*/
         (lhdc_cie_a.hasFeatureLLAC == lhdc_cie_b.hasFeatureLLAC) &&
         (lhdc_cie_a.hasFeatureLHDCV4 == lhdc_cie_b.hasFeatureLHDCV4) &&
         (lhdc_cie_a.isLLSupported == lhdc_cie_b.isLLSupported);
}

// Savitech Patch - START  Offload
int A2DP_VendorGetBitRateLhdcV3(const uint8_t* p_codec_info) {

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

int A2DP_VendorGetTrackSampleRateLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false);
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

int A2DP_VendorGetTrackBitsPerSampleLhdcV3(const uint8_t* p_codec_info) {
    tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false);
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

int A2DP_VendorGetTrackChannelCountLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }
  return 2;
}

int A2DP_VendorGetChannelModeCodeLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }
  return A2DP_LHDC_CHANNEL_MODE_STEREO;
}

bool A2DP_VendorGetPacketTimestampLhdcV3(UNUSED_ATTR const uint8_t* p_codec_info,
                                       const uint8_t* p_data,
                                       uint32_t* p_timestamp) {
  // TODO: Is this function really codec-specific?
  *p_timestamp = *(const uint32_t*)p_data;
  return true;
}

int16_t A2DP_VendorGetMaxDatarateLhdcV3(const uint8_t* p_codec_info){
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, true);
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

bool A2DP_VendorGetLowLatencyStateLhdcV3(const uint8_t* p_codec_info){
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }

  LOG_DEBUG( "%s: isLLSupported =%d", __func__, lhdc_cie.isLLSupported);

  return lhdc_cie.isLLSupported ? true : false;
}




//Always return newest version.
uint8_t A2DP_VendorGetVersionLhdcV3(const uint8_t* p_codec_info){
  tA2DP_LHDC_CIE lhdc_cie;
  uint8_t result;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }


  for (result = 0x08; result != 0; ) {
    if ((lhdc_cie.version & result) != 0) {
      break;
    }
    result >>= 1;
  }
  //LOG_DEBUG( "%s: version = 0x%02x, result = 0x%02x", __func__, lhdc_cie.version, result);


  switch (result) {
    case A2DP_LHDC_VER3:
    return 1;
    case A2DP_LHDC_VER4:
    return 2;
    case A2DP_LHDC_VER5:
    return 3;
    case A2DP_LHDC_VER6:
    return 4;
    default:
    return -1;
  }
}


int8_t A2DP_VendorGetChannelSplitModeLhdcV3(const uint8_t* p_codec_info){
  tA2DP_LHDC_CIE lhdc_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: cannot decode codec information: %d", __func__,
              a2dp_status);
    return -1;
  }

  LOG_DEBUG( "%s: channelSplitMode =%d", __func__, lhdc_cie.channelSplitMode);

  return lhdc_cie.channelSplitMode;

}

bool A2DP_VendorHasV4FlagLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;
  if (A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false) != A2DP_SUCCESS)
    return false;

  return lhdc_cie.hasFeatureLHDCV4;
}

bool A2DP_VendorHasJASFlagLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;
  if (A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false) != A2DP_SUCCESS)
    return false;

  return lhdc_cie.hasFeatureJAS;
}

bool A2DP_VendorHasARFlagLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;
  if (A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false) != A2DP_SUCCESS)
    return false;

  return lhdc_cie.hasFeatureAR;
}

bool A2DP_VendorHasLLACFlagLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;
  if (A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false) != A2DP_SUCCESS)
    return false;

  return lhdc_cie.hasFeatureLLAC;
}

bool A2DP_VendorHasMETAFlagLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;
  if (A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false) != A2DP_SUCCESS)
    return false;

  return lhdc_cie.hasFeatureMETA;
}

bool A2DP_VendorHasMinBRFlagLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;
  if (A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false) != A2DP_SUCCESS)
    return false;

  return lhdc_cie.hasFeatureMinBitrate;
}

bool A2DP_VendorHasLARCFlagLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE lhdc_cie;
  if (A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, false) != A2DP_SUCCESS)
    return false;

  return lhdc_cie.hasFeatureLARC;
}

bool A2DP_VendorBuildCodecHeaderLhdcV3(UNUSED_ATTR const uint8_t* p_codec_info,
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

void A2DP_VendorDumpCodecInfoLhdcV3(const uint8_t* p_codec_info) {
  tA2DP_STATUS a2dp_status;
  tA2DP_LHDC_CIE lhdc_cie;

  LOG_DEBUG( "%s", __func__);

  a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, true);
  if (a2dp_status != A2DP_SUCCESS) {
    LOG_ERROR( "%s: A2DP_ParseInfoLhdcV3 fail:%d", __func__, a2dp_status);
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

std::string A2DP_VendorCodecInfoStringLhdcV3(const uint8_t* p_codec_info) {
  std::stringstream res;
  std::string field;
  tA2DP_STATUS a2dp_status;
  tA2DP_LHDC_CIE lhdc_cie;

  a2dp_status = A2DP_ParseInfoLhdcV3(&lhdc_cie, p_codec_info, true);
  if (a2dp_status != A2DP_SUCCESS) {
    res << "A2DP_ParseInfoLhdcV3 fail: " << loghex(a2dp_status);
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

const tA2DP_ENCODER_INTERFACE* A2DP_VendorGetEncoderInterfaceLhdcV3(
    const uint8_t* p_codec_info) {
  if (!A2DP_IsVendorSourceCodecValidLhdcV3(p_codec_info)) return NULL;

  return &a2dp_encoder_interface_lhdcv3;
}

bool A2DP_VendorAdjustCodecLhdcV3(uint8_t* p_codec_info) {
  tA2DP_LHDC_CIE cfg_cie;

  // Nothing to do: just verify the codec info is valid
  if (A2DP_ParseInfoLhdcV3(&cfg_cie, p_codec_info, true) != A2DP_SUCCESS)
    return false;

  return true;
}

btav_a2dp_codec_index_t A2DP_VendorSourceCodecIndexLhdcV3(
    UNUSED_ATTR const uint8_t* p_codec_info) {
  return BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV3;
}

const char* A2DP_VendorCodecIndexStrLhdcV3(void) { return "LHDC V3"; }

bool A2DP_VendorInitCodecConfigLhdcV3(AvdtpSepConfig* p_cfg) {
  if (A2DP_BuildInfoLhdcV3(AVDT_MEDIA_TYPE_AUDIO, &a2dp_lhdc_source_caps,
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

A2dpCodecConfigLhdcV3::A2dpCodecConfigLhdcV3(
    btav_a2dp_codec_priority_t codec_priority)
    : A2dpCodecConfig(BTAV_A2DP_CODEC_INDEX_SOURCE_LHDCV3, "LHDC V3",
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

A2dpCodecConfigLhdcV3::~A2dpCodecConfigLhdcV3() {}

bool A2dpCodecConfigLhdcV3::init() {
  if (!isValid()) return false;

  // Load the encoder
  if (!A2DP_VendorLoadEncoderLhdcV3()) {
    LOG_ERROR( "%s: cannot load the encoder", __func__);
    return false;
  }

  return true;
}

bool A2dpCodecConfigLhdcV3::useRtpHeaderMarkerBit() const { return false; }

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

bool A2dpCodecConfigLhdcV3::copySinkCapability(uint8_t * codec_info){
    std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
    memcpy(codec_info, ota_codec_peer_capability_, AVDT_CODEC_SIZE);
    return true;
}

static bool A2DP_IsFeatureInUserConfigLhdcV3(tA2DP_CODEC_CONFIGS_PACK* cfgsPtr, uint8_t featureCode)
{
  switch(featureCode)
  {
    case LHDC_EXTEND_FUNC_A2DP_LHDC_JAS_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_user_config_, a2dp_lhdc_source_caps_JAS.inSpecBank, A2DP_LHDC_JAS_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_user_config_, a2dp_lhdc_source_caps_AR.inSpecBank, A2DP_LHDC_AR_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_user_config_, a2dp_lhdc_source_caps_META.inSpecBank, A2DP_LHDC_META_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_LLAC_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_user_config_, a2dp_lhdc_source_caps_LLAC.inSpecBank, A2DP_LHDC_LLAC_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_MBR_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_user_config_, a2dp_lhdc_source_caps_MBR.inSpecBank, A2DP_LHDC_MBR_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_user_config_, a2dp_lhdc_source_caps_LARC.inSpecBank, A2DP_LHDC_LARC_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_V4_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_user_config_, a2dp_lhdc_source_caps_LHDCV4.inSpecBank, A2DP_LHDC_V4_ENABLED);
      }
      break;

  default:
    break;
  }

  return false;
}
static bool A2DP_IsFeatureInCodecConfigLhdcV3(tA2DP_CODEC_CONFIGS_PACK* cfgsPtr, uint8_t featureCode)
{
  switch(featureCode)
  {
    case LHDC_EXTEND_FUNC_A2DP_LHDC_JAS_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_config_, a2dp_lhdc_source_caps_JAS.inSpecBank, A2DP_LHDC_JAS_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_config_, a2dp_lhdc_source_caps_AR.inSpecBank, A2DP_LHDC_AR_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_config_, a2dp_lhdc_source_caps_META.inSpecBank, A2DP_LHDC_META_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_LLAC_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_config_, a2dp_lhdc_source_caps_LLAC.inSpecBank, A2DP_LHDC_LLAC_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_MBR_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_config_, a2dp_lhdc_source_caps_MBR.inSpecBank, A2DP_LHDC_MBR_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_config_, a2dp_lhdc_source_caps_LARC.inSpecBank, A2DP_LHDC_LARC_ENABLED);
      }
      break;
    case LHDC_EXTEND_FUNC_A2DP_LHDC_V4_CODE:
      {
        CHECK_IN_A2DP_SPEC(cfgsPtr->_codec_config_, a2dp_lhdc_source_caps_LHDCV4.inSpecBank, A2DP_LHDC_V4_ENABLED);
      }
      break;

  default:
    break;
  }

  return false;
}

static void A2DP_UpdateFeatureToSpecLhdcV3(tA2DP_CODEC_CONFIGS_PACK* cfgsPtr,
    uint16_t toCodecCfg, bool hasFeature, uint8_t toSpec, int64_t value)
{
  if(toCodecCfg & A2DP_LHDC_TO_A2DP_CODEC_CONFIG_)
  {
    SETUP_A2DP_SPEC(cfgsPtr->_codec_config_, toSpec, hasFeature, value);
  }
  if(toCodecCfg & A2DP_LHDC_TO_A2DP_CODEC_CAP_)
  {
    SETUP_A2DP_SPEC(cfgsPtr->_codec_capability_, toSpec, hasFeature, value);
  }
  if(toCodecCfg & A2DP_LHDC_TO_A2DP_CODEC_LOCAL_CAP_)
  {
    SETUP_A2DP_SPEC(cfgsPtr->_codec_local_capability_, toSpec, hasFeature, value);
  }
  if(toCodecCfg & A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_)
  {
    SETUP_A2DP_SPEC(cfgsPtr->_codec_selectable_capability_, toSpec, hasFeature, value);
  }
  if(toCodecCfg & A2DP_LHDC_TO_A2DP_CODEC_USER_)
  {
    SETUP_A2DP_SPEC(cfgsPtr->_codec_user_config_, toSpec, hasFeature, value);
  }
  if(toCodecCfg & A2DP_LHDC_TO_A2DP_CODEC_AUDIO_)
  {
    SETUP_A2DP_SPEC(cfgsPtr->_codec_audio_config_, toSpec, hasFeature, value);
  }
}

static void A2DP_UpdateFeatureToA2dpConfigLhdcV3(tA2DP_CODEC_CONFIGS_PACK *cfgsPtr,
    uint8_t featureCode,  uint16_t toCodecCfg, bool hasFeature)
{

  //LOG_DEBUG( "%s: featureCode:0x%02X toCfgs:0x%04X, toSet:%d", __func__, featureCode, toCodecCfg, hasFeature);

  switch(featureCode)
  {
  case LHDC_EXTEND_FUNC_A2DP_LHDC_JAS_CODE:
    A2DP_UpdateFeatureToSpecLhdcV3(cfgsPtr, toCodecCfg, hasFeature,
        a2dp_lhdc_source_caps_JAS.inSpecBank, A2DP_LHDC_JAS_ENABLED);
    break;
  case LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE:
    A2DP_UpdateFeatureToSpecLhdcV3(cfgsPtr, toCodecCfg, hasFeature,
        a2dp_lhdc_source_caps_AR.inSpecBank, A2DP_LHDC_AR_ENABLED);
    break;
  case LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE:
    A2DP_UpdateFeatureToSpecLhdcV3(cfgsPtr, toCodecCfg, hasFeature,
        a2dp_lhdc_source_caps_META.inSpecBank, A2DP_LHDC_META_ENABLED);
    break;
  case LHDC_EXTEND_FUNC_A2DP_LHDC_LLAC_CODE:
    A2DP_UpdateFeatureToSpecLhdcV3(cfgsPtr, toCodecCfg, hasFeature,
        a2dp_lhdc_source_caps_LLAC.inSpecBank, A2DP_LHDC_LLAC_ENABLED);
    break;
  case LHDC_EXTEND_FUNC_A2DP_LHDC_MBR_CODE:
    A2DP_UpdateFeatureToSpecLhdcV3(cfgsPtr, toCodecCfg, hasFeature,
        a2dp_lhdc_source_caps_MBR.inSpecBank, A2DP_LHDC_MBR_ENABLED);
    break;
  case LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE:
    A2DP_UpdateFeatureToSpecLhdcV3(cfgsPtr, toCodecCfg, hasFeature,
        a2dp_lhdc_source_caps_LARC.inSpecBank, A2DP_LHDC_LARC_ENABLED);
    break;
  case LHDC_EXTEND_FUNC_A2DP_LHDC_V4_CODE:
    A2DP_UpdateFeatureToSpecLhdcV3(cfgsPtr, toCodecCfg, hasFeature,
        a2dp_lhdc_source_caps_LHDCV4.inSpecBank, A2DP_LHDC_V4_ENABLED);
    break;

  default:
    break;
  }
}


static uint32_t A2DP_MaxBitRatetoQualityLevelLhdcV3(uint8_t maxTargetBitrate)
{
	switch (maxTargetBitrate & A2DP_LHDC_MAX_BIT_RATE_MASK)
	{
	case A2DP_LHDC_MAX_BIT_RATE_900K:
		return A2DP_LHDC_QUALITY_HIGH;
	case A2DP_LHDC_MAX_BIT_RATE_500K:
		return A2DP_LHDC_QUALITY_MID;
	case A2DP_LHDC_MAX_BIT_RATE_400K:
		return A2DP_LHDC_QUALITY_LOW;
	default:
		return (0xFF);
	}
}


bool A2dpCodecConfigLhdcV3::setCodecConfig(const uint8_t* p_peer_codec_info,
                                         bool is_capability,
                                         uint8_t* p_result_codec_config) {
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  tA2DP_LHDC_CIE sink_info_cie;
  tA2DP_LHDC_CIE result_config_cie;
  uint8_t sampleRate;
  bool isLLEnabled;
  bool hasFeature;
  uint32_t quality_mode, maxBitRate_Qmode;
  //uint8_t supportedBitrate;
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

  tA2DP_CODEC_CONFIGS_PACK allCfgPack;
  allCfgPack._codec_config_ = &codec_config_;
  allCfgPack._codec_capability_ = &codec_capability_;
  allCfgPack._codec_local_capability_ = &codec_local_capability_;
  allCfgPack._codec_selectable_capability_ = &codec_selectable_capability_;
  allCfgPack._codec_user_config_ = &codec_user_config_;
  allCfgPack._codec_audio_config_ = &codec_audio_config_;

  tA2DP_STATUS status =
      A2DP_ParseInfoLhdcV3(&sink_info_cie, p_peer_codec_info, is_capability);
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


  LOG_DEBUG( "%s: incoming version: peer(0x%02x), host(0x%02x)",
            __func__, sink_info_cie.version, a2dp_lhdc_source_caps.version);

  // 2021/08/19: when sink's version is "V3_NotComapatible(version == A2DP_LHDC_VER6(0x8))",
  //				wrap it to A2DP_LHDC_VER3 to accept and treat as an A2DP_LHDC_VER3 device.
  if(sink_info_cie.version == A2DP_LHDC_VER6) {
	  sink_info_cie.version = A2DP_LHDC_VER3;
	  LOG_DEBUG( "%s: wrap V3_NotComapatible sink version to A2DP_LHDC_VER3", __func__);
  }

  if ((sink_info_cie.version & a2dp_lhdc_source_caps.version) == 0) {
    LOG_ERROR( "%s: Sink versoin unsupported! peer(0x%02x), host(0x%02x)",
              __func__, sink_info_cie.version, a2dp_lhdc_source_caps.version);
    goto fail;
  }
  result_config_cie.version = sink_info_cie.version;


/*
  if (sink_info_cie.channelSplitMode & A2DP_LHDC_CH_SPLIT_TWS) {
      result_config_cie.channelSplitMode = A2DP_LHDC_CH_SPLIT_TWS;
  }else{
      result_config_cie.channelSplitMode = A2DP_LHDC_CH_SPLIT_NONE;
  }
  */

  LOG_DEBUG( "%s: Enter User_SP1=(0x%016llX); SP2=(0x%016llX); SP3=(0x%016llX); SP4=(0x%016llX)", __func__,
		  (unsigned long long)(codec_user_config_.codec_specific_1),
		  (unsigned long long)(codec_user_config_.codec_specific_2),
		  (unsigned long long)(codec_user_config_.codec_specific_3),
		  (unsigned long long)(codec_user_config_.codec_specific_4));
  LOG_DEBUG( "%s: Enter Codec_SP1=(0x%016llX); SP2=(0x%016llX); SP3=(0x%016llX); SP4=(0x%016llX)", __func__,
		  (unsigned long long)(codec_config_.codec_specific_1),
		  (unsigned long long)(codec_config_.codec_specific_2),
		  (unsigned long long)(codec_config_.codec_specific_3),
		  (unsigned long long)(codec_config_.codec_specific_4));

  /*******************************************
   * Update Capabilities: LHDC Low Latency
   * to A2DP specifics 2
   *******************************************/
  isLLEnabled = (a2dp_lhdc_source_caps.isLLSupported & sink_info_cie.isLLSupported);
  result_config_cie.isLLSupported = false;
  switch (codec_user_config_.codec_specific_2 & A2DP_LHDC_LL_ENABLED) {
    case A2DP_LHDC_LL_ENABLE:
    if (isLLEnabled) {
      result_config_cie.isLLSupported = true;
      codec_config_.codec_specific_2 |= A2DP_LHDC_LL_ENABLED;
    }
    break;
    case A2DP_LHDC_LL_DISABLE:
    if (!isLLEnabled) {
      result_config_cie.isLLSupported = false;
      codec_config_.codec_specific_2 &= ~A2DP_LHDC_LL_ENABLED;
    }
    break;
  }
  if (isLLEnabled) {
    codec_selectable_capability_.codec_specific_2 |= A2DP_LHDC_LL_ENABLED;
    codec_capability_.codec_specific_2 |= A2DP_LHDC_LL_ENABLED;
  }
  //result_config_cie.isLLSupported = sink_info_cie.isLLSupported;
  LOG_DEBUG( "%s: isLLSupported, Sink(0x%02x) Set(0x%08x), result(0x%02x)", __func__,
                                sink_info_cie.isLLSupported,
                                (uint32_t)codec_user_config_.codec_specific_2,
                                result_config_cie.isLLSupported);

  //
  // Select the sample frequency
  //
  sampleRate = a2dp_lhdc_source_caps.sampleRate & sink_info_cie.sampleRate;
  LOG_DEBUG( "%s: sampleRate src:0x%x sink:0x%x matched:0x%x", __func__,
		  a2dp_lhdc_source_caps.sampleRate, sink_info_cie.sampleRate, sampleRate);

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

    //Above Parts: if codec_config is setup successfully(ie., sampleRate in codec_user_config_ is valid), ignore following parts.
    if (codec_config_.sample_rate != BTAV_A2DP_CODEC_SAMPLE_RATE_NONE) {
    	LOG_DEBUG( "%s: setup sample_rate:0x%x from user_config", __func__, codec_config_.sample_rate);
    	break;
    }
    //Below Parts: if codec_config is still not setup successfully, test default sample rate or use the best match

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
      LOG_DEBUG( "%s: select audio sample rate:(0x%x)", __func__, result_config_cie.sampleRate);
      break;
    }

    // No user preference - try the default config
    if (sink_info_cie.hasFeatureLLAC) {
      if (select_best_sample_rate(
              a2dp_lhdc_default_config.llac_sampleRate & sink_info_cie.sampleRate,
              &result_config_cie, &codec_config_)) {
    	LOG_DEBUG( "%s: select best sample rate(LLAC default):0x%x", __func__, result_config_cie.sampleRate);
        break;
      }
    } else {
      if (select_best_sample_rate(
              a2dp_lhdc_default_config.sampleRate & sink_info_cie.sampleRate,
              &result_config_cie, &codec_config_)) {
        LOG_DEBUG( "%s: select best sample rate(LHDC default):0x%x", __func__, result_config_cie.sampleRate);
        break;
      }
    }

    // No user preference - use the best match
    if (select_best_sample_rate(sampleRate, &result_config_cie,
                                &codec_config_)) {
      LOG_DEBUG( "%s: select best sample rate(best):0x%x", __func__, result_config_cie.sampleRate);
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
  LOG_DEBUG( "%s: bits_per_sample src:0x%02x sink:0x%02x matched:0x%02x", __func__,
		  a2dp_lhdc_source_caps.bits_per_sample, sink_info_cie.bits_per_sample, bits_per_sample);
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

    if (codec_config_.bits_per_sample != BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE){
    	LOG_DEBUG( "%s: setup bit_per_sample:0x%02x user_config", __func__, codec_config_.bits_per_sample);
    	break;
    }

    // Compute the common capability
    if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16)
      codec_capability_.bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
    if (bits_per_sample & BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24)
      codec_capability_.bits_per_sample |= BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;

    // No user preference - the the codec audio config
    if (select_audio_bits_per_sample(&codec_audio_config_, bits_per_sample,
                                     &result_config_cie, &codec_config_)) {
      LOG_DEBUG( "%s: select audio bits_per_sample:0x%x", __func__, result_config_cie.bits_per_sample);
      break;
    }

    // No user preference - try the default config
    if (select_best_bits_per_sample(a2dp_lhdc_default_config.bits_per_sample & sink_info_cie.bits_per_sample,
                                    &result_config_cie, &codec_config_)) {
      LOG_DEBUG( "%s: select best bits_per_sample(default):0x%x", __func__, result_config_cie.bits_per_sample);
      break;
    }

    // No user preference - use the best match
    if (select_best_bits_per_sample(bits_per_sample,
                                    &result_config_cie, &codec_config_)) {
      LOG_DEBUG( "%s: select best bits_per_sample(best):0x%x", __func__, result_config_cie.bits_per_sample);
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
  LOG_DEBUG( "%s: channelMode = Only supported stereo", __func__);
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
              "%s: codec_config_.channel_mode != BTAV_A2DP_CODEC_CHANNEL_MODE_NONE or BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO "
              , __func__);
    goto fail;
  }

  /*******************************************
   * Update maxTargetBitrate
   *
   *******************************************/
  result_config_cie.maxTargetBitrate = sink_info_cie.maxTargetBitrate;

  LOG_DEBUG( "%s: Config Max bitrate result(0x%02x)", __func__, result_config_cie.maxTargetBitrate);

  /*******************************************
   * Update channelSplitMode
   *
   *******************************************/
  result_config_cie.channelSplitMode = sink_info_cie.channelSplitMode;
  LOG_DEBUG("%s: channelSplitMode = %d", __func__, result_config_cie.channelSplitMode);


  /*******************************************
   * quality mode: magic num check and reconfigure
   * to specific 1
   *******************************************/
  if ((codec_user_config_.codec_specific_1 & A2DP_LHDC_VENDOR_CMD_MASK) != A2DP_LHDC_QUALITY_MAGIC_NUM) {
      codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_ABR;
      LOG_DEBUG("%s: use default quality_mode:ABR", __func__);
  }
  quality_mode = codec_user_config_.codec_specific_1 & 0xff;

  // (1000K) does not supported in V3, reset to 900K
  if (quality_mode == A2DP_LHDC_QUALITY_HIGH1) {
    LOG_DEBUG( "%s: set non-supported quality mode %s to %s", __func__,
        lhdcV3_QualityModeBitRate_toString(quality_mode).c_str(),
        lhdcV3_QualityModeBitRate_toString(A2DP_LHDC_QUALITY_HIGH).c_str());
    codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_HIGH;
    quality_mode = A2DP_LHDC_QUALITY_HIGH;
  }

  /*******************************************
   * LHDC features: safety tag check
   * to specific 3
   *******************************************/
  if ((codec_user_config_.codec_specific_3 & A2DP_LHDC_VENDOR_FEATURE_MASK) != A2DP_LHDC_FEATURE_MAGIC_NUM)
  {
      LOG_DEBUG( "%s: LHDC feature tag not matched! use old feature settings", __func__);

      /* *
       * Magic num does not match:
       * 1. add tag
       * 2. Re-adjust previous feature(which refers to codec_user_config)'s state(in codec_config_) to codec_user_config_:
       * 	AR(has UI), Meta(no UI yet), LARC(no UI yet)
       * */
      // clean entire specific and set safety tag
      codec_user_config_.codec_specific_3 = A2DP_LHDC_FEATURE_MAGIC_NUM;

      // Feature: AR
      if( A2DP_IsFeatureInCodecConfigLhdcV3(&allCfgPack, LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE) )
      {
    	    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
    	        &allCfgPack,
				LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE,
    	        A2DP_LHDC_TO_A2DP_CODEC_USER_,
    	        true);
    	    LOG_DEBUG( "%s: restore user_cfg to previous AR status => ON", __func__);
      }
      else
      {
			A2DP_UpdateFeatureToA2dpConfigLhdcV3(
				&allCfgPack,
				LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE,
				A2DP_LHDC_TO_A2DP_CODEC_USER_,
				false);
			LOG_DEBUG( "%s: restore user_cfg to previous AR status => OFF", __func__);
      }
      // Feature: META
      if( A2DP_IsFeatureInCodecConfigLhdcV3(&allCfgPack, LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE) )
      {
    	    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
    	        &allCfgPack,
				LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE,
    	        A2DP_LHDC_TO_A2DP_CODEC_USER_,
    	        true);
    	    LOG_DEBUG( "%s: restore user_cfg to previous META status => ON", __func__);
      }
      else
      {
			A2DP_UpdateFeatureToA2dpConfigLhdcV3(
				&allCfgPack,
				LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE,
				A2DP_LHDC_TO_A2DP_CODEC_USER_,
				false);
			LOG_DEBUG( "%s: restore user_cfg to previous META status => OFF", __func__);
      }
      // Feature: LARC
      if( A2DP_IsFeatureInCodecConfigLhdcV3(&allCfgPack, LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE) )
      {
    	    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
    	        &allCfgPack,
				LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE,
    	        A2DP_LHDC_TO_A2DP_CODEC_USER_,
    	        true);
    	    LOG_DEBUG( "%s: restore user_cfg to previous LARC status => ON", __func__);
      }
      else
      {
			A2DP_UpdateFeatureToA2dpConfigLhdcV3(
				&allCfgPack,
				LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE,
				A2DP_LHDC_TO_A2DP_CODEC_USER_,
				false);
			LOG_DEBUG( "%s: restore user_cfg to previous LARC status => OFF", __func__);
      }
  }
  else
  {
	  LOG_DEBUG( "%s: LHDC feature tag matched!", __func__);
  }


  /**
  *LHDC V4 modify
  */

  /*******************************************
   * Update Feature/Capabilities: LLAC
   * to A2DP specifics
   *******************************************/
  //result_config_cie.hasFeatureLLAC = sink_info_cie.hasFeatureLLAC;
  hasFeature = (a2dp_lhdc_source_caps.hasFeatureLLAC & sink_info_cie.hasFeatureLLAC);
  result_config_cie.hasFeatureLLAC = false;

  /* *
   * reset bit in A2DP configs
   *     codec_config_.codec_specific_3 &= ~A2DP_LHDC_LLAC_ENABLED;
   *     codec_selectable_capability_.codec_specific_3 &= ~A2DP_LHDC_LLAC_ENABLED;
   *     codec_capability_.codec_specific_3 &= ~ A2DP_LHDC_LLAC_ENABLED;
   * */
  A2DP_UpdateFeatureToA2dpConfigLhdcV3(
      &allCfgPack,
      LHDC_EXTEND_FUNC_A2DP_LHDC_LLAC_CODE,
      (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_|A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_|A2DP_LHDC_TO_A2DP_CODEC_CAP_),
      false);

  if (hasFeature /*&& 
      (
       (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_48000 && (quality_mode <= A2DP_LHDC_QUALITY_LOW || quality_mode == A2DP_LHDC_QUALITY_ABR)) ||
       (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_44100 && (quality_mode <= A2DP_LHDC_QUALITY_LOW || quality_mode == A2DP_LHDC_QUALITY_ABR))
      )*/
     ) {
    result_config_cie.hasFeatureLLAC = true;
    //codec_config_.codec_specific_3 |= A2DP_LHDC_LLAC_ENABLED;
    //codec_user_config_.codec_specific_3 |= A2DP_LHDC_LLAC_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_LLAC_CODE,
        (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_| A2DP_LHDC_TO_A2DP_CODEC_USER_),
        true);
  }

  if(hasFeature)
  {
    //codec_selectable_capability_.codec_specific_3 |= A2DP_LHDC_LLAC_ENABLED;
    //codec_capability_.codec_specific_3 |= A2DP_LHDC_LLAC_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_LLAC_CODE,
        (A2DP_LHDC_TO_A2DP_CODEC_CAP_ | A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_),
        true);
  }

  LOG_DEBUG( "%s: Has LLAC feature?(0x%02x) Src(0x%02x) Sink(0x%02x) result(0x%02x)", __func__,
                                hasFeature,
                                a2dp_lhdc_source_caps.hasFeatureLLAC,
                                sink_info_cie.hasFeatureLLAC,
                                result_config_cie.hasFeatureLLAC);
  LOG_DEBUG( "%s: LLAC update:[config:(0x%016llX) cap:(0x%016llX) selcap:(0x%016llX) user:(0x%016llX)] ", __func__,
                                (codec_config_.codec_specific_3 & A2DP_LHDC_LLAC_ENABLED),
                                (codec_capability_.codec_specific_3 & A2DP_LHDC_LLAC_ENABLED),
                                (codec_selectable_capability_.codec_specific_3 & A2DP_LHDC_LLAC_ENABLED),
                                (codec_user_config_.codec_specific_3 & A2DP_LHDC_LLAC_ENABLED));

  /*******************************************
   * Update Feature/Capabilities: LHDCV4
   * to A2DP specifics
   *******************************************/
  //result_config_cie.hasFeatureLHDCV4 = sink_info_cie.hasFeatureLHDCV4;
  hasFeature = (a2dp_lhdc_source_caps.hasFeatureLHDCV4 & sink_info_cie.hasFeatureLHDCV4);
  result_config_cie.hasFeatureLHDCV4 = false;

  /* *
   * reset bit in A2DP configs
   *     codec_config_.codec_specific_3 &= ~A2DP_LHDC_V4_ENABLED;
   *     codec_selectable_capability_.codec_specific_3 &= ~A2DP_LHDC_V4_ENABLED;
   *     codec_capability_.codec_specific_3 &= ~ A2DP_LHDC_V4_ENABLED;
   * */
  A2DP_UpdateFeatureToA2dpConfigLhdcV3(
      &allCfgPack,
      LHDC_EXTEND_FUNC_A2DP_LHDC_V4_CODE,
      (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_|A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_|A2DP_LHDC_TO_A2DP_CODEC_CAP_),
      false);

  if (hasFeature /*&& 
      (
       (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_96000) ||
       (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_48000 && (quality_mode > A2DP_LHDC_QUALITY_LOW && quality_mode != A2DP_LHDC_QUALITY_ABR)) ||
       (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_44100 && (quality_mode > A2DP_LHDC_QUALITY_LOW && quality_mode != A2DP_LHDC_QUALITY_ABR))
      )*/
     ) {
    result_config_cie.hasFeatureLHDCV4 = true;
    //codec_config_.codec_specific_3 |= A2DP_LHDC_V4_ENABLED;
    //codec_user_config_.codec_specific_3 |= A2DP_LHDC_V4_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_V4_CODE,
        (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_| A2DP_LHDC_TO_A2DP_CODEC_USER_),
        true);
  }
  if (hasFeature) {
    //codec_selectable_capability_.codec_specific_3 |= A2DP_LHDC_V4_ENABLED;
    //codec_capability_.codec_specific_3 |= A2DP_LHDC_V4_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_V4_CODE,
        (A2DP_LHDC_TO_A2DP_CODEC_CAP_ | A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_),
        true);
  }

  LOG_DEBUG( "%s: Has V4 feature?(0x%02x) Src(0x%02x) Sink(0x%02x) result(0x%02x)", __func__,
                                hasFeature,
                                a2dp_lhdc_source_caps.hasFeatureLHDCV4,
                                sink_info_cie.hasFeatureLHDCV4,
                                result_config_cie.hasFeatureLHDCV4);
  LOG_DEBUG( "%s: V4 update:[config:(0x%016llX) cap:(0x%016llX) selcap:(0x%016llX) user:(0x%016llX)] ", __func__,
                                (codec_config_.codec_specific_3 & A2DP_LHDC_V4_ENABLED),
                                (codec_capability_.codec_specific_3 & A2DP_LHDC_V4_ENABLED),
                                (codec_selectable_capability_.codec_specific_3 & A2DP_LHDC_V4_ENABLED),
                                (codec_user_config_.codec_specific_3 & A2DP_LHDC_V4_ENABLED));

  /*******************************************
   * Update Feature/Capabilities: JAS
   * to A2DP specifics
   *******************************************/
  {
    //result_config_cie.hasFeatureJAS = sink_info_cie.hasFeatureJAS;
    hasFeature = (a2dp_lhdc_source_caps.hasFeatureJAS & sink_info_cie.hasFeatureJAS);
    result_config_cie.hasFeatureJAS = false;

    /* *
     * reset bit in A2DP configs
     *     codec_config_.codec_specific_3 &= ~A2DP_LHDC_JAS_ENABLED;
     *     codec_selectable_capability_.codec_specific_3 &= ~A2DP_LHDC_JAS_ENABLED;
     *     codec_capability_.codec_specific_3 &= ~ A2DP_LHDC_JAS_ENABLED;
     * */
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_JAS_CODE,
        (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_|A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_|A2DP_LHDC_TO_A2DP_CODEC_CAP_),
        false);

    //if ((codec_user_config_.codec_specific_3 & A2DP_LHDC_JAS_ENABLED) && hasFeature) {
    /*  06/02/2021: enable JAS without UI control */
    //if( hasFeature && A2DP_IsFeatureInUserConfigLhdcV3(&allCfgPack, LHDC_EXTEND_FUNC_A2DP_LHDC_JAS_CODE) )
    if( hasFeature )
    {
      result_config_cie.hasFeatureJAS = true;
      //codec_config_.codec_specific_3 |= A2DP_LHDC_JAS_ENABLED;
      A2DP_UpdateFeatureToA2dpConfigLhdcV3(
          &allCfgPack,
          LHDC_EXTEND_FUNC_A2DP_LHDC_JAS_CODE,
          A2DP_LHDC_TO_A2DP_CODEC_CONFIG_,
          true);
    }

    if(hasFeature)
    {
      //codec_selectable_capability_.codec_specific_3 |= A2DP_LHDC_JAS_ENABLED;
      //codec_capability_.codec_specific_3 |= A2DP_LHDC_JAS_ENABLED;
      A2DP_UpdateFeatureToA2dpConfigLhdcV3(
          &allCfgPack,
          LHDC_EXTEND_FUNC_A2DP_LHDC_JAS_CODE,
          (A2DP_LHDC_TO_A2DP_CODEC_CAP_ | A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_),
          true);
    }
    LOG_DEBUG( "%s: Has JAS feature?(0x%02x) Src(0x%02x) Sink(0x%02x) result(0x%02x)", __func__,
                                  hasFeature,
                                  a2dp_lhdc_source_caps.hasFeatureJAS,
                                  sink_info_cie.hasFeatureJAS,
                                  result_config_cie.hasFeatureJAS);
    LOG_DEBUG( "%s: JAS update:[config:(0x%016llX) cap:(0x%016llX) selcap:(0x%016llX) user:(0x%016llX)] ", __func__,
                                  (codec_config_.codec_specific_3 & A2DP_LHDC_JAS_ENABLED),
                                  (codec_capability_.codec_specific_3 & A2DP_LHDC_JAS_ENABLED),
                                  (codec_selectable_capability_.codec_specific_3 & A2DP_LHDC_JAS_ENABLED),
                                  (codec_user_config_.codec_specific_3 & A2DP_LHDC_JAS_ENABLED));
  }

  /*******************************************
   * Update Feature/Capabilities: AR
   * to A2DP specifics
   *******************************************/
  //result_config_cie.hasFeatureAR = sink_info_cie.hasFeatureAR;
  hasFeature = (a2dp_lhdc_source_caps.hasFeatureAR & sink_info_cie.hasFeatureAR);
  result_config_cie.hasFeatureAR = false;

  /* *
   * reset bit in A2DP configs
   *     codec_config_.codec_specific_3 &= ~A2DP_LHDC_AR_ENABLED;
   *     codec_selectable_capability_.codec_specific_3 &= ~A2DP_LHDC_AR_ENABLED;
   *     codec_capability_.codec_specific_3 &= ~ A2DP_LHDC_AR_ENABLED;
   * */
  A2DP_UpdateFeatureToA2dpConfigLhdcV3(
      &allCfgPack,
      LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE,
      (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_|A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_|A2DP_LHDC_TO_A2DP_CODEC_CAP_),
      false);

  //if ((codec_user_config_.codec_specific_3 & A2DP_LHDC_AR_ENABLED) && hasFeature) {
  if( hasFeature && A2DP_IsFeatureInUserConfigLhdcV3(&allCfgPack, LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE) ){
    result_config_cie.hasFeatureAR = true;
    //codec_config_.codec_specific_3 |= A2DP_LHDC_AR_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE,
        A2DP_LHDC_TO_A2DP_CODEC_CONFIG_,
        true);
    /* 06/02/2021: When AR function is turned ON, downgrade the sample rate to 48KHz if needed.
     * 	Reason: prevent high CPU loading of AR engine running on higher sample rate(ex:96KHz)
     */
    if(codec_user_config_.sample_rate > BTAV_A2DP_CODEC_SAMPLE_RATE_48000)
    {
    	LOG_DEBUG( "%s: Limit current sample rate(0x%02X) to 48Khz when AR feature turnned on", __func__,
    			codec_user_config_.sample_rate);
		codec_config_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
		codec_user_config_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
		result_config_cie.sampleRate = A2DP_LHDC_SAMPLING_FREQ_48000;
    }
  }

  if (hasFeature) {
    //codec_selectable_capability_.codec_specific_3 |= A2DP_LHDC_AR_ENABLED;
    //codec_capability_.codec_specific_3 |= A2DP_LHDC_AR_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_AR_CODE,
        (A2DP_LHDC_TO_A2DP_CODEC_CAP_ | A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_),
        true);
  }

  LOG_DEBUG( "%s: Has AR feature?(0x%02x) Src(0x%02x) Sink(0x%02x) result(0x%02x)", __func__,
                                hasFeature,
                                a2dp_lhdc_source_caps.hasFeatureAR,
                                sink_info_cie.hasFeatureAR,
                                result_config_cie.hasFeatureAR);
  LOG_DEBUG( "%s: AR update:[config:(0x%016llX) cap:(0x%016llX) selcap:(0x%016llX) user:(0x%016llX)] ", __func__,
                                (codec_config_.codec_specific_3 & A2DP_LHDC_AR_ENABLED),
                                (codec_capability_.codec_specific_3 & A2DP_LHDC_AR_ENABLED),
                                (codec_selectable_capability_.codec_specific_3 & A2DP_LHDC_AR_ENABLED),
                                (codec_user_config_.codec_specific_3 & A2DP_LHDC_AR_ENABLED));

  /*******************************************
   * Update Feature/Capabilities: META
   * to A2DP specifics
   *******************************************/
  //result_config_cie.hasFeatureMETA = sink_info_cie.hasFeatureMETA;
  hasFeature = (a2dp_lhdc_source_caps.hasFeatureMETA & sink_info_cie.hasFeatureMETA);
  result_config_cie.hasFeatureMETA = false;

  /* *
   * reset bit in A2DP configs
   *     codec_config_.codec_specific_3 &= ~A2DP_LHDC_META_ENABLED;
   *     codec_selectable_capability_.codec_specific_3 &= ~A2DP_LHDC_META_ENABLED;
   *     codec_capability_.codec_specific_3 &= ~ A2DP_LHDC_META_ENABLED;
   * */
  A2DP_UpdateFeatureToA2dpConfigLhdcV3(
      &allCfgPack,
      LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE,
      (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_|A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_|A2DP_LHDC_TO_A2DP_CODEC_CAP_),
      false);

  //if ((codec_user_config_.codec_specific_3 & A2DP_LHDC_META_ENABLED) && hasFeature) {
  if( hasFeature && A2DP_IsFeatureInUserConfigLhdcV3(&allCfgPack, LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE) ){
    result_config_cie.hasFeatureMETA = true;
    //codec_config_.codec_specific_3 |= A2DP_LHDC_META_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE,
        A2DP_LHDC_TO_A2DP_CODEC_CONFIG_,
        true);
  }

  if (hasFeature) {
    //codec_selectable_capability_.codec_specific_3 |= A2DP_LHDC_META_ENABLED;
    //codec_capability_.codec_specific_3 |= A2DP_LHDC_META_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_META_CODE,
        (A2DP_LHDC_TO_A2DP_CODEC_CAP_ | A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_),
        true);
  }

  LOG_DEBUG( "%s: Has META feature?(0x%02x) Src(0x%02x) Sink(0x%02x) result(0x%02x)", __func__,
                                hasFeature,
                                a2dp_lhdc_source_caps.hasFeatureMETA,
                                sink_info_cie.hasFeatureMETA,
                                result_config_cie.hasFeatureMETA);
  LOG_DEBUG( "%s: META update:[config:(0x%016llX) cap:(0x%016llX) selcap:(0x%016llX) user:(0x%016llX)] ", __func__,
                                (codec_config_.codec_specific_3 & A2DP_LHDC_META_ENABLED),
                                (codec_capability_.codec_specific_3 & A2DP_LHDC_META_ENABLED),
                                (codec_selectable_capability_.codec_specific_3 & A2DP_LHDC_META_ENABLED),
                                (codec_user_config_.codec_specific_3 & A2DP_LHDC_META_ENABLED));

  /*******************************************
   * Update Feature/Capabilities: MBR
   * to A2DP specifics
   *******************************************/
  //result_config_cie.hasFeatureMinBitrate = sink_info_cie.hasFeatureMinBitrate;
  hasFeature = (a2dp_lhdc_source_caps.hasFeatureMinBitrate & sink_info_cie.hasFeatureMinBitrate);
  result_config_cie.hasFeatureMinBitrate = false;

  /* *
   * reset bit in A2DP configs
   *     codec_config_.codec_specific_3 &= ~A2DP_LHDC_MBR_ENABLED;
   *     codec_selectable_capability_.codec_specific_3 &= ~A2DP_LHDC_MBR_ENABLED;
   *     codec_capability_.codec_specific_3 &= ~ A2DP_LHDC_MBR_ENABLED;
   * */
  A2DP_UpdateFeatureToA2dpConfigLhdcV3(
      &allCfgPack,
      LHDC_EXTEND_FUNC_A2DP_LHDC_MBR_CODE,
      (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_|A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_|A2DP_LHDC_TO_A2DP_CODEC_CAP_),
      false);

  if (hasFeature) {
    result_config_cie.hasFeatureMinBitrate = true;
    //codec_config_.codec_specific_3 |= A2DP_LHDC_MBR_ENABLED;
    //codec_selectable_capability_.codec_specific_3 |= A2DP_LHDC_MBR_ENABLED;
    //codec_capability_.codec_specific_3 |= A2DP_LHDC_MBR_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_MBR_CODE,
        (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_ | A2DP_LHDC_TO_A2DP_CODEC_CAP_ | A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_),
        true);
  }

  LOG_DEBUG( "%s: Has MBR feature?(0x%02x) Src(0x%02x) Sink(0x%02x) result(0x%02x)", __func__,
                                hasFeature,
                                a2dp_lhdc_source_caps.hasFeatureMinBitrate,
                                sink_info_cie.hasFeatureMinBitrate,
                                result_config_cie.hasFeatureMinBitrate);
  LOG_DEBUG( "%s: MBR update:[config:(0x%016llX) cap:(0x%016llX) selcap:(0x%016llX) user:(0x%016llX)] ", __func__,
                                (codec_config_.codec_specific_3 & A2DP_LHDC_MBR_ENABLED),
                                (codec_capability_.codec_specific_3 & A2DP_LHDC_MBR_ENABLED),
                                (codec_selectable_capability_.codec_specific_3 & A2DP_LHDC_MBR_ENABLED),
                                (codec_user_config_.codec_specific_3 & A2DP_LHDC_MBR_ENABLED));


  /*******************************************
   * Update Feature/Capabilities: LARC
   * to A2DP specifics
   *******************************************/
  //result_config_cie.hasFeatureLARC = sink_info_cie.hasFeatureLARC;
  hasFeature = (a2dp_lhdc_source_caps.hasFeatureLARC & sink_info_cie.hasFeatureLARC);
  result_config_cie.hasFeatureLARC = false;

  /* *
   * reset bit in A2DP configs
   *     codec_config_.codec_specific_3 &= ~A2DP_LHDC_LARC_ENABLED;
   *     codec_selectable_capability_.codec_specific_3 &= ~A2DP_LHDC_LARC_ENABLED;
   *     codec_capability_.codec_specific_3 &= ~ A2DP_LHDC_LARC_ENABLED;
   * */
  A2DP_UpdateFeatureToA2dpConfigLhdcV3(
      &allCfgPack,
      LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE,
      (A2DP_LHDC_TO_A2DP_CODEC_CONFIG_|A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_|A2DP_LHDC_TO_A2DP_CODEC_CAP_),
      false);

  //if ((codec_user_config_.codec_specific_3 & A2DP_LHDC_LARC_ENABLED) && hasFeature) {
  //if( hasFeature && A2DP_IsFeatureInUserConfigLhdcV3(&allCfgPack, LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE) ){
  if( hasFeature ){
    result_config_cie.hasFeatureLARC = true;
    //codec_config_.codec_specific_3 |= A2DP_LHDC_LARC_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE,
        A2DP_LHDC_TO_A2DP_CODEC_CONFIG_,
        true);
  }

  if (hasFeature) {
    //codec_selectable_capability_.codec_specific_3 |= A2DP_LHDC_LARC_ENABLED;
    //codec_capability_.codec_specific_3 |= A2DP_LHDC_LARC_ENABLED;
    A2DP_UpdateFeatureToA2dpConfigLhdcV3(
        &allCfgPack,
        LHDC_EXTEND_FUNC_A2DP_LHDC_LARC_CODE,
        (A2DP_LHDC_TO_A2DP_CODEC_CAP_ | A2DP_LHDC_TO_A2DP_CODEC_SELECT_CAP_),
        true);
  }

  LOG_DEBUG( "%s: Has LARC feature?(0x%02x) Src(0x%02x) Sink(0x%02x) result(0x%02x)", __func__,
                                hasFeature,
                                a2dp_lhdc_source_caps.hasFeatureLARC,
                                sink_info_cie.hasFeatureLARC,
                                result_config_cie.hasFeatureLARC);
  LOG_DEBUG( "%s: LARC update:[config:(0x%016llX) cap:(0x%016llX) selcap:(0x%016llX) user:(0x%016llX)] ", __func__,
                                (codec_config_.codec_specific_3 & A2DP_LHDC_LARC_ENABLED),
                                (codec_capability_.codec_specific_3 & A2DP_LHDC_LARC_ENABLED),
                                (codec_selectable_capability_.codec_specific_3 & A2DP_LHDC_LARC_ENABLED),
                                (codec_user_config_.codec_specific_3 & A2DP_LHDC_LARC_ENABLED));


  /*******************************************
   * quality mode: re-adjust according to maxTargetBitrate(smaller one adopted)
   *******************************************/
  if ( (result_config_cie.hasFeatureLLAC && result_config_cie.hasFeatureLHDCV4) &&
	   (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_96000) &&
	   (quality_mode != A2DP_LHDC_QUALITY_ABR))
  {
	  //In this case, max bit rate mechanism is disabled(set to 900k)
	  result_config_cie.maxTargetBitrate = A2DP_LHDC_MAX_BIT_RATE_900K;
	  LOG_DEBUG("%s: [LLAC + LHDC V4]: set MBR (0x%x)", __func__, result_config_cie.maxTargetBitrate);

	  //dont re-adjust quality mode in this case
	  LOG_DEBUG("%s: do not adjust quality_mode in this case", __func__);
  }
  else
  {
	  maxBitRate_Qmode = A2DP_MaxBitRatetoQualityLevelLhdcV3(result_config_cie.maxTargetBitrate);
	  if(maxBitRate_Qmode < 0xFF) {
		  if(quality_mode != A2DP_LHDC_QUALITY_ABR && quality_mode > maxBitRate_Qmode){
			  LOG_DEBUG("%s: adjust quality_mode:0x%x to 0x%x by maxTargetBitrate:0x%x", __func__,
					  quality_mode, maxBitRate_Qmode, result_config_cie.maxTargetBitrate);
			  quality_mode = maxBitRate_Qmode;
			  codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | quality_mode;
		  }
	  }
  }

  /*
   * Final Custom Rules of resolving conflict between capabilities and version
   */
  if (result_config_cie.hasFeatureLLAC && result_config_cie.hasFeatureLHDCV4) {
    //LHDCV4 + LLAC
    if (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_96000) {

      if (quality_mode == A2DP_LHDC_QUALITY_ABR) {
		result_config_cie.sampleRate = A2DP_LHDC_SAMPLING_FREQ_48000;
		codec_config_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
		codec_user_config_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;	//also set UI settings
		result_config_cie.hasFeatureLHDCV4 = false;
		codec_config_.codec_specific_3 &= ~A2DP_LHDC_V4_ENABLED;
		LOG_DEBUG("%s: [LLAC + LHDC V4]: LLAC, reset sampleRate (0x%x)", __func__, result_config_cie.sampleRate);
      } else {
        result_config_cie.hasFeatureLLAC = false;
        codec_config_.codec_specific_3 &= ~A2DP_LHDC_LLAC_ENABLED;
        LOG_DEBUG("%s: [LLAC + LHDC V4]: LHDC", __func__);

  	    //result_config_cie.maxTargetBitrate = A2DP_LHDC_MAX_BIT_RATE_900K;
  	    //LOG_DEBUG("%s: [LLAC + LHDC V4]: set MBR (0x%x)", __func__, result_config_cie.maxTargetBitrate);

      	if (result_config_cie.hasFeatureMinBitrate) {
          if (quality_mode < A2DP_LHDC_QUALITY_MID) {
            codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_MID;
            quality_mode = A2DP_LHDC_QUALITY_MID;
            LOG_DEBUG("%s: [LLAC + LHDC V4]: LHDC 96KSR, reset Qmode (0x%x)", __func__, quality_mode);
          }
      	} else {
    	  if (quality_mode < A2DP_LHDC_QUALITY_LOW) {
		    codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_LOW;
		    quality_mode = A2DP_LHDC_QUALITY_LOW;
		    LOG_DEBUG("%s: [LLAC + LHDC V4]: LHDC 96KSR, reset Qmode (0x%x)", __func__, quality_mode);
		  }
      	}
      }
    } else if (
        (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_48000 && (quality_mode > A2DP_LHDC_QUALITY_LOW && quality_mode != A2DP_LHDC_QUALITY_ABR)) ||
        (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_44100 && (quality_mode > A2DP_LHDC_QUALITY_LOW && quality_mode != A2DP_LHDC_QUALITY_ABR))
       ) {
      result_config_cie.hasFeatureLLAC = false;
      codec_config_.codec_specific_3 &= ~A2DP_LHDC_LLAC_ENABLED;
      LOG_DEBUG("%s: [LLAC + LHDC V4]: LHDC", __func__);
    } else if (
       (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_48000 && (quality_mode <= A2DP_LHDC_QUALITY_LOW || quality_mode == A2DP_LHDC_QUALITY_ABR)) ||
       (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_44100 && (quality_mode <= A2DP_LHDC_QUALITY_LOW || quality_mode == A2DP_LHDC_QUALITY_ABR))
      ) {
      result_config_cie.hasFeatureLHDCV4 = false;
      codec_config_.codec_specific_3 &= ~A2DP_LHDC_V4_ENABLED;
      LOG_DEBUG("%s: [LLAC + LHDC V4]: LLAC", __func__);

      /* LLAC: prevent quality mode using 64kbps */
      if (result_config_cie.hasFeatureMinBitrate) {
    	if (quality_mode < A2DP_LHDC_QUALITY_LOW1) {
    	  codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_LOW1;
    	  quality_mode = A2DP_LHDC_QUALITY_LOW1;
    	  LOG_DEBUG("%s: [LLAC + LHDC V4]: LLAC, reset Qmode (0x%x)", __func__, quality_mode);
    	}
      }
    } else {
      LOG_ERROR("%s: [LLAC + LHDC V4]: format incorrect.", __func__);
      goto fail;
    }

  } else if (!result_config_cie.hasFeatureLLAC && result_config_cie.hasFeatureLHDCV4) {
    //LHDC V4 only
    LOG_DEBUG("%s: [LHDCV4 only]", __func__);
	if (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_96000) {
		if (result_config_cie.hasFeatureMinBitrate) {
			if (quality_mode < A2DP_LHDC_QUALITY_LOW) {
				codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_LOW;
				quality_mode = A2DP_LHDC_QUALITY_LOW;
				LOG_DEBUG("%s: [LHDCV4 only]: reset Qmode (0x%x)", __func__, quality_mode);
			}
		}
	} else {
		if (result_config_cie.hasFeatureMinBitrate) {
			if (quality_mode < A2DP_LHDC_QUALITY_LOW4) {
				codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_LOW4;
				quality_mode = A2DP_LHDC_QUALITY_LOW4;
				LOG_DEBUG("%s: [LHDCV4 only]: reset Qmode (0x%x), ", __func__, quality_mode);
			}
		}
	}

  } else if (result_config_cie.hasFeatureLLAC && !result_config_cie.hasFeatureLHDCV4) {
    //LLAC only
    LOG_DEBUG("%s: [LLAC only]", __func__);
    if (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_96000) {
      result_config_cie.sampleRate = A2DP_LHDC_SAMPLING_FREQ_48000;
      codec_config_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
      codec_user_config_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;	//also set UI settings
      LOG_DEBUG("%s: [LLAC only]: reset SampleRate (0x%x)", __func__, result_config_cie.sampleRate);
    }

    if (quality_mode > A2DP_LHDC_QUALITY_LOW && quality_mode != A2DP_LHDC_QUALITY_ABR) {
      codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_LOW;
      quality_mode = A2DP_LHDC_QUALITY_LOW;
      LOG_DEBUG("%s: [LLAC only]: reset Qmode (0x%x)", __func__, quality_mode);
    }
    
    /* LLAC: prevent quality mode using 64kbps */
    if (result_config_cie.hasFeatureMinBitrate) {
		if (quality_mode < A2DP_LHDC_QUALITY_LOW1) {
		  codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_LOW1;
		  quality_mode = A2DP_LHDC_QUALITY_LOW1;
		  LOG_DEBUG("%s: [LLAC only]: reset Qmode (0x%x)", __func__, quality_mode);
		}
    }

  } else if (!result_config_cie.hasFeatureLLAC && !result_config_cie.hasFeatureLHDCV4) {
    //LHDC V3 only
    LOG_DEBUG("%s: [LHDCV3 only]", __func__);
	if (result_config_cie.sampleRate == A2DP_LHDC_SAMPLING_FREQ_96000) {
		if (result_config_cie.hasFeatureMinBitrate) {
			if (quality_mode < A2DP_LHDC_QUALITY_LOW) {
				codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_LOW;
				quality_mode = A2DP_LHDC_QUALITY_LOW;
				LOG_DEBUG("%s: [LHDCV3 only]: reset Qmode (0x%x)", __func__, quality_mode);
			}
		}
	} else {
		if (result_config_cie.hasFeatureMinBitrate) {
			if (quality_mode < A2DP_LHDC_QUALITY_LOW4) {
				codec_user_config_.codec_specific_1 = A2DP_LHDC_QUALITY_MAGIC_NUM | A2DP_LHDC_QUALITY_LOW4;
				quality_mode = A2DP_LHDC_QUALITY_LOW4;
				LOG_DEBUG("%s: [LHDCV3 only]: reset Qmode (0x%x), ", __func__, quality_mode);
			}
		}
	}
  }

  LOG_DEBUG("%s: Final quality_mode = (%d) %s", __func__,
      quality_mode,
      lhdcV3_QualityModeBitRate_toString(quality_mode).c_str());

  //
  // Copy the codec-specific fields if they are not zero
  //
  if (codec_user_config_.codec_specific_1 != 0)
    codec_config_.codec_specific_1 = codec_user_config_.codec_specific_1;
  if (codec_user_config_.codec_specific_2 != 0)
    codec_config_.codec_specific_2 = codec_user_config_.codec_specific_2;
  if (codec_user_config_.codec_specific_3 != 0)
    codec_config_.codec_specific_3 = codec_user_config_.codec_specific_3;
  if (codec_user_config_.codec_specific_4 != 0)
    codec_config_.codec_specific_4 = codec_user_config_.codec_specific_4;
  
  /* Setup final nego result codec config to peer */
  if (int ret = A2DP_BuildInfoLhdcV3(AVDT_MEDIA_TYPE_AUDIO, &result_config_cie,
                         p_result_codec_config) != A2DP_SUCCESS) {
    LOG_ERROR("%s: A2DP_BuildInfoLhdcV3 fail(0x%x)", __func__, ret);
    goto fail;
  }


  // Create a local copy of the peer codec capability, and the
  // result codec config.
    LOG_ERROR("%s: is_capability = %d", __func__, is_capability);
  if (is_capability) {
    status = A2DP_BuildInfoLhdcV3(AVDT_MEDIA_TYPE_AUDIO, &sink_info_cie,
                                ota_codec_peer_capability_);
  } else {
    status = A2DP_BuildInfoLhdcV3(AVDT_MEDIA_TYPE_AUDIO, &sink_info_cie,
                                ota_codec_peer_config_);
  }
  CHECK(status == A2DP_SUCCESS);

  status = A2DP_BuildInfoLhdcV3(AVDT_MEDIA_TYPE_AUDIO, &result_config_cie,
                              ota_codec_config_);
  CHECK(status == A2DP_SUCCESS);

  LOG_DEBUG( "%s: Final User_SP1=(0x%016llX); SP2=(0x%016llX); SP3=(0x%016llX); SP4=(0x%016llX)", __func__,
		  (unsigned long long)codec_user_config_.codec_specific_1,
		  (unsigned long long)codec_user_config_.codec_specific_2,
		  (unsigned long long)codec_user_config_.codec_specific_3,
		  (unsigned long long)codec_user_config_.codec_specific_4);
  LOG_DEBUG( "%s: Final Codec_SP1=(0x%016llX); SP2=(0x%016llX); SP3=(0x%016llX); SP4=(0x%016llX)", __func__,
		  (unsigned long long)(codec_config_.codec_specific_1),
		  (unsigned long long)(codec_config_.codec_specific_2),
		  (unsigned long long)(codec_config_.codec_specific_3),
		  (unsigned long long)(codec_config_.codec_specific_4));

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



bool A2dpCodecConfigLhdcV3::setPeerCodecCapabilities(
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
    A2DP_ParseInfoLhdcV3(&peer_info_cie, p_peer_codec_capabilities, true);
    if (status != A2DP_SUCCESS) {
        LOG_ERROR( "%s: can't parse peer's capabilities: error = %d",
                  __func__, status);
        goto fail;
    }
/*
    if (peer_info_cie.version > a2dp_lhdc_source_caps.version) {
        LOG_ERROR( "%s: can't parse peer's capabilities: Missmatch version(%u:%u)",
                  __func__, a2dp_lhdc_source_caps.version, peer_info_cie.version);
        goto fail;
    }
*/

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

    status = A2DP_BuildInfoLhdcV3(AVDT_MEDIA_TYPE_AUDIO, &peer_info_cie,
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


