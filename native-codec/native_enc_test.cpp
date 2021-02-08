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
 * module: native-codec: native_enc_test sample code
 * date  : 2021/01/28
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "native_enc_test"
#include <utils/Log.h>

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/MediaErrors.h>
#include <media/openmax/OMX_IVCommon.h>
#include <media/ICrypto.h>
#include <binder/IPCThreadState.h>
#include <gui/Surface.h>

using namespace android;

#define MAX_FILE_LEN 128

static const char* kMimeTypeAvc = "video/avc";
static int64_t kTimeout = 20 * 1000; // 20ms

typedef struct EncTestArgs_t {
    // src and dst
    char                 file_input[MAX_FILE_LEN];
    char                 file_output[MAX_FILE_LEN];
    FILE                 *fp_input;
    FILE                 *fp_output;

    // configure parameters of encoder
    sp<MediaCodec>       codec;
    Vector<sp<ABuffer> > inBuffers;
    Vector<sp<ABuffer> > outBuffers;
    uint32_t             width;
    uint32_t             height;
    uint32_t             bitRate;
    uint32_t             colorFormat;
    uint32_t             frameRate;
    uint32_t             IDRInterval;
    uint32_t             numBuffersEncoded;
}  EncTestArgs;

/*
 * Dumps usage on stderr.
 */
static void testUsage() {
    fprintf(stderr,
        "Usage: native_enc_test [options] \n"
        "Android native mediacodec encoder demo.\n"
        "  - native_enc_test --i input.yuv --o out.h264 --w 1280 --h 720"
        "\n"
        "Options:\n"
        "--u\n"
        "    Show this message.\n"
        "--i\n"
        "    input bitstream file\n"
        "--o\n"
        "    output bitstream files\n"
        "--w\n"
        "    the width of input picture\n"
        "--h\n"
        "    the height of input picture\n"
        "--b\n"
        "    the bitrate of encoder, default 3Mbps\n"
        "--f\n"
        "    the framerate of encoder, deault 30fps\n"
        "\n");
}

status_t testParseArgs(EncTestArgs *cmd, int argc, char **argv) {
    static const struct option longOptions[] = {
        { "usage",              no_argument,        NULL, 'u' },
        { "input",              required_argument,  NULL, 'i' },
        { "output",             required_argument,  NULL, 'o' },
        { "width",              required_argument,  NULL, 'w' },
        { "height",             required_argument,  NULL, 'h' },
        { "bitrate",            required_argument,  NULL, 'b' },
        { "framerate",          required_argument,  NULL, 'f' },
        { NULL,                 0,                  NULL, 0 }
    };

    cmd->fp_input = NULL;
    cmd->fp_output = NULL;
    cmd->width = 0;
    cmd->height = 0;
    cmd->frameRate = 0;
    cmd->bitRate = 0;

    while (true) {
        int optionIndex = 0;
        int ic = getopt_long(argc, argv, "", longOptions, &optionIndex);
        if (ic == -1) {
            break;
        }

        switch (ic) {
        case 'u':
            return BAD_VALUE;
        case 'i':
            strcpy(cmd->file_input, optarg);
            cmd->fp_input = fopen(cmd->file_input, "rb+");
            if (cmd->fp_input == NULL) {
                fprintf(stderr, "failed to open input file %s\n", cmd->file_input);
                return BAD_VALUE;
            }
            break;
        case 'o':
            strcpy(cmd->file_output, optarg);
            cmd->fp_output = fopen(cmd->file_output, "wb+");
            if (cmd->fp_output == NULL) {
                fprintf(stderr, "failed to open output file %s\n", cmd->file_output);
                return BAD_VALUE;
            }
            break;
        case 'w':
            cmd->width = atoi(optarg);
            break;
        case 'h':
            cmd->height = atoi(optarg);
            break;
        case 'b':
            cmd->bitRate = atoi(optarg);
            break;
        case 'f':
            cmd->frameRate = atoi(optarg);
            break;
        default:
            fprintf(stderr, "getopt_long returned unexpected value 0x%x\n", ic);
            return BAD_VALUE;
        }
    }

    if (cmd->fp_input == NULL || cmd->fp_output == NULL
            || cmd->width == 0 || cmd->height == 0) {
        fprintf(stderr, "ERROR: test must specify input|output|width|height");
        return BAD_VALUE;
    }

    if (cmd->bitRate <= 0) {
        cmd->bitRate = 3000000; // 3Mbps
    }
    if (cmd->frameRate <= 0) {
        cmd->frameRate = 30; // 30fps
    }
    cmd->numBuffersEncoded = 0;
    cmd->colorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    cmd->IDRInterval = 1;

    // dump cmd options
    fprintf(stderr,
        "\ncmd parse result:\n"
        "   input bitstream file : %s\n"
        "   output bitstream file: %s\n"
        "   resolution           : %dx%d\n"
        "   bitrare              : %d\n"
        "   framerate            : %d\n",
        cmd->file_input, cmd->file_output,
        cmd->width, cmd->height, cmd->bitRate, cmd->frameRate);

    return OK;
}

status_t testSetupCodec(EncTestArgs *data) {
    sp<ALooper> looper = new ALooper;
    looper->setName("native_enc_looper");
    looper->start();

    sp<MediaCodec> codec = MediaCodec::CreateByType(looper, kMimeTypeAvc, true);
    if (codec == NULL) {
        fprintf(stderr, "ERROR: unable to create codec instance.\n");
        return UNKNOWN_ERROR;
    }

    sp<AMessage> format = new AMessage;
    format->setInt32("width", data->width);
    format->setInt32("height", data->height);
    format->setString("mime", kMimeTypeAvc);
    format->setInt32("color-format", data->colorFormat);
    format->setInt32("bitrate", data->bitRate);
    format->setFloat("frame-rate", data->frameRate);
    format->setInt32("i-frame-interval", data->IDRInterval);

    ALOGD("configure output format is '%s'", format->debugString(0).c_str());

    status_t err = codec->configure(format, NULL, NULL,
            MediaCodec::CONFIGURE_FLAG_ENCODE);
    if (err != NO_ERROR) {
        fprintf(stderr, "ERROR: unable to configure codec at %dx%d (err=%d)\n",
                data->width, data->height, err);
        codec->release();
        return err;
    }

    CHECK_EQ((status_t)OK, codec->start());

    CHECK_EQ((status_t)OK, codec->getInputBuffers(&data->inBuffers));
    CHECK_EQ((status_t)OK, codec->getOutputBuffers(&data->outBuffers));

    ALOGD("got %zu input and %zu output buffers",
          data->inBuffers.size(), data->outBuffers.size());

    data->codec = codec;

    return OK;
}

status_t testEncRun(EncTestArgs *data) {
    status_t err;
    int32_t pktsize, readsize;
    char *pktBuf = NULL;
    size_t index;

    bool lastPktQueued = true;
    bool sawInputEOS = false;
    bool signalledInputEOS = false, signalledOutputEOS = false;

    // YUV420SP input deault
    pktsize = data->width * data->height * 3 / 2;
    pktBuf = (char*)malloc(sizeof(char) * pkt_size);

    while (true) {
        if (!sawInputEOS && lastPktQueued) {
            readsize = fread(pktBuf, 1, pktsize, data->fp_input);
            if (readsize != pktsize && feof(data->fp_input)) {
                ALOGD("saw input eos");
                sawInputEOS = true;
            }

            lastPktQueued = false;
        }

        if (!sawInputEOS) {
            err = data->codec->dequeueInputBuffer(&index, kTimeout);
            if (err == OK) {
                ALOGV("filling input buffer %zu", index);
                const sp<ABuffer> &buffer = data->inBuffers.itemAt(index);
                memcpy(buffer->base(), pktBuf, readsize);
                buffer->setRange(0, readsize);

                err = data->codec->queueInputBuffer(index, 0, buffer->size(), 0, 0);

                CHECK_EQ(err, (status_t)OK);
                lastPktQueued = true;
            } else {
                CHECK_EQ(err, -EAGAIN);
            }
        } else {
            if (!signalledInputEOS) {
                err = data->codec->dequeueInputBuffer(&index, kTimeout);
                if (err == OK) {
                    ALOGD("signalling input EOS");
                    err = data->codec->queueInputBuffer(index, 0, 0,
                             0, MediaCodec::BUFFER_FLAG_EOS);

                    CHECK_EQ(err, (status_t)OK);
                    lastPktQueued = true;
                    signalledInputEOS = true;
                }
            }
        }

        size_t offset, size;
        int64_t presentationTimeUs;
        uint32_t flags;

        err = data->codec->dequeueOutputBuffer(
                    &index, &offset, &size,
                    &presentationTimeUs, &flags, kTimeout);
        if (err == OK) {
            ALOGV("draining output buffer %zu, time = %lld us",
                  index, (long long)presentationTimeUs);

            const sp<ABuffer> &buffer = data->outBuffers.itemAt(index);
            fwrite(buffer->base(), 1, buffer->size(), data->fp_output);
            fflush(data->fp_output);

            if (flags & MediaCodec::BUFFER_FLAG_EOS) {
                signalledOutputEOS = true;
                ALOGD("reached EOS on output.");
            }
            data->numBuffersEncoded += 1;
            err = data->codec->releaseOutputBuffer(index);
            CHECK_EQ(err, (status_t)OK);
        } else if (err == INFO_OUTPUT_BUFFERS_CHANGED) {
            ALOGD("INFO_OUTPUT_BUFFERS_CHANGED");
            CHECK_EQ((status_t)OK,
                     data->codec->getOutputBuffers(&data->outBuffers));
            ALOGD("got %zu output buffers", data->outBuffers.size());
        } else if (err == INFO_FORMAT_CHANGED) {
            sp<AMessage> format;
            CHECK_EQ((status_t)OK, data->codec->getOutputFormat(&format));
            ALOGD("INFO_FORMAT_CHANGED: %s", format->debugString().c_str());
        } else {
            CHECK_EQ(err, -EAGAIN);
        }

        if (signalledInputEOS && signalledOutputEOS)
            break;
    }

    free(pktBuf);

    return 0;
}

int main(int argc, char **argv) {
    status_t err = OK;;
    EncTestArgs cmd;
    int64_t startTimeUs, elapsedTimeUs;

    // parse the cmd option
    if (argc > 1)
        err = testParseArgs(&cmd, argc, argv);

    if (err != OK) {
        testUsage();
        goto ENCODE_OUT;
    }

    // Start Binder thread pool.  MediaCodec needs to be able to receive
    // messages from mediaserver.
    ProcessState::self()->startThreadPool();

    ALOGD("creating codec");
    err = testSetupCodec(&cmd);
    if (err != OK) {
        fprintf(stderr, "ERROR: setup codec failed(err=%d)", err);
        goto ENCODE_OUT;
    }

    startTimeUs = ALooper::GetNowUs();

    ALOGD("start codec test");
    err = testEncRun(&cmd);
    if (err != OK) {
        fprintf(stderr, "ERROR: enc_test_run failed(err=%d)", err);
    } else {
        elapsedTimeUs = ALooper::GetNowUs() - startTimeUs;
        printf("\nenc_test done, %d frame encoded in %lld ms, %.2f fps\n",
               cmd.numBuffersEncoded, elapsedTimeUs / 1000,
               cmd.numBuffersEncoded * 1E6 / elapsedTimeUs);
    }

ENCODE_OUT:
    ALOGD("release codec");

    cmd.codec->stop();
    cmd.codec->release();

    if (cmd.fp_input != NULL)
        fclose(cmd.fp_input);

    if (cmd.fp_output != NULL)
        fclose(cmd.fp_output);

    return 0;
}

