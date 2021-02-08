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
 * module: native-codec: rkvpu_dec_test sample code
 * date  : 2021/02/06
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "rkvpu_dec_test"
#include "utils/Log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <getopt.h>

#include "rkvpu_dec_api.h"

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

typedef struct DecTestCtx_t {
    // src and dst
    char fileInput[MAX_FILE_LEN];
    char fileOutput[MAX_FILE_LEN];
    bool hasOutput;

    /* vpu configuration settings */
    OMX_RK_VIDEO_CODINGTYPE videoCoding;
    int32_t width;
    int32_t height;

    int32_t numBuffersDecoded;
} DecTestCtx;

/*
 * Dumps usage on stderr.
 */
static void testUsage() {
    fprintf(stderr,
        "\nUsage: rkvpu_dec_test [options] \n"
        "Rockchip VpuApiLegacy decoder demo.\n"
        "  - rkvpu_dec_test --i input.h264 --o out.yuv --w 1280 --h 720 --t 1\n"
        "\n"
        "Options:\n"
        "--u\n"
        "    Show this message.\n"
        "--i\n"
        "    input file\n"
        "--o\n"
        "    output bitstream files\n"
        "--w\n"
        "    the width of input picture\n"
        "--h\n"
        "    the height of input picture\n"
        "--t\n"
        "    input pictrue type(h264 default):\n"
        "        1: h264\n"
        "        2: h265\n"
        "\n");
}

VPU_RET testParseArgs(DecTestCtx *ctx, int argc, char **argv) {
    static const struct option longOptions[] = {
        { "usage",              no_argument,        NULL, 'u' },
        { "input",              required_argument,  NULL, 'i' },
        { "output",             required_argument,  NULL, 'o' },
        { "width",              required_argument,  NULL, 'w' },
        { "height",             required_argument,  NULL, 'h' },
        { "type",               required_argument,  NULL, 't' },
        { NULL,                 0,                  NULL, 0 }
    };

    ctx->width = 0;
    ctx->height = 0;
    ctx->hasOutput = false;
    ctx->videoCoding = OMX_RK_VIDEO_CodingAVC; // h264 defualt
    ctx->numBuffersDecoded = 0;

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
        case 't':
            if (atoi(optarg) == 2) {
                ctx->videoCoding = OMX_RK_VIDEO_CodingHEVC;
            } else {
                ctx->videoCoding = OMX_RK_VIDEO_CodingAVC;
            }
            break;
        default:
            fprintf(stderr, "getopt_long returned unexpected value 0x%x\n", ic);
            return VPU_ERR_UNKNOW;
        }
    }

    if (!hasInput) {
        fprintf(stderr, "ERROR: must specify input file\n");
        return VPU_ERR_UNKNOW;
    }

    // dump cmd options
    fprintf(stderr, "\ncmd parse result:\n"
        "   input bitstream file : %s\n"
        "   output bitstream file: %s\n"
        "   input_resolution     : %dx%d\n"
        "   input video coding   : %d\n",
        ctx->fileInput, ctx->fileOutput, ctx->width,
        ctx->height, ctx->videoCoding);

    return VPU_OK;
}


VPU_RET runDecoder(RKHWDecApi *decApi, DecTestCtx *decCtx)
{
    VPU_RET ret = VPU_OK;
    FILE *fpInput = NULL, *fpOutput = NULL;
    char *pktBuf = NULL;
    int32_t pktsize = 1000; // 1000 byte

    bool sawInputEOS = false;
    // Indicates that the last buffer has delivered to vpu_decoder
    bool lastPktQueued = true;
    int32_t readsize;

    // input and output dst
    pktBuf = (char*)malloc(sizeof(char) * pktsize);

    fpInput = fopen(decCtx->fileInput, "rb+");
    if (fpInput == NULL) {
        fprintf(stderr, "failed to open input file %s\n", decCtx->fileInput);
        ret = VPU_ERR_INIT;
        goto DECODE_OUT;
    }

    if (decCtx->hasOutput) {
        fpOutput = fopen(decCtx->fileOutput, "wb+");
        if (fpOutput == NULL) {
            fprintf(stderr, "failed to open output file %s\n", decCtx->fileOutput);
            ret = VPU_ERR_INIT;
            goto DECODE_OUT;
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
            ret = decApi->sendStream(pktBuf, readsize, 0, 0);
            if (!ret) {
                lastPktQueued = true;
            } else {
                /* reduce cpu overhead here */
                usleep(1000);
            }
        } else {
            ret = decApi->sendStream(pktBuf, readsize, 0, OMX_BUFFERFLAG_EOS);
            if (!ret) {
                lastPktQueued = true;
            } else {
                usleep(1000);
            }
        }

        VPU_FRAME vframe;
        ret = decApi->getOutFrame(&vframe);
        if (ret == VPU_OK) {
            ++decCtx->numBuffersDecoded;

            if (decCtx->hasOutput) {
                fwrite(vframe.vpumem.vir_addr, 1, vframe.vpumem.size, fpOutput);
                fflush(fpOutput);
            }

            /*
             * VPU_FRAME buffers used recycled inside decoder, so release
             * that buffer which has been display success.
             */
             decApi->deinitOutFrame(&vframe);
        } else if (ret == VPU_EAGAIN) {
            /* reduce cpu overhead here */
            usleep(1000);
        } else if (ret == VPU_EOS_STREAM_REACHED) {
            ALOGD("saw output eos");
            break;
        }
    }

    ret = VPU_OK;

DECODE_OUT:
    free(pktBuf);

    if (fpInput != NULL)
        fclose(fpInput);

    if (fpOutput != NULL)
        fclose(fpOutput);

    return ret;
}

int main(int argc, char **argv) {
    VPU_RET ret = VPU_OK;
    DecTestCtx decCtx;
    RKHWDecApi decApi;

    // parse the cmd option
    if (argc > 1)
        ret = testParseArgs(&decCtx, argc, argv);

    if (ret != VPU_OK) {
        testUsage();
        return 1;
    }

    ret = decApi.prepare(decCtx.width, decCtx.height, decCtx.videoCoding);
    if (ret) {
        fprintf(stderr, "ERROR: decApi prapare failed(err=%d)", ret);
        return 1;
    }

    time_start_record();

    ret = runDecoder(&decApi, &decCtx);
    if (ret != VPU_OK) {
        fprintf(stderr, "ERROR: dec_test failed(err=%d)", ret);
        return 1;
    } else {
        int64_t elapsedTimeUs = time_end_record();
        printf("\ndec_test done, %lld frames decoded in %lld ms, %.2f fps\n",
               (long long)decCtx.numBuffersDecoded, elapsedTimeUs / 1000,
               decCtx.numBuffersDecoded * 1E6 / elapsedTimeUs);
    }

    return 0;
}
