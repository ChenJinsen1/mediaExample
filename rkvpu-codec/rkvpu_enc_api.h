/*
 * Copyright 2021 Rockchip Electronics Co. LTD
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
 *
 * author: kevin.chen@rock-chips.com
 * module: RKHWEncApi
 * date  : 2021/02/17
 */

#ifndef __RKVPU_ENC_API_H__
#define __RKVPU_ENC_API_H__

#include "vpu_api.h"

#define OMX_BUFFERFLAG_EOS              0x00000001

typedef enum VPU_RET {
    VPU_OK                      = 0,
    VPU_ERR_UNKNOW              = -1,
    VPU_ERR_BASE                = -1000,
    VPU_ERR_LIST_STREAM         = VPU_API_ERR_BASE - 1,
    VPU_ERR_INIT                = VPU_API_ERR_BASE - 2,
    VPU_ERR_VPU_CODEC_INIT      = VPU_API_ERR_BASE - 3,
    VPU_ERR_STREAM              = VPU_API_ERR_BASE - 4,
    VPU_ERR_FATAL_THREAD        = VPU_API_ERR_BASE - 5,
    VPU_EAGAIN                  = VPU_API_ERR_BASE - 6,
    VPU_EOS_STREAM_REACHED      = VPU_API_ERR_BASE - 11,
} VPU_RET;

/* Rate control parameter */
typedef enum MppEncRcMode_e {
    ENC_RC_MODE_VBR,    // Variable Bit Rate, QP_range first
    ENC_RC_MODE_CBR,    // Constant Bit Rate, bitRate first
    ENC_RC_MODE_FIXQP,  // fixed QP mode
} EncRcMode;

class RKHWEncApi
{
public:
    RKHWEncApi();
    ~RKHWEncApi();

    typedef struct EncCfgInfo {
        int32_t width;
        int32_t height;
        OMX_RK_VIDEO_CODINGTYPE coding;
        int32_t format;       /* input yuv format */
        int32_t IDRInterval;
        int32_t rc_mode;      /* 0 - VBR mode; 1 - CBR mode; 2 - FIXQP mode */
        int32_t bitRate;      /* target bitrate */
        int32_t framerate;
        int32_t qp;
    } EncCfgInfo_t;

    VPU_RET prepare(EncCfgInfo *cfg);

    /*
     * send video frame to encoder only, async interface
     */
    VPU_RET sendFrame(char *data, int32_t size, int64_t pts, int32_t flag);

    /*
     * get encoded video packet from encoder only, async interface
     */
    VPU_RET getOutStream(EncoderOut_t *encOut);

private:
    VpuCodecContext *mVpuCtx;
    unsigned char *mOutputBuf;
    unsigned char *mSpsPpsBuf;
    int32_t mSpsPpsLen;
    OMX_RK_VIDEO_CODINGTYPE mCoding;

    int32_t mInitOK;
    int32_t mFrameCount;
};

#endif  // __RKVPU_ENC_API_H__

