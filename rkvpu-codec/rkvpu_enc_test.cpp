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
 * module: native-codec: rkvpu_enc_test sample code
 * date  : 2021/02/17
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "rkvpu_enc_test"
#include "utils/Log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <getopt.h>

#include "rkvpu_enc_api.h"

#define MAX_FILE_LEN  128

typedef struct {
    struct timeval start;
    struct timeval end;
} DebugTimeInfo;

static DebugTimeInfo time_info;

static void time_start_record()
{
    gettimeofday(&time_info.start, NULL);
}

static int64_t time_end_record()
{
    gettimeofday(&time_info.end, NULL);
    return ((time_info.end.tv_sec  - time_info.start.tv_sec)  * 1000) +
           ((time_info.end.tv_usec - time_info.start.tv_usec) / 1000);
}

typedef struct EncTestCtx_t {
    // src and dst
    char fileInput[MAX_FILE_LEN];
    char fileOutput[MAX_FILE_LEN];
    bool hasOutput;

    /* vpu configuration settings */
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t bitRate;
    int32_t frameRate;

    int32_t numBuffersEncoded;
} EncTestCtx;

/*
 * Dumps usage on stderr.
 */
static void testUsage()
{
    fprintf(stderr,
        "\nUsage: rkvpu_enc_test [options] \n"
        "Rockchip VpuApiLegacy encoder demo(h264 default).\n"
        "  - rkvpu_enc_test --i input.yuv --o out.h264 --w 1280 --h 720\n"
        "\n"
        "Options:\n"
        "--u\n"
        "    Show this message.\n"
        "--i\n"
        "    input yuv file\n"
        "--o\n"
        "    output bitstream files\n"
        "--w\n"
        "    the width of input yuv\n"
        "--h\n"
        "    the height of input yuv\n"
        "--f\n"
        "    the framerate of encoder, deault 30fps\n"
        "--b\n"
        "    the bitrate of encoder, default 3Mbps\n"
        "\n");
}

VPU_RET testParseArgs(EncTestCtx *ctx, int argc, char **argv)
{
    static const struct option longOptions[] = {
        { "usage",              no_argument,        NULL, 'u' },
        { "input",              required_argument,  NULL, 'i' },
        { "output",             required_argument,  NULL, 'o' },
        { "width",              required_argument,  NULL, 'w' },
        { "height",             required_argument,  NULL, 'h' },
        { NULL,                 0,                  NULL, 0 }
    };

    ctx->width = 0;
    ctx->height = 0;
    ctx->frameRate = 0;
    ctx->bitRate = 0;
    ctx->hasOutput = false;

    bool hasInput = false;

    while (true) {
        int optionIndex = 0;
        int ic = getopt_long(argc, argv, "", longOptions, &optionIndex);
        if (ic == -1) {
            break;
        }

        switch (ic) {
        case 'u':
            return VPU_ERR_UNKNOW;
        case 'i':
            strcpy(ctx->fileInput, optarg);
            hasInput = true;
            break;
        case 'o':
            strcpy(ctx->fileOutput, optarg);
            ctx->hasOutput = true;
            break;
        case 'w':
            ctx->width = atoi(optarg);
            break;
        case 'h':
            ctx->height = atoi(optarg);
            break;
        case 'f':
            ctx->frameRate = atoi(optarg);
            break;
        case 'b':
            ctx->bitRate = atoi(optarg);
            break;
        default:
            fprintf(stderr, "getopt_long returned unexpected value 0x%x\n", ic);
            return VPU_ERR_UNKNOW;
        }
    }

    if (!hasInput || ctx->width <= 0 || ctx->height <= 0) {
        fprintf(stderr, "ERROR: must specify input|width|height\n");
        return VPU_ERR_UNKNOW;
    }

    if (ctx->bitRate <= 0) {
        ctx->bitRate = 3000000; // 3Mbps
    }
    if (ctx->frameRate <= 0) {
        ctx->frameRate = 30; // 30fps
    }
    ctx->numBuffersEncoded = 0;

    // dump cmd options
    fprintf(stderr, "\ncmd parse result:\n"
        "   input bitstream file : %s\n"
        "   output bitstream file: %s\n"
        "   input_resolution     : %dx%d\n"
        "   frameRate            : %d\n"
        "   bitRate              : %d\n",
        ctx->fileInput, ctx->fileOutput, ctx->width,
        ctx->height, ctx->frameRate, ctx->bitRate);

    return VPU_OK;
}

VPU_RET runEncoder(RKHWEncApi *encApi, EncTestCtx *encCtx)
{
    VPU_RET ret = VPU_OK;
    FILE *fpInput = NULL, *fpOutput = NULL;
    char *pktBuf = NULL;
    int32_t pktsize;

    bool sawInputEOS = false, signalledInputEOS = false;
    // Indicates that the last buffer has delivered to vpu_encoder
    bool lastPktQueued = true;
    int32_t readsize;

    if (encCtx->format <= ENC_INPUT_YUV422_INTERLEAVED_UYVY) {
        pktsize = encCtx->width * encCtx->height * 3 / 2;
    } else if (encCtx->format <= ENC_INPUT_BGR888) {
        pktsize = encCtx->width * encCtx->height * 3;
    } else {
        pktsize = encCtx->width * encCtx->height * 4;
    }
    pktBuf = (char*)malloc(sizeof(char) * pktsize);

    // input and output dst
    fpInput = fopen(encCtx->fileInput, "rb+");
    if (fpInput == NULL) {
        fprintf(stderr, "failed to open input file %s\n", encCtx->fileInput);
        ret = VPU_ERR_INIT;
        goto ENCODE_OUT;
    }

    if (encCtx->hasOutput) {
        fpOutput = fopen(encCtx->fileOutput, "wb+");
        if (fpOutput == NULL) {
            fprintf(stderr, "failed to open output file %s\n", encCtx->fileOutput);
            ret = VPU_ERR_INIT;
            goto ENCODE_OUT;
        }
    }

    while (true) {
        if (!sawInputEOS && lastPktQueued) {
            readsize = fread(pktBuf, 1, pktsize, fpInput);
            if (readsize != pktsize && feof(fpInput)) {
                ALOGD("saw input eos");
                sawInputEOS = true;
            }
            lastPktQueued = false;
        }

        if (!sawInputEOS) {
            ret = encApi->sendFrame(pktBuf, readsize, 0, 0);
            if (!ret) {
                lastPktQueued = true;
            } else {
                /* reduce cpu overhead here */
                usleep(1000);
            }
        } else {
            if (!signalledInputEOS) {
                ret = encApi->sendFrame(pktBuf, readsize, 0, OMX_BUFFERFLAG_EOS);
                if (ret == VPU_OK) {
                    lastPktQueued = true;
                    signalledInputEOS = true;
                } else {
                    usleep(1000);
                }
            }
        }

        EncoderOut_t encOut;
        ret = encApi->getOutStream(&encOut);
        if (ret == VPU_OK) {
            ++encCtx->numBuffersEncoded;

            if (encCtx->hasOutput) {
                fwrite(encOut.data, 1, encOut.size, fpOutput);
                fflush(fpOutput);
            }
        } else if (ret == VPU_EAGAIN) {
            /* reduce cpu overhead here */
            usleep(1000);
        } else if (ret == VPU_EOS_STREAM_REACHED) {
            ALOGD("saw output eos");
            break;
        }
    }

    ret = VPU_OK;

ENCODE_OUT:
    free(pktBuf);

    if (fpInput != NULL)
        fclose(fpInput);

    if (fpOutput != NULL)
        fclose(fpOutput);

    return ret;
}

int main(int argc, char **argv)
{
    VPU_RET ret = VPU_OK;
    EncTestCtx encCtx;
    RKHWEncApi encApi;
    RKHWEncApi::EncCfgInfo cfg;

    // parse the cmd option
    if (argc > 0)
        ret = testParseArgs(&encCtx, argc, argv);

    if (ret != VPU_OK) {
        testUsage();
        return 1;
    }

    /* setup RKHWEncApi::EncCfgInfo by encCtx */
    cfg.width = encCtx.width;
    cfg.height = encCtx.height;
    cfg.coding = OMX_RK_VIDEO_CodingAVC;  // h264 default
    encCtx.format = ENC_INPUT_YUV420_SEMIPLANAR;
    cfg.format = encCtx.format;           // input format: yuv420p default
    cfg.framerate = encCtx.frameRate;
    cfg.bitRate = encCtx.bitRate;
    cfg.IDRInterval = 1; // 1 seconds 1 gop

    /*
     * rc_mode: Rate control parameter
     *   - ENC_RC_MODE_VBR  : Variable Bit Rate, QP_range first
     *   - ENC_RC_MODE_CBR  : Constant Bit Rate, bitRate first
     *   - ENC_RC_MODE_FIXQP: fixed QP mode
     */
    cfg.rc_mode = ENC_RC_MODE_CBR;
    cfg.qp = 20;

    ret = encApi.prepare(&cfg);
    if (ret) {
        fprintf(stderr, "ERROR: encApi prapare failed(err=%d)", ret);
        return 1;
    }

    time_start_record();

    ret = runEncoder(&encApi, &encCtx);
    if (ret != VPU_OK) {
        fprintf(stderr, "ERROR: enc_test failed(err=%d)", ret);
        return 1;
    } else {
        int64_t elapsedTimeUs = time_end_record();
        printf("\nenc_test done, %lld frames encoded in %lld ms, %.2f fps\n",
               (long long)encCtx.numBuffersEncoded, elapsedTimeUs / 1000,
               encCtx.numBuffersEncoded * 1E6 / elapsedTimeUs);
    }

    return 0;
}
