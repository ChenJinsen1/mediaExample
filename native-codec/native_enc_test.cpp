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
 *   date: 2021/01/28
 * module: native-codec: native_enc_test sample code
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

static const char* kMimeTypeAvc = "video/avc";

typedef struct {
    struct timeval start;
    struct timeval end;
} DebugTimeInfo;

static DebugTimeInfo time_info;

static void time_start_record()
{
    gettimeofday(&time_info.start, NULL);
}

static uint64_t time_end_record()
{
    gettimeofday(&time_info.end, NULL);
    return (time_info.end.tv_sec  - time_info.start.tv_sec)  * 1000 +
           (time_info.end.tv_usec - time_info.start.tv_usec) / 1000;
}

typedef struct EncTestData_t {
    // src and dst
    FILE                *fp_input;
    FILE                *fp_output;
    char                file_input[128];
    char                file_output[128];
    uint32_t            frameCount;

    // configure parameters of encoder
    Vector<sp<ABuffer>>  inBuffers;
    Vector<sp<ABuffer>>  outBuffers;
    uint32_t             width;
    uint32_t             height;
    uint32_t             bitRate;
    uint32_t             colorFormat;
    uint32_t             frameRate;
    uint32_t             IDRInterval;
    // codec timeout value
    int64_t              timeout;
} EncTestData;

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

status_t testSetupEncDataByArgs(EncTestData *data, int argc, char **argv) {
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

    data->fp_input = NULL;
    data->fp_output = NULL;
    data->width = 0;
    data->height = 0;
    data->frameRate = 0;
    data->bitRate = 0;

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
            strcpy(data->file_input, optarg);
            data->fp_input = fopen(data->file_input, "rb+");
            if (data->fp_input == NULL) {
                ALOGD("failed to open input file %s", data->file_input);
                return BAD_VALUE;
            }
            break;
        case 'o':
            strcpy(data->file_output, optarg);
            data->fp_output = fopen(data->file_output, "wb+");
            if (data->fp_input == NULL) {
                ALOGD("failed to open output file %s", data->file_output);
                return BAD_VALUE;
            }
            break;
        case 'w':
            data->width = atoi(optarg);
            break;
        case 'h':
            data->height = atoi(optarg);
            break;
        case 'b':
            data->bitRate = atoi(optarg);
            break;
        case 'f':
            data->frameRate = atoi(optarg);
            break;
        default:
            fprintf(stderr, "getopt_long returned unexpected value 0x%x\n", ic);
            return BAD_VALUE;
        }
    }

    if (data->fp_input == NULL || data->fp_output == NULL
            || data->width == 0 || data->height == 0) {
        ALOGE("ERROR: native_enc_test must specify input|output|width|height.");
        return BAD_VALUE;
    }

    if (data->bitRate <= 0) {
        data->bitRate = 3000000; // 3Mbps
    }
    if (data->frameRate <= 0) {
        data->frameRate = 30; // 30fps
    }
    data->frameCount = 0;
    data->colorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    data->IDRInterval = 1;
    data->timeout = 20 * 1000;

    // dump cmd options
    fprintf(stderr,
        "\ncmd parse result:\n"
        "   input bitstream file:  %s\n"
        "   output bitstream file: %s\n"
        "   resolution:  %dx%d\n"
        "   bitrare:     %d\n"
        "   framerate:   %d\n",
        data->file_input, data->file_output,
        data->width, data->height, data->bitRate, data->frameRate);

    return OK;
}

status_t testSetupCodec(sp<MediaCodec>* pCodec, EncTestData *data) {
    status_t err;

    sp<ALooper> looper = new ALooper;
    looper->setName("native_enc_looper");
    looper->start();

    sp<MediaCodec> codec = MediaCodec::CreateByType(looper, kMimeTypeAvc, true);
    if (codec == NULL) {
        ALOGD("ERROR: unable to create %s codec instance", kMimeTypeAvc);
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

    err = codec->configure(format, NULL, NULL,
            MediaCodec::CONFIGURE_FLAG_ENCODE);
    if (err != NO_ERROR) {
        ALOGD("ERROR: unable to configure %s codec at %dx%d (err=%d)",
              kMimeTypeAvc, data->width, data->height, err);
        codec->release();
        return err;
    }

    CHECK_EQ((status_t)OK, codec->start());

    CHECK_EQ((status_t)OK, codec->getInputBuffers(&data->inBuffers));
    CHECK_EQ((status_t)OK, codec->getOutputBuffers(&data->outBuffers));

    ALOGD("got %zu input and %zu output buffers",
          data->inBuffers.size(), data->outBuffers.size());

    *pCodec = codec;

    return OK;
}

status_t testEncodeRun(sp<MediaCodec> encoder, EncTestData *data) {
    status_t err;
    int32_t pkt_size, read_size;
    char *pkt_buf = NULL;
    size_t index;
    bool lastPktQueued = true;
    bool sawInputEOS = false;
    bool signalledInputEOS = false, signalledOutputEOS = false;

    // YUV420SP input deault
    pkt_size = data->width * data->height * 3 / 2;
    pkt_buf = (char*)malloc(sizeof(char) * pkt_size);

    while (true) {
        if (!sawInputEOS && lastPktQueued) {
            read_size = fread(pkt_buf, 1, pkt_size, data->fp_input);
            if (read_size != pkt_size && feof(data->fp_input)) {
                ALOGD("saw input eos");
                sawInputEOS = true;
            }

            lastPktQueued = false;
        }

        if (!sawInputEOS) {
            err = encoder->dequeueInputBuffer(&index, data->timeout);
            if (err == OK) {
                ALOGV("filling input buffer %zu", index);
                const sp<ABuffer> &buffer = data->inBuffers.itemAt(index);
                memcpy(buffer->base(), pkt_buf, read_size);
                buffer->setRange(0, read_size);

                err = encoder->queueInputBuffer(index, 0, buffer->size(), 0, 0);

                CHECK_EQ(err, (status_t)OK);
                lastPktQueued = true;
            } else {
                CHECK_EQ(err, -EAGAIN);
            }
        } else {
            if (!signalledInputEOS) {
                err = encoder->dequeueInputBuffer(&index, data->timeout);
                if (err == OK) {
                    ALOGD("signalling input EOS");
                    err = encoder->queueInputBuffer(index, 0, 0,
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

        err = encoder->dequeueOutputBuffer(
                    &index, &offset, &size,
                    &presentationTimeUs, &flags, data->timeout);
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
            data->frameCount += 1;
            err = encoder->releaseOutputBuffer(index);
            CHECK_EQ(err, (status_t)OK);
        } else if (err == INFO_OUTPUT_BUFFERS_CHANGED) {
            ALOGD("INFO_OUTPUT_BUFFERS_CHANGED");
            CHECK_EQ((status_t)OK,
                     encoder->getOutputBuffers(&data->outBuffers));
            ALOGD("got %zu output buffers", data->outBuffers.size());
        } else if (err == INFO_FORMAT_CHANGED) {
            sp<AMessage> format;
            CHECK_EQ((status_t)OK, encoder->getOutputFormat(&format));
            ALOGD("INFO_FORMAT_CHANGED: %s", format->debugString().c_str());
        } else {
            CHECK_EQ(err, -EAGAIN);
        }

        if (signalledInputEOS && signalledOutputEOS)
            break;
    }

    return 0;
}

int main(int argc, char **argv) {
    status_t err = OK;;
    sp<MediaCodec> encoder;
    EncTestData tData;

    // parse the cmd option
    if (argc > 1)
        err = testSetupEncDataByArgs(&tData, argc, argv);

    if (err != OK) {
        testUsage();
        return 1;
    }

   // Start Binder thread pool.  MediaCodec needs to be able to receive
   // messages from mediaserver.
   sp<ProcessState> self = ProcessState::self();
   self->startThreadPool();

    ALOGD("creating codec");
    err = testSetupCodec(&encoder, &tData);
    if (err != OK)
        return err;

    time_start_record();

    err = testEncodeRun(encoder, &tData);
    if (err != OK) {
        ALOGD("ERROR: enc_test_run quit (err=%d)", err);
    } else {
        uint64_t consumes = time_end_record();
        ALOGD("native_enc_test success total frame %d use_time %lld ms",
              tData.frameCount, consumes);
    }

//ENCODE_OUT:
    ALOGD("release codec");

    encoder->stop();
    encoder->release();

    return 0;
}

