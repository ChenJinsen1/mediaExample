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
 * date  : 2021/02/03
 * module: native-codec: native_dec_test sample code
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "native_dec_test"
#include <utils/Log.h>

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <media/ICrypto.h>
#include <media/IMediaHTTPService.h>
#include <media/IMediaPlayerService.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AString.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/MediaCodecList.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/NuMediaExtractor.h>
#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>
#include <ui/DisplayInfo.h>

using namespace android;

#define MAX_FILE_LEN 128

static int64_t kTimeout = 500ll;

typedef struct DecTestArgs_t {
    // src and dst
    char file_input[MAX_FILE_LEN];
    char file_output[MAX_FILE_LEN];
    int32_t input_width;
    int32_t input_height;

    /* raw 264 or 265 input format */
    bool rawInputFmt;
    /* render surface on the screen */
    bool renderSurface;
} DecTestArgs;

struct CodecState {
    sp<NuMediaExtractor> mExtractor;
    sp<Surface> mSurface;
    sp<MediaCodec> mCodec;
    char mFileInput[MAX_FILE_LEN];
    char mFileOutput[MAX_FILE_LEN];
    Vector<sp<ABuffer> > mInBuffers;
    Vector<sp<ABuffer> > mOutBuffers;
    bool mSignalledInputEOS;
    bool mSawOutputEOS;
    int64_t mNumBuffersDecoded;
};

/*
 * Dumps usage on stderr.
 */
static void testUsage() {
    fprintf(stderr,
        "\nUsage: native_dec_test [options] \n"
        "Android native mediacodec decoder demo.\n"
        "  - 1) native_dec_test --i input.264 --o out.yuv --w 1280 --h 720 --t 1\n"
        "  - 2) native_dec_test --i input.mp4 --t 2\n"
        "\n"
        "Options:\n"
        "--u\n"
        "    Show this message.\n"
        "--i\n"
        "    input file\n"
        "--o\n"
        "    output bitstream files, if output file not specified,\n"
        "    will render surface on the screen\n"
        "--w\n"
        "    the width of input picture\n"
        "--h\n"
        "    the height of input picture\n"
        "--t\n"
        "    input type:\n"
        "        1: raw 264 or 265 format, default\n"
        "        2: with container, use extrator to get decode_input\n"
        "\n");
}

status_t testParseArgs(DecTestArgs *cmd, int argc, char **argv) {
    static const struct option longOptions[] = {
        { "usage",              no_argument,        NULL, 'u' },
        { "input",              required_argument,  NULL, 'i' },
        { "output",             required_argument,  NULL, 'o' },
        { "width",              required_argument,  NULL, 'w' },
        { "height",             required_argument,  NULL, 'h' },
        { "type",               required_argument,  NULL, 't' },
        { NULL,                 0,                  NULL, 0 }
    };

    cmd->input_width = 0;
    cmd->input_height = 0;
    cmd->rawInputFmt = true;
    cmd->renderSurface = true;
    bool hasInput = false, hasOutput = false;

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
            hasInput = true;
            break;
        case 'o':
            strcpy(cmd->file_output, optarg);
            hasOutput = true;
            break;
        case 'w':
            cmd->input_width = atoi(optarg);
            break;
        case 'h':
            cmd->input_height = atoi(optarg);
            break;
        case 't':
            cmd->rawInputFmt = (atoi(optarg) == 2) ? false : true;
            break;
        default:
            fprintf(stderr, "getopt_long returned unexpected value 0x%x\n", ic);
            return BAD_VALUE;
        }
    }

    if (!hasInput) {
        fprintf(stderr, "ERROR: must specify input file\n");
        return BAD_VALUE;
    }

    if (hasOutput) {
        //  don't render surface if we specified output file.
        cmd->renderSurface = false;
    }

    // dump cmd options
    fprintf(stderr, "\ncmd parse result:\n"
        "   input bitstream file : %s\n"
        "   output bitstream file: %s\n"
        "   input_resolution     : %dx%d\n"
        "   cmd->rawInputFmt     : %d\n"
        "   cmd->renderSurface   : %d\n",
        cmd->file_input, cmd->file_output, cmd->input_width,
        cmd->input_height, cmd->rawInputFmt, cmd->renderSurface);

    return OK;
}

status_t testSetupCodec(DecTestArgs cmd, CodecState *state) {
    status_t err = OK;
    sp<AMessage> format;

    sp<ALooper> looper = new ALooper;
    looper->setName("native_dec_looper");
    looper->start();

    if (!cmd.rawInputFmt) {
        sp<NuMediaExtractor> extractor = new NuMediaExtractor;

        // input bitstream with container, we use extractor to get decode_data.
        /* MediaExtractor: register all sniffers */
        DataSource::RegisterDefaultSniffers();
        if (extractor->setDataSource(NULL /* httpService */, cmd.file_input) != OK) {
            fprintf(stderr, "ERROR: unable to instantiate extractor.\n");
            return BAD_VALUE;
        }
        for (size_t i = 0; i < extractor->countTracks(); ++i) {
            status_t err = extractor->getTrackFormat(i, &format);
            CHECK_EQ(err, (status_t)OK);

            AString mime;
            CHECK(format->findString("mime", &mime));

            ALOGD("configure format is '%s'", format->debugString(0).c_str());

            bool isVideo = !strncasecmp(mime.c_str(), "video/", 6);
            ALOGD("mime - %s", mime.c_str());
            if (!isVideo)
                continue;

            ALOGD("selecting track %zu", i);

            err = extractor->selectTrack(i);
            CHECK_EQ(err, (status_t)OK);

            state->mCodec = MediaCodec::CreateByType(
                    looper, mime.c_str(), false /* encoder */);
            CHECK(state->mCodec != NULL);

            state->mExtractor = extractor;
            break;
        }
    } else {
        // h264 default
        static const char* kMimeTypeAvc = "video/avc";
        format = new AMessage;

        format->setInt32("width", cmd.input_width);
        format->setInt32("height", cmd.input_height);
        format->setString("mime", kMimeTypeAvc);

        state->mCodec = MediaCodec::CreateByType(
                looper, kMimeTypeAvc, false /* encoder */);
        CHECK(state->mCodec != NULL);

    }

    ALOGD("configure format is '%s'", format->debugString(0).c_str());

    err = state->mCodec->configure(format, state->mSurface,
            NULL /* crypto */, 0 /* flag */);
    CHECK_EQ(err, (status_t)OK);

    if (state->mCodec == NULL) {
        fprintf(stderr, "ERROR: video track not find.\n");
        return BAD_VALUE;
    }

    CHECK_EQ((status_t)OK, state->mCodec->start());

    CHECK_EQ((status_t)OK, state->mCodec->getInputBuffers(&state->mInBuffers));
    CHECK_EQ((status_t)OK, state->mCodec->getOutputBuffers(&state->mOutBuffers));

    ALOGD("got %zu input and %zu output buffers",
          state->mInBuffers.size(), state->mOutBuffers.size());

    strcpy(state->mFileInput, cmd.file_input);
    strcpy(state->mFileOutput, cmd.file_output);

    state->mSignalledInputEOS = false;
    state->mSawOutputEOS = false;
    state->mNumBuffersDecoded = 0;

    return OK;
}

status_t testDecRunRawInput(CodecState *state) {
    status_t err = OK;
    bool renderSurface = (state->mSurface != NULL);

    FILE *fpInput = NULL, *fpOutput = NULL;
    char *pktBuf = NULL;
    int32_t pktsize = 1000; // 1000 byte

    pktBuf = (char*)malloc(sizeof(char) * pktsize);

    fpInput = fopen(state->mFileInput, "rb+");
    if (fpInput == NULL) {
        fprintf(stderr, "failed to open input file %s\n", state->mFileInput);
        err = BAD_VALUE;
        goto DECODE_OUT;
    }

    if (!renderSurface) {
        fpOutput = fopen(state->mFileOutput, "wb+");
        if (fpOutput == NULL) {
            fprintf(stderr, "failed to open output file %s\n", state->mFileOutput);
            err = BAD_VALUE;
            goto DECODE_OUT;
        }
    }

    bool sawInputEOS = false;
    // Indicates that the last buffer has delivered into vpu_decoder
    bool lastPktQueued = true;
    int32_t readsize;

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
            size_t index;
            err = state->mCodec->dequeueInputBuffer(&index, kTimeout);
            if (err == OK) {
                ALOGV("filling input buffer %zu", index);
                const sp<ABuffer> &buffer = state->mInBuffers.itemAt(index);

                memcpy(buffer->base(), pktBuf, readsize);
                buffer->setRange(0, readsize);

                uint32_t bufferFlags = 0;
                err = state->mCodec->queueInputBuffer(
                        index,
                        0 /* offset */,
                        buffer->size(),
                        0,
                        bufferFlags);

                CHECK_EQ(err, (status_t)OK);
                lastPktQueued = true;
            } else  {
                CHECK_EQ(err, -EAGAIN);
            }
        } else  {
            if (!state->mSignalledInputEOS) {
                size_t index;
                status_t err =
                    state->mCodec->dequeueInputBuffer(&index, kTimeout);
                if (err == OK) {
                    ALOGD("signalling input EOS");
                    err = state->mCodec->queueInputBuffer(
                            index,
                            0 /* offset */,
                            0 /* size */,
                            0ll /* timeUs */,
                            MediaCodec::BUFFER_FLAG_EOS);
                    CHECK_EQ(err, (status_t)OK);
                    lastPktQueued = true;
                    state->mSignalledInputEOS = true;
                } else {
                    CHECK_EQ(err, -EAGAIN);
                }
            }
        }

        size_t index;
        size_t offset;
        size_t size;
        int64_t presentationTimeUs;
        uint32_t flags;
        status_t err = state->mCodec->dequeueOutputBuffer(
                &index, &offset, &size, &presentationTimeUs, &flags,
                kTimeout);
        if (err == OK) {
            ALOGV("draining output buffer %zu, time = %lld us",
                  index, (long long)presentationTimeUs);

            ++state->mNumBuffersDecoded;

            if (renderSurface) {
                err = state->mCodec->renderOutputBufferAndRelease(index);
            } else {
                const sp<ABuffer> &buffer = state->mOutBuffers.itemAt(index);
                fwrite(buffer->base(), 1, buffer->size(), fpOutput);
                fflush(fpOutput);

                err = state->mCodec->releaseOutputBuffer(index);
            }

            CHECK_EQ(err, (status_t)OK);

            if (flags & MediaCodec::BUFFER_FLAG_EOS) {
                ALOGD("reached EOS on output.");

                state->mSawOutputEOS = true;
            }
        } else if (err == INFO_OUTPUT_BUFFERS_CHANGED) {
            ALOGD("INFO_OUTPUT_BUFFERS_CHANGED");
            CHECK_EQ((status_t)OK,
                     state->mCodec->getOutputBuffers(&state->mOutBuffers));

            ALOGD("got %zu output buffers", state->mOutBuffers.size());
        } else if (err == INFO_FORMAT_CHANGED) {
            sp<AMessage> format;
            CHECK_EQ((status_t)OK, state->mCodec->getOutputFormat(&format));

            ALOGD("INFO_FORMAT_CHANGED: %s", format->debugString().c_str());
        } else {
            CHECK_EQ(err, -EAGAIN);
        }

        if (state->mSawOutputEOS)
            break;
    }

    err = OK;

DECODE_OUT:
    state->mCodec->release();

    free(pktBuf);

    if (fpInput != NULL)
        fclose(fpInput);

    if (fpOutput != NULL)
        fclose(fpOutput);

    return err;
}

status_t testDecRunExtrator(CodecState *state) {
    status_t err = OK;
    bool renderSurface = (state->mSurface != NULL);

    FILE *fpOutput = NULL;
    bool sawInputEOS = false;

    if (!renderSurface) {
        fpOutput = fopen(state->mFileOutput, "wb+");
        if (fpOutput == NULL) {
            fprintf(stderr, "failed to open output file %s\n", state->mFileOutput);
            err = BAD_VALUE;
            goto DECODE_OUT;
        }
    }

    while (true) {
        if (!sawInputEOS) {
            size_t trackIndex;
            err = state->mExtractor->getSampleTrackIndex(&trackIndex);
            if (err != OK) {
                ALOGD("saw input eos");
                sawInputEOS = true;
            } else {
                size_t index;
                err = state->mCodec->dequeueInputBuffer(&index, kTimeout);
                if (err == OK) {
                    ALOGV("filling input buffer %zu", index);
                    const sp<ABuffer> &buffer = state->mInBuffers.itemAt(index);

                    err = state->mExtractor->readSampleData(buffer);
                    CHECK_EQ(err, (status_t)OK);

                    int64_t timeUs;
                    err = state->mExtractor->getSampleTime(&timeUs);
                    CHECK_EQ(err, (status_t)OK);

                    uint32_t bufferFlags = 0;
                    err = state->mCodec->queueInputBuffer(
                            index,
                            0 /* offset */,
                            buffer->size(),
                            timeUs,
                            bufferFlags);

                    CHECK_EQ(err, (status_t)OK);

                    state->mExtractor->advance();
                } else  {
                    CHECK_EQ(err, -EAGAIN);
                }
            }
        } else  {
            if (!state->mSignalledInputEOS) {
                size_t index;
                status_t err =
                    state->mCodec->dequeueInputBuffer(&index, kTimeout);
                if (err == OK) {
                    ALOGD("signalling input EOS");

                    err = state->mCodec->queueInputBuffer(
                            index,
                            0 /* offset */,
                            0 /* size */,
                            0ll /* timeUs */,
                            MediaCodec::BUFFER_FLAG_EOS);

                    CHECK_EQ(err, (status_t)OK);

                    state->mSignalledInputEOS = true;
                } else {
                    CHECK_EQ(err, -EAGAIN);
                }
            }
        }

        size_t index;
        size_t offset;
        size_t size;
        int64_t presentationTimeUs;
        uint32_t flags;
        status_t err = state->mCodec->dequeueOutputBuffer(
                &index, &offset, &size, &presentationTimeUs, &flags,
                kTimeout);
        if (err == OK) {
            ALOGV("draining output buffer %zu, time = %lld us",
                  index, (long long)presentationTimeUs);

            ++state->mNumBuffersDecoded;

            if (renderSurface) {
                err = state->mCodec->renderOutputBufferAndRelease(index);
            } else {
                const sp<ABuffer> &buffer = state->mOutBuffers.itemAt(index);
                fwrite(buffer->base(), 1, buffer->size(), fpOutput);
                fflush(fpOutput);

                err = state->mCodec->releaseOutputBuffer(index);
            }

            CHECK_EQ(err, (status_t)OK);

            if (flags & MediaCodec::BUFFER_FLAG_EOS) {
                ALOGD("reached EOS on output.");

                state->mSawOutputEOS = true;
            }
        } else if (err == INFO_OUTPUT_BUFFERS_CHANGED) {
            ALOGD("INFO_OUTPUT_BUFFERS_CHANGED");
            CHECK_EQ((status_t)OK,
                     state->mCodec->getOutputBuffers(&state->mOutBuffers));

            ALOGD("got %zu output buffers", state->mOutBuffers.size());
        } else if (err == INFO_FORMAT_CHANGED) {
            sp<AMessage> format;
            CHECK_EQ((status_t)OK, state->mCodec->getOutputFormat(&format));

            ALOGD("INFO_FORMAT_CHANGED: %s", format->debugString().c_str());
        } else {
            CHECK_EQ(err, -EAGAIN);
        }

        if (state->mSawOutputEOS)
            break;
    }

    err = OK;

DECODE_OUT:
    state->mCodec->release();

    if (fpOutput)
        fclose(fpOutput);

    return OK;
}

int main(int argc, char **argv) {
    status_t err = OK;;
    DecTestArgs cmd;
    CodecState state;
    int64_t startTimeUs, elapsedTimeUs;

    // parse the cmd option
    if (argc > 1)
        err = testParseArgs(&cmd, argc, argv);

    if (err != OK) {
        testUsage();
        return 1;
    }

    // Start Binder thread pool.  MediaCodec needs to be able to receive
    // messages from mediaserver.
    ProcessState::self()->startThreadPool();

    sp<SurfaceComposerClient> composerClient;
    sp<SurfaceControl> control;

    if (cmd.renderSurface) {
        composerClient = new SurfaceComposerClient;
        CHECK_EQ(composerClient->initCheck(), (status_t)OK);

        sp<IBinder> display(SurfaceComposerClient::getBuiltInDisplay(
                ISurfaceComposer::eDisplayIdMain));
        DisplayInfo info;
        SurfaceComposerClient::getDisplayInfo(display, &info);
        ssize_t displayWidth = info.w;
        ssize_t displayHeight = info.h;

        ALOGD("renderSurface - display is %zd x %zd\n", info.w, info.h);

        control = composerClient->createSurface(
                String8("A Surface"),
                displayWidth,
                displayHeight,
                PIXEL_FORMAT_RGB_565,
                0);

        CHECK(control != NULL);
        CHECK(control->isValid());

        SurfaceComposerClient::openGlobalTransaction();
        CHECK_EQ(control->setLayer(INT_MAX), (status_t)OK);
        CHECK_EQ(control->show(), (status_t)OK);
        SurfaceComposerClient::closeGlobalTransaction();

        state.mSurface = control->getSurface();
        CHECK(state.mSurface != NULL);
    }

    ALOGD("setup codec");
    err = testSetupCodec(cmd, &state);
    if (err != OK) {
        fprintf(stderr, "ERROR: setup codec failed(err=%d)", err);
        return 1;
    }

    startTimeUs = ALooper::GetNowUs();

    ALOGD("start codec test");
    if (cmd.rawInputFmt) {
        err = testDecRunRawInput(&state);
    } else {
        err = testDecRunExtrator(&state);
    }
    if (err != OK) {
        fprintf(stderr, "ERROR: dec_test_run failed(err=%d)", err);
    } else {
        elapsedTimeUs = ALooper::GetNowUs() - startTimeUs;
        printf("\ndec_test done, %lld frames decoded in %lld ms, %.2f fps\n",
               (long long)state.mNumBuffersDecoded, elapsedTimeUs / 1000,
               state.mNumBuffersDecoded * 1E6 / elapsedTimeUs);
    }

    if (cmd.renderSurface && composerClient != NULL) {
        composerClient->dispose();
    }

    return 0;
}

